#include "StrictOpt.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Function.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

PreservedAnalyses StrictOpt::run(Function &F, FunctionAnalysisManager &FAM) {
    errs() << "[strict-opt]\tFunction " << F.getName() << "\n";
    auto Modified = false;
    for (auto &Arg : F.args()) {
        errs() << "[strict-opt]\t\tArg " << Arg.getName() << "\n";
        if (Arg.getType()->isPointerTy()
            && !Arg.hasAttribute(Attribute::NoAlias)) {
            Arg.addAttr(Attribute::NoAlias);
            Modified |= true;
        }
    }

    auto PA = PreservedAnalyses::all();
    if (Modified) {
        PA.abandon<AAManager>();
    }
    return PA;
}

extern "C" ::llvm::PassPluginLibraryInfo LLVM_ATTRIBUTE_WEAK
llvmGetPassPluginInfo() {
  return {
    LLVM_PLUGIN_API_VERSION, "StrictOpt", "v0.1",
    [](PassBuilder &PB) {
#ifdef STRICT_OPT_USE_PIPELINE_PARSER
      // Use opt's `--passes` textual pipeline description to trigger
      // StrictOpt
      using PipelineElement = typename PassBuilder::PipelineElement;
      PB.registerPipelineParsingCallback(
        [](StringRef Name, FunctionPassManager &FPM, ArrayRef<PipelineElement>){
          if (Name == "strict-opt") {
            FPM.addPass(StrictOpt());
            return true;
          }
          return false;
        });
#else
      // Run Pass before other optimizations when the optimization
      // level is at least -O2
      PB.registerPipelineStartEPCallback(
        [](ModulePassManager &MPM, OptimizationLevel OL) {
          
          errs() << "[strict-opt] PipelineStartEPCallback\n";

          if (OL.getSpeedupLevel() >= 2) {
            // Since `PassBuilder::registerPipelineStartEPCallback`
            // only accept ModulePass, we need an adapter to make
            // it work.

            errs() << "[strict-opt] StrictOpt registered successfully\n";

            MPM.addPass(createModuleToFunctionPassAdaptor(StrictOpt()));
          } else {
            errs() << "[strict-opt] StrictOpt cannot be registered\n";
          }
        });
#endif
    }
  };
}