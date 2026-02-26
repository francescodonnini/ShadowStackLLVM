#include "ShadowStack.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/KnownBits.h"
#include "llvm/Support/raw_ostream.h"

#define DEBUG_TYPE "shadow-stack-opt"

using namespace llvm;

STATISTIC(NumStoresSeen, "Total number of stores evaluated");
STATISTIC(NumSkippedAlloca, "Number of masks skipped (Stack Object)");
STATISTIC(NumSkippedKnownBits, "Number of masks skipped (Known Bits)");
STATISTIC(NumSkippedSCEV, "Number of masks skipped (Scalar Evolution)");
STATISTIC(NumSkippedDomTree, "Number of masks skipped (Dominator Tree)");
STATISTIC(NumStoresInstrumented, "Number of masks actually injected");

PreservedAnalyses SSFunctionPass::run(Function &F, FunctionAnalysisManager &FAM) {
    if (F.isDeclaration()) {
      return PreservedAnalyses::all();
    }

    auto isLeaf = true;
    for (auto &BB : F) {
      for (auto &I : BB) {
        if (auto *CI = dyn_cast<CallInst>(&I)) {
          if (!CI->getIntrinsicID()) {
            isLeaf = false;
            break;
          }
        }
      }
      if (!isLeaf) break;
    }


    if (_instrumentStore) {
      auto &DT = FAM.getResult<DominatorTreeAnalysis>(F);
      auto &SE = FAM.getResult<ScalarEvolutionAnalysis>(F);
      instrumentStores(F, DT, SE);
    }

    if (_instrumentFun && !isLeaf) {
      instrumentPreamble(F);
      instrumentEpilogue(F);
    }

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

void SSFunctionPass::instrumentStores(Function &F, DominatorTree &DT, ScalarEvolution &SE) {  
  SmallVector<StoreInst*, 8> Stores;
  for (auto &BB : F) {
    for (auto &I : BB) {
      if (auto *SI = dyn_cast<StoreInst>(&I)) {
          Stores.push_back(SI);
      }
    }
  }

  auto &EntryBB = F.getEntryBlock();
  IRBuilder<> Builder(&EntryBB, EntryBB.getFirstNonPHIIt());
  auto Mask = Builder.getInt64(MASK);

  DenseMap<Value*, Value*> MaskedPtrs;
  for (auto SI : Stores) {
    instrumentStore(F, *SI, MASK, Mask, DT, SE, MaskedPtrs);
  }
}

void SSFunctionPass::instrumentStore(
  Function &F,
  StoreInst &I,
  uint64_t Mask,
  Value *MaskVal,
  DominatorTree &DT,
  ScalarEvolution &SE,
  DenseMap<Value*, Value*> &MaskedPtrs) {
  NumStoresSeen++;
#if defined(OPT_ALL) || defined(ALLOCA_OPT)
  if (allocaOpt(I)) return;
#endif

#if defined(OPT_ALL) || defined(KNOWNBITS_OPT)
  if (knownBitsOpt(F, I, Mask, DT, SE)) return;
#endif

#if defined(OPT_ALL) || defined(SCEV_OPT)
  if (scalarEvolutionOpt(I, Mask, DT, SE)) return;
#endif

#if defined(OPT_ALL) || defined(DOMTREE_OPT)
  if (domTreeOpt(I, DT, MaskedPtrs)) return;
#endif

  IRBuilder<> Builder(&I);
  auto *M = F.getParent();
  auto *I64Ty = Builder.getInt64Ty();
  auto *I64PtrTy = PointerType::getUnqual(I64Ty);
  auto *GsI64PtrTy = PointerType::get(I64Ty, GS_ADDR_SPACE);

  auto *DstPtr = I.getPointerOperand();
  auto *DstI64 = Builder.CreatePtrToInt(DstPtr, Builder.getInt64Ty());
  auto *MaskedInt = Builder.CreateAnd(DstI64, MaskVal, "masked_adr");
  auto *MaskedPtr = Builder.CreateIntToPtr(MaskedInt, DstPtr->getType());

  I.setOperand(I.getPointerOperandIndex(), MaskedPtr);
  
  MaskedPtrs[DstPtr] = MaskedPtr;
  
  NumStoresInstrumented++;
}

bool SSFunctionPass::allocaOpt(StoreInst &I) {
  auto *DstPtr = I.getPointerOperand();
  auto *RawPtr = getUnderlyingObject(DstPtr);
  if (isa<AllocaInst>(RawPtr)) {
    NumSkippedAlloca++;
    return true;
  }
  return false;
}

bool SSFunctionPass::knownBitsOpt(Function &F, StoreInst&I, uint64_t Mask, DominatorTree &DT, ScalarEvolution &SE) {
  auto *DstPtr = I.getPointerOperand();
  const auto &DL = F.getParent()->getDataLayout();
  auto Known = computeKnownBits(DstPtr, DL);
  auto BitsToClear = ~Mask;
  if ((Known.Zero.getZExtValue() & BitsToClear) == BitsToClear) {
    NumSkippedKnownBits++;
    return true;
  }
  return false;
}

bool SSFunctionPass::scalarEvolutionOpt(StoreInst &I, uint64_t Mask, DominatorTree &DT, ScalarEvolution &SE) {
  auto *DstPtr = I.getPointerOperand();
  const auto &SCEV = SE.getSCEV(DstPtr);
  auto PtrRange = SE.getUnsignedRange(SCEV);
  auto MaxValue = PtrRange.getUnsignedMax();
  APInt APMask(MaxValue.getBitWidth(), Mask);
  if ((MaxValue & ~APMask).isZero()) {
    NumSkippedSCEV++;
    return true;
  }
  return false;
}

bool SSFunctionPass::domTreeOpt(StoreInst &I, DominatorTree &DT, DenseMap<Value*, Value*> &MaskedPtrs) {
  auto *DstPtr = I.getPointerOperand();
  if (MaskedPtrs.count(DstPtr)) {
    auto *PrevMaskedPtr = MaskedPtrs[DstPtr];
    auto *PrevInst = dyn_cast<Instruction>(PrevMaskedPtr);
    if (!PrevInst ||DT.dominates(PrevInst, &I)) {
      I.setOperand(I.getPointerOperandIndex(), PrevMaskedPtr);
      NumSkippedDomTree++;
      return true;
    }
  }
  return false;
}

extern "C" ::llvm::PassPluginLibraryInfo LLVM_ATTRIBUTE_WEAK
llvmGetPassPluginInfo() {
  return {
    LLVM_PLUGIN_API_VERSION, "ShadowStack", "v0.1",
    [](PassBuilder &PB) {
      PB.registerPipelineStartEPCallback(
        [](ModulePassManager &MPM, OptimizationLevel OL) {
          MPM.addPass(createModuleToFunctionPassAdaptor(SSFunctionPass()));
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