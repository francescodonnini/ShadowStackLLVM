#include "ShadowStack.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "shadow-stack-opt"

using namespace llvm;

PreservedAnalyses SSFunctionPass::run(Function &F, FunctionAnalysisManager &FAM) {
    if (F.isDeclaration()) {
      errs() << "Skipping function " << F.getName() << "\n";
      return PreservedAnalyses::all();
    } else {
      errs() << "Instrumenting function " << F.getName() << "\n";
    }

    instrumentPreamble(F);
    instrumentEpilogue(F);

    return PreservedAnalyses::none();
}

void SSFunctionPass::instrumentPreamble(Function &F) {
  auto &EntryBB = F.getEntryBlock();
  
  IRBuilder<> Builder(&EntryBB, EntryBB.getFirstNonPHIIt());

  auto *Int64Ty = Builder.getInt64Ty();
  auto *GsPtrTy = PointerType::get(Int64Ty, GS_ADDR_SPACE);
  
  // Calculate new top of stack
  auto *ShadowAreaPtr = Builder.CreateIntToPtr(Builder.getInt64(0), GsPtrTy);
  auto *SSTop = Builder.CreateLoad(Int64Ty, ShadowAreaPtr, true);
  auto *NewSSTop = Builder.CreateAdd(SSTop, Builder.getInt64(8));

  // Update stack's header (gs:0) with new top of stack
  Builder.CreateStore(NewSSTop, ShadowAreaPtr, true);

  // Get return address
  auto *GetRetAddr = Intrinsic::getOrInsertDeclaration(F.getParent(), Intrinsic::returnaddress);
  auto *RetAddrPtr = Builder.CreateCall(GetRetAddr, {Builder.getInt32(0)});
  auto *RetAddr = Builder.CreatePtrToInt(RetAddrPtr, Int64Ty);

  // Store return address at the top of the shadow stack
  auto *NewSSTopPtr = Builder.CreateIntToPtr(NewSSTop, GsPtrTy);
  auto *StoreRetAddr = Builder.CreateStore(RetAddr, NewSSTopPtr, true);
}

void SSFunctionPass::instrumentEpilogue(Function &F) {
  SmallVector<ReturnInst*, 8> Rets;
  for (auto &BB : F) {
      if (auto *RI = dyn_cast<ReturnInst>(BB.getTerminator())) {
          Rets.push_back(RI);
      }
  }

  for (auto RI : Rets) {
    instrumentRet(F, *RI);
  }
}

void SSFunctionPass::instrumentRet(Function &F, ReturnInst &I) {
  IRBuilder<> Builder(&I);

  auto *Int64Ty = Builder.getInt64Ty();
  auto *GsPtrTy = PointerType::get(Int64Ty, GS_ADDR_SPACE);

  // Load from shadow stack
  auto *SSTopPtr = Builder.CreateIntToPtr(Builder.getInt64(-8), GsPtrTy);
  auto *SSTop = Builder.CreateLoad(Int64Ty, SSTopPtr, true);

  // Get return address
  auto *GetRetAddr = Intrinsic::getOrInsertDeclaration(I.getModule(), Intrinsic::returnaddress);
  auto *RetAddrPtr = Builder.CreateCall(GetRetAddr, {Builder.getInt32(0)});
  auto *RetAddr = Builder.CreatePtrToInt(RetAddrPtr, Int64Ty);

  // Compare return address
  auto *Compare = Builder.CreateICmpNE(SSTop, RetAddr);

  auto *CheckBB = I.getParent();
  auto *RetBB = CheckBB->splitBasicBlock(&I);

  CheckBB->getTerminator()->eraseFromParent();

  // Fail path
  auto *TrueBr = BasicBlock::Create(F.getContext(), "ss_fail", &F);
  IRBuilder<> TBuilder(TrueBr);
  auto *Trap = Intrinsic::getOrInsertDeclaration(F.getParent(), Intrinsic::trap);
  TBuilder.CreateCall(Trap);
  TBuilder.CreateUnreachable();

  // Link CheckBB to FailBB + RetBB
  Builder.SetInsertPoint(CheckBB);
  Builder.CreateCondBr(Compare, TrueBr, RetBB);

  // Success path
  IRBuilder<> FBuilder(&RetBB->front());
  auto *NewTop = FBuilder.CreateSub(SSTopPtr, Builder.getInt64(8), "ss_pop");

  auto *PopHeadPtr = FBuilder.CreateIntToPtr(FBuilder.getInt64(-8), GsPtrTy);
  FBuilder.CreateStore(NewTop, PopHeadPtr, true);
}

extern "C" ::llvm::PassPluginLibraryInfo LLVM_ATTRIBUTE_WEAK
llvmGetPassPluginInfo() {
  return {
    LLVM_PLUGIN_API_VERSION, "ShadowStack", "v0.1",
    [](PassBuilder &PB) {
      // Run Pass before other optimizations when the optimization
      // level is at least -O2
      PB.registerPipelineStartEPCallback(
        [](ModulePassManager &MPM, OptimizationLevel OL) {
          if (OL.getSpeedupLevel() >= 2) {
            // Since `PassBuilder::registerPipelineStartEPCallback`
            // only accept ModulePass, we need an adapter to make
            // it work.
            MPM.addPass(createModuleToFunctionPassAdaptor(SSFunctionPass()));
            
            errs() << "[ss] ShadowStack registered successfully\n";
          } else {
            errs() << "[ss] ShadowStack registered successfully\n";
          }
        });

      PB.registerPipelineParsingCallback(
        [](StringRef Name, ModulePassManager &MPM,
           ArrayRef<PassBuilder::PipelineElement>) {
          if (Name == "shadow-stack") {
            // Add the pass when invoked manually via -passes="shadow-stack"
            MPM.addPass(createModuleToFunctionPassAdaptor(SSFunctionPass()));
            return true;
          }
          return false;
        });
    }
  };
}