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

  auto *M = F.getParent();
  auto *Int64Ty = Builder.getInt64Ty();
  auto *GsPtrTy = PointerType::get(Int64Ty, GS_ADDR_SPACE);
  auto *Int64PtrTy = PointerType::getUnqual(Int64Ty);

  auto *RetAdrIntr = Intrinsic::getOrInsertDeclaration(M, Intrinsic::returnaddress);
  auto *RetAdrPtr = Builder.CreateCall(RetAdrIntr, {Builder.getInt32(0)});
  auto *RetAdrVal = Builder.CreatePtrToInt(RetAdrPtr, Int64Ty, "ret_adr_before");

  // 2. Push the return address at the top of the shadow stack
  // Get the address of the top of the shadow stack
  // The address is stored as the first element of the shadow stack object
  auto *SSOPtr = Builder.CreateIntToPtr(Builder.getInt64(0), GsPtrTy, "ss_obj_ptr");
  auto *SSTopFieldPtr = Builder.CreateLoad(Int64PtrTy, SSOPtr);
  auto *SSTopPtrPtr = Builder.CreateLoad(Int64PtrTy, SSTopFieldPtr);

  Builder.CreateStore(RetAdrVal, SSTopPtrPtr);

  // 4. Advance the top of the shadow stack
  auto *NewSSTopPtr = Builder.CreateGEP(Int64Ty, SSTopFieldPtr, Builder.getInt64(1), "ss_push");
  Builder.CreateStore(NewSSTopPtr, SSOPtr);
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

  auto *M = F.getParent();
  auto *Int64Ty = Builder.getInt64Ty();
  auto *GsPtrTy = PointerType::get(Int64Ty, GS_ADDR_SPACE);
  auto *Int64PtrTy = PointerType::get(Int64Ty, 0);

  auto *AdrOfRetAdrIntr = Intrinsic::getOrInsertDeclaration(M, Intrinsic::addressofreturnaddress, { Builder.getPtrTy() });
  auto *AdrOfRetAdrPtr = Builder.CreateCall(AdrOfRetAdrIntr, {});
  auto *RetAdrVal = Builder.CreateLoad(Int64Ty, AdrOfRetAdrPtr, true, "ret_adr_after");

  // 2. Pop the return address from the top of the shadow stack
  // Get the address of the top of the shadow stack
  // The address is stored as the first element of the shadow stack object
  auto *SSOPtr = Builder.CreateIntToPtr(Builder.getInt64(0), GsPtrTy, "ss_obj_ptr");
  auto *SSTopFieldPtr = Builder.CreateLoad(Int64PtrTy, SSOPtr);
  auto *SSTopPtr = Builder.CreateLoad(Int64PtrTy, SSTopFieldPtr);
  auto *SSCurPtr = Builder.CreateGEP(Int64Ty, SSTopPtr, Builder.getInt64(-1), "ss_curr_ptr");
  auto *ShadowRetVal = Builder.CreateLoad(Int64Ty, SSCurPtr);
  // Update the top of the shadow stack
  Builder.CreateStore(SSCurPtr, SSOPtr);
  
  // Compare return address
  auto *Compare = Builder.CreateICmpNE(ShadowRetVal, RetAdrVal);

  auto *CheckBB = I.getParent();
  auto *RetBB = CheckBB->splitBasicBlock(&I, "ss_ret");
  CheckBB->getTerminator()->eraseFromParent();

  // Fail path
  auto *FailBB = BasicBlock::Create(F.getContext(), "ss_fail", &F);
  IRBuilder<> FailPathBuilder(FailBB);
  auto *Trap = Intrinsic::getOrInsertDeclaration(M, Intrinsic::trap);
  FailPathBuilder.CreateCall(Trap);
  FailPathBuilder.CreateUnreachable();

  // Link CheckBB to FailBB + RetBB
  Builder.SetInsertPoint(CheckBB);
  Builder.CreateCondBr(Compare, FailBB, RetBB);
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
          MPM.addPass(createModuleToFunctionPassAdaptor(SSFunctionPass()));
          errs() << "[ss] ShadowStack registered successfully\n";
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