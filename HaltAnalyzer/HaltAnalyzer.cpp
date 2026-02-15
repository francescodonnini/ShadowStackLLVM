#include "HaltAnalyzer.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

void llvm::HaltAnalyzer::findHaltCalls(Function &F) {
  Calls.clear();
  for (auto &I : instructions(F)) {
    if (auto *CI = dyn_cast<CallInst>(&I)) {
      if (CI->getCalledFunction()->getName() == HaltFunName) {
        Calls.push_back(&I);
      }
    }
  }
}

PreservedAnalyses HaltAnalyzer::run(Function &F, FunctionAnalysisManager &FAM) {
  findHaltCalls(F);
  auto &DT = FAM.getResult<DominatorTreeAnalysis>(F);
  SmallVector<BasicBlock*, 4> DomBBs;
  for (auto *I : Calls) {
    auto *BB = I->getParent();
    DomBBs.clear();
    DT.getDescendants(BB, DomBBs);
    for (auto *DomBB : DomBBs) {
      if (DomBB != BB) {
        DomBB->printAsOperand(errs() << "[WARNING] Unreachable BB: ");
        errs() << "\n";
      }
    }
  }
  return PreservedAnalyses::all();
}

extern "C" ::llvm::PassPluginLibraryInfo LLVM_ATTRIBUTE_WEAK
llvmGetPassPluginInfo() {
  return {
    LLVM_PLUGIN_API_VERSION, "StrictOpt", "v0.1",
    [](PassBuilder &PB) {
      PB.registerOptimizerLastEPCallback(
        [](ModulePassManager &MPM, OptimizationLevel OL, ThinOrFullLTOPhase Phase) {
          MPM.addPass(createModuleToFunctionPassAdaptor(HaltAnalyzer()));
        });
    }
  };
}