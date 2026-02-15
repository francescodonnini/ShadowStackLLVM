#ifndef STRICT_OPT_H
#define STRICT_OPT_H
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"

namespace llvm {
class Function;

struct SSFunctionPass : public PassInfoMixin<SSFunctionPass> {
    PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);
private:
    constexpr static auto GS_ADDR_SPACE = 256;

    void instrumentPreamble(Function &F);
    void instrumentEpilogue(Function &F);
    void instrumentRet(Function &F, ReturnInst &I);
};
}
#endif