#include "X86.h"          
#include "X86InstrInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"          
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/Target/TargetRegisterInfo.h"

using namespace llvm;

namespace {

class SSX86MachinePass : public MachineFunctionPass {
    StringRef getPassName() const override {
        return "X86 Shadow Stack Preamble/Epilogue Instrumentator";
    }

    bool runOnFunction(Function &F);
};
}