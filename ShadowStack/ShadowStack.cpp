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
  auto *Int64PtrTy = PointerType::getUnqual(Int64Ty);

  // Push return address at the top of the stack
  // 1. SSOPtr is the pointer to the shadow stack object stored in gs:0
  auto *SSOPtr = Builder.CreateIntToPtr(Builder.getInt64(0), GsPtrTy, "ss_obj_ptr");

  // 2. Load the address of the next shadow stack's slot stored in gs:0
  auto *SSObjAdr = Builder.CreateLoad(Int64Ty, SSOPtr, true, "ss_obj_adr");
  auto *SSObjPtr = Builder.CreateIntToPtr(SSObjAdr, Int64PtrTy, "ss_obj_ptr");
  auto *SSPTopVal = Builder.CreateLoad(Int64Ty, SSObjPtr, true, "ss_top");

  // 3. Store the return address to the top of the shadow stack
  // Read te retun address from the stack
  // WARNING: Intrinsic::returnaddress might reuse the register populated by the preamble instrumentation to
  // get the return address so it is useless in this scenario
  auto *FrameAddrFn = Intrinsic::getOrInsertDeclaration(F.getParent(), Intrinsic::frameaddress, {Int64PtrTy});
  auto *FramePtr = Builder.CreateCall(FrameAddrFn, {Builder.getInt32(0)});

  // On x86_64, return address is at [rbp + 8]
  auto *FramePtrAdr = Builder.CreatePtrToInt(FramePtr, Int64Ty);
  auto *RetAdrPtrVal = Builder.CreateAdd(FramePtrAdr, Builder.getInt64(8), "ss_ret_ptr");

  // Load the return address
  auto *RetAdrPtr = Builder.CreateIntToPtr(RetAdrPtrVal, Int64PtrTy);
  auto *RetAdrVal = Builder.CreateLoad(Int64Ty, RetAdrPtr);

  auto *SSPTopPtr = Builder.CreateIntToPtr(SSPTopVal, Int64PtrTy, "ss_top_ptr");
  Builder.CreateStore(RetAdrVal, SSPTopPtr, true);

  // 4. Advance the top of the shadow stack
  auto *NewSSPTopVal = Builder.CreateAdd(SSPTopVal, Builder.getInt64(8), "ss_push");

  // 5. Store the new shadow stack pointer in gs:0
  Builder.CreateStore(NewSSPTopVal, SSOPtr, true);
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
  auto *Int64PtrTy = PointerType::get(Int64Ty, 0);

  // 1. SSPVar is an HANDLE to gs:0
  auto *SSOPtr = Builder.CreateIntToPtr(Builder.getInt64(0), GsPtrTy);

  // 2. Load the current top of the shadow stack
  auto *SSObjAdr = Builder.CreateLoad(Int64Ty, SSOPtr, true, "ss_obj_adr");
  auto *SSObjPtr = Builder.CreateIntToPtr(SSObjAdr, Int64PtrTy, "ss_obj_ptr");
  auto *SSPTopVal = Builder.CreateLoad(Int64Ty, SSObjPtr, true, "ss_top");
  
  // 3. Decrement the current stack pointer and update the shadow stack pointer
  auto *NewSSPTopVal = Builder.CreateSub(SSPTopVal, Builder.getInt64(8), "ss_pop");
  Builder.CreateStore(NewSSPTopVal, SSOPtr, true);

  // 5. Read the return address from the top of the shadow stack
  auto *NewSSPTopPtr = Builder.CreateIntToPtr(NewSSPTopVal, Int64PtrTy);
  auto *ShadowRetVal = Builder.CreateLoad(Int64Ty, NewSSPTopPtr, true);

  // Read te retun address from the stack
  // WARNING: Intrinsic::returnaddress might reuse the register populated by the preamble instrumentation to
  // get the return address so it is useless in this scenario
  auto *FrameAddrFn = Intrinsic::getOrInsertDeclaration(I.getModule(), Intrinsic::frameaddress, {Int64PtrTy});
  auto *FramePtr = Builder.CreateCall(FrameAddrFn, {Builder.getInt32(0)});

  // On x86_64, return address is at [rbp + 8]
  auto *FramePtrAdr = Builder.CreatePtrToInt(FramePtr, Int64Ty);
  auto *RetAdrPtrVal = Builder.CreateAdd(FramePtrAdr, Builder.getInt64(8), "ss_ret_ptr");

  // Load the return address
  auto *RetAdrPtr = Builder.CreateIntToPtr(RetAdrPtrVal, Int64PtrTy);
  auto *RetAdrVal = Builder.CreateLoad(Int64Ty, RetAdrPtr);
  
  // Compare return address
  auto *Compare = Builder.CreateICmpNE(ShadowRetVal, RetAdrVal);

  auto *CheckBB = I.getParent();
  auto *RetBB = CheckBB->splitBasicBlock(&I, "ss_ret");
  CheckBB->getTerminator()->eraseFromParent();

  // Fail path
  auto *FailBB = BasicBlock::Create(F.getContext(), "ss_fail", &F);
  IRBuilder<> FailPathBuilder(FailBB);
  auto *Trap = Intrinsic::getOrInsertDeclaration(F.getParent(), Intrinsic::trap);
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