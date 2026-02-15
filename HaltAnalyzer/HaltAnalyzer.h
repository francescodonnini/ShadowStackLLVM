#ifndef STRICT_OPT_H
#define STRICT_OPT_H
#include "llvm/IR/PassManager.h"

namespace llvm {
class Function;

class HaltAnalyzer : public PassInfoMixin<HaltAnalyzer> {
    static constexpr const char *HaltFunName = "halt";

    SmallVector<Instruction*, 2> Calls;

    void findHaltCalls(Function &F);
public:
    PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);
};
}
#endif