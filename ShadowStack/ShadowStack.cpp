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

    instrumentStores(F);
    instrumentPreamble(F);
    instrumentEpilogue(F);

    return PreservedAnalyses::none();
}

void SSFunctionPass::instrumentPreamble(Function &F) {
  auto &EntryBB = F.getEntryBlock();
  IRBuilder<> Builder(&EntryBB, EntryBB.getFirstNonPHIIt());

  auto *M = F.getParent();
  auto *I64Ty = Builder.getInt64Ty();
  auto *I64PtrTy = PointerType::getUnqual(I64Ty);
  auto *GsI64PtrTy = PointerType::get(I64Ty, GS_ADDR_SPACE);

  auto *RetAdrIntr = Intrinsic::getOrInsertDeclaration(M, Intrinsic::returnaddress);
  auto *RetAdrPtr = Builder.CreateCall(RetAdrIntr, {Builder.getInt32(0)});
  auto *RetAdrVal = Builder.CreatePtrToInt(RetAdrPtr, I64Ty, "ret_adr_before");

  auto *SSOPtr = Builder.CreateIntToPtr(Builder.getInt64(8), GsI64PtrTy, "ss_obj_ptr");
  auto *SSTopI64 = Builder.CreateLoad(I64Ty, SSOPtr);
  auto *SSTopPtr = Builder.CreateIntToPtr(SSTopI64, I64PtrTy);

  Builder.CreateStore(RetAdrVal, SSTopPtr);

  auto *NewSSTopPtr = Builder.CreateGEP(I64Ty, SSTopPtr, Builder.getInt64(1), "ss_push");
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
  auto *I64Ty = Builder.getInt64Ty();
  auto *I64PtrTy = PointerType::getUnqual(I64Ty);
  auto *GsI64PtrTy = PointerType::get(I64Ty, GS_ADDR_SPACE);

  auto *AdrOfRetAdrIntr = Intrinsic::getOrInsertDeclaration(M, Intrinsic::addressofreturnaddress, { Builder.getPtrTy() });
  auto *AdrOfRetAdrPtr = Builder.CreateCall(AdrOfRetAdrIntr, {});
  auto *RetAdrVal = Builder.CreateLoad(I64Ty, AdrOfRetAdrPtr, true, "ret_adr_after");

  // 2. Pop the return address from the top of the shadow stack
  // Get the address of the top of the shadow stack
  // The address is stored as the first element of the shadow stack object
  auto *SSOPtr = Builder.CreateIntToPtr(Builder.getInt64(8), GsI64PtrTy, "ss_obj_ptr");
  auto *SSTopPtr = Builder.CreateLoad(I64PtrTy, SSOPtr);

  auto *SSCurPtr = Builder.CreateGEP(I64Ty, SSTopPtr, Builder.getInt64(-1), "ss_curr_ptr");
  auto *ShadowRetVal = Builder.CreateLoad(I64Ty, SSCurPtr);
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

void SSFunctionPass::instrumentStores(Function &F) {
  SmallVector<StoreInst*, 8> Stores;
  for (auto &BB : F) {
    for (auto &I : BB) {
      if (auto *SI = dyn_cast<StoreInst>(&I)) {
          Stores.push_back(SI);
      }
    }
  }

  for (auto SI : Stores) {
    instrumentStore(F, *SI);
  }
}

void SSFunctionPass::instrumentStore(Function &F, StoreInst &I) {
  constexpr auto MASK = ~0x700000000000ULL;
  auto *DstPtr = I.getPointerOperand();
  
  IRBuilder<> Builder(&I);
  auto *DstI64 = Builder.CreatePtrToInt(DstPtr, Builder.getInt64Ty());
  auto *MaskedInt = Builder.CreateAnd(DstI64, Builder.getInt64(MASK));
  auto *MaskedPtr = Builder.CreateIntToPtr(MaskedInt, DstPtr->getType());
  I.setOperand(I.getPointerOperandIndex(), MaskedPtr);
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