//===-- CodeBeadsGen.cpp - Code Beads Generator -*- C++ -*-----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file contains tablegen implementation of "code beads", binary strings
/// of arbitrary length defined for an instruction. They are used to pass very
/// target specific binary information directly from a tablegen file, e.g.
/// instruction encoding.
///
//===----------------------------------------------------------------------===//

#include "CodeGenTarget.h"

#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/Debug.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Record.h"
#include "llvm/TableGen/TableGenBackend.h"

#include <string>

using namespace llvm;

namespace {

class CodeBeadsGen {
  RecordKeeper &Records;

public:
  CodeBeadsGen(RecordKeeper &R) : Records(R) {}
  void run(raw_ostream &o);
};

void CodeBeadsGen::run(raw_ostream &o) {
  CodeGenTarget Target(Records);

  // For little-endian instruction bit encodings, reverse the bit order
  Target.reverseBitsForLittleEndianEncoding();

  auto Instructions = Target.getInstructionsByEnumValue();

  constexpr unsigned BeadSize = 8;
  constexpr unsigned BeadsNumber = 24;
  constexpr unsigned BeadsLength = BeadsNumber * BeadSize;

  // Emit function declaration
  o << "const uint" << BeadSize << "_t * " << Target.getName();
  o << "MCCodeEmitter::getGenInstrBeads(const MCInst &MI) const {\n";

  // Emit instruction base values
  o << "  static const uint" << BeadSize << "_t InstBits[][" << BeadsNumber
    << "] = {\n";

  for (const auto *CGI : Instructions) {
    Record *R = CGI->TheDef;

    // Pseudos don't have encoding
    if (R->getValueAsString("Namespace") == "TargetOpcode" ||
        R->getValueAsBit("isPseudo")) {
      o << "\t{ 0x0 },";
      o << '\t' << "// (Pseudo) " << R->getName() << "\n";
      continue;
    }

    BitsInit *Beads = R->getValueAsBitsInit("Beads");

    if (!Beads->isComplete()) {
      PrintFatalError(R->getLoc(), "Record `" + R->getName() +
                                       "', bit field 'Beads' is not complete");
    }

    if (Beads->getNumBits() > BeadsLength) {
      PrintFatalError(R->getLoc(),
                      "Record `" + R->getName() +
                          "', bit field 'Beads' is too long(maximum: " +
                          std::to_string(BeadsLength) + ")");
    }

    // Convert to byte array:
    // [dcba] -> [a][b][c][d]
    o << "\t{";
    for (unsigned p = 0; p < BeadsNumber; ++p) {
      unsigned Right = BeadSize * p;
      unsigned Left = Right + BeadSize;
      unsigned Value = 0;

      for (unsigned i = Right; i != Left; ++i) {
        unsigned Shift = i % BeadSize;

        if (BitInit *B = dyn_cast<BitInit>(Beads->getBit(i))) {
          Value |= (static_cast<unsigned>(B->getValue()) << Shift);
        } else {
          PrintFatalError(R->getLoc(),
                          "Record `" + R->getName() + "', bit 'Beads[" +
                              std::to_string(i) + "]' is not defined");
        }
      }

      if (p != 0) {
        o << ',';
      }

      o << " 0x";
      o.write_hex(Value);
      o << "";
    }
    o << " }," << '\t' << "// " << R->getName() << "\n";
  }
  o << "\t{ 0x0 }\n  };\n";

  // Emit initial function code
  o << "  return InstBits[MI.getOpcode()];\n"
    << "}\n\n";
}

} // anonymous namespace

namespace llvm {

void EmitCodeBeads(RecordKeeper &RK, raw_ostream &OS) {
  emitSourceFileHeader("Machine Code Beads", OS);
  CodeBeadsGen(RK).run(OS);
}

} // namespace llvm
