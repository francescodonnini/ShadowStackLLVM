# CMake Directives
When writing `CMakeLists.txt` for a new library, you shouldn't use the normal `add_library_directive` that appears in normal `CMakeLists.txt` files, as follows:

```{CMakeLists.txt}
add_library(MyLib SHARED
  MyLib.cpp)
```

Therefore, you should always use the `add_llvm_component_library` CMake function shown here:

```{CMakeLists.txt}
add_llvm_component_library(MyLib
    MyLib.cpp)
```

By using `LINK_COMPONENTS` argument in `add_llvm_component_library` you can designate the target's linked componenents

```{CMakeLists.txt}
add_llvm_component_library(MyLib
    MyLib.cpp
    LINK_COMPONENTS
    SomeComponent)
```

or alternatively

```{CMakeLists.txt}
set(LLVM_LINK_COMPONENTS SomeComponent)
```

To link my LLVM component library to a non-LLVM one LINK_LIBS argument can be used, for example:

```{CMakeLists.txt}
add_llvm_component_library(MyLib
  MyLib.cpp
  LINK_LIBS
  SomeLibrary)
```

LLVM uses a tool called TableGen to automatically write C++ code during the build process. It takes description files (.td) and turns them into C++ headers (`.h`) files.
If a library (e.g. MyLib) tries to include a header file that hasn't been generated yet, the build will fail with a `"File not found"` error.
`DEPENDS` forces CMake to wait for the needed files to be generated.

```{CMakeLists.txt}
add_llvm_component_library(MyLib
  MyLib.cpp
  DEPENDS
  intrinsics_gen)
```

Similar to `add_llvm_component_library`, to add a new executable target, we can
use `add_llvm_executable` or `add_llvm_tool`:

```{CMakeLists.txt}
add_llvm_tool(MyLib
  Tool.cpp)
```
**WARNING** `DEPENDS` argument can be used here as well but you can only use the `LLVM_LINK_COMPONENTS` variable to designate components to link.
In previous versions of LLVM, it was needed `add_llvm_library` to develop a Pass plugin, now it can be used (`LINK_COMPONENTS`, `LINK_LIBS`, and `DEPENDS` arguments are also available
here, with the same usages and functionalities as in `add_llvm_component_library`):

```{CMakeLists.txt}
add_llvm_pass_plugin(ShadowStackLLVM
   ShadowStack.cpp)
```

# CMake integration for out-of-tree projects
There are two ways to configure out-of-tree projects to link against LLVM:
- Using the `llvm-config` tool
- Using LLVM's CMake modules (it creates more concise and readable CMake scripts, which is preferable for projects that are already using CMake).

1. CMakeLists.txt skeleton:
```{CMakeLists.txt}
project(ShadowStackLLVM)

set(SOURCE_FILES
    ShadowStack.cpp)

add_executable(magic-cli
    ${SOURCE_FILES})
```
2. `include path` and `library path` resolution
```{CMakeLists.txt}
find_package(LLVM REQUIRED CONFIG)
include_directories(${LLVM_INCLUDE_DIRS})
link_directories(${LLVM_LIBRARY_DIRS})
```
To make `find_package` work, it is needed to supply the CMake variable `LLVM_DIR` while
invoking the CMake command
```sh
cmake -DLLVM_DIR="LLVM install path"/lib/cmake/llvm
```
3. Link main executable against LLVM's libraries. `add_llvm_executable` is really useful here, but CMake needs to be able to **find** those functions.
```{CMakeLists.txt}
list(APPEND CMAKE_MODULE_PATH ${LLVM_CMAKE_DIR})
include(AddLLVM)
```
`AddLLVM` contains those functions.
4. Add executable build target
```{CMakeLists.txt}
include(AddLLVM)
set(LLVM_LINK_COMPONENTS
    Support
    Analysis)
add_llvm_executable(magic-cli
    main.cpp)
```
Adding the library target makes no difference
```{CMakeLists.txt}
include(AddLLVM)
add_llvm_library(SomeLibrary
    lib.cpp
    LINK_COMPONENTS
    Support Analysis)
```
5. Add the LLVM pass plugin
```{CMakeLists.txt}
include(AddLLVM)
add_llvm_pass_plugin(ShadowStackLLVM
    ShadowStack.cpp)
```
6. LLVM-specific definitions and RTTI setting
```{CMakeLists.txt}
add_definitions(${LLVM_DEFINITIONS})
if (NOT ${LLVM_DEFINITIONS})
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti")
endif()
```

# TESTING
**TODO.**

# `PassManager` and `AnalysisManager`
## `ninja opt`
`opt` is a command line utility to test **Passes.** It can be used to run passes to an LLVM IR representation
```sh
opt --load-pass-plugin="Plugin.so" \
    --passes="function(?)" \
    -S -o - test.ll
```

## Registering a Pass into the pipeline
We create a special global function called `llvmGetPassPluginInfo` with an outline like this:
```c
extern "C" ::llvm::PassPluginLibraryInfo LLVM_ATTRIBUTE_
WEAK
llvmGetPassPluginInfo() {
  return {
    LLVM_PLUGIN_API_VERSION, "PassName", "Version",
    [](PassBuilder &PB) { /* ... */ }
  };
}
```
This function returns a `PassPluginLibraryInfo` instance, which contains various pieces of information such as the plugin API version and the Pass name. One of its most important fields is a lambda function that takes a single `PassBuilder&` argument. This function insert the Pass into a proper position within the pipeline.
- It configures the pipeline according to the optimization level
- It inserts the Pass into some of the places in the pipeline
- It allows the developer to specify a textual description of the pipeline they want to run by using `--passes` argument on `opt`. For example, the following command will run `InstCombine`,`PromoteMemToReg` and `SROA` in sequential order:
```sh
opt --passes="instcombine,mem2reg,sroa" test.ll -S -o -
```

```sh
opt --passes="PassName" test.ll -S -o –
```
```c
PB.registerPipelineParsingCallback(
  [](StringRef Name, FunctionPassManager &FPM, ArrayRef<PipelineElement>){
    if (Name == "PassName") {
      FPM.addPass(Pass());
      return true;
    }
    return false;
  });
```
```sh
opt -O2 --enable-new-pm \
    --load-pass-plugin=Pugin.so test.ll -S -o –
```
```c
PB.registerPipelineStartEPCallback(
  [](ModulePassManager &MPM, OptimizationLevel OL) {
    if (OL.getSpeedupLevel() >= 2)
      // Since `PassBuilder::registerPipelineStartEPCallback`
      // only accept ModulePass, we need an adapter to make
      // it work.
      MPM.addPass(createModuleToFunctionPassAdaptor(Pass()));
  });
```
`registerPipelineStartEPCallback` registers a callback that can customize certain places in the Pass pipeline, called **extension points (EPs).** 

## Writing a Pass
A **Pass in LLVM** is the basic unit that is required to perform certain actions against LLVM IR. LLVM consists of multiple Passes that are executed in sequential order, called the **Pass pipeline.** There are two kinds of Passes: IR Passes that transforms an LLVM IR block to another, and Machine IR Passes which produce machine code targeting a specific architecture. The Pass pipeline is managed by a **PassManager** that owns the plan - their execution order - to run the Passes (**PassManager** and **Pass pipeline** are used interchangeably). Code trasformations can be complex, indeed multiple transformation Passes might need the same set of program information, which is called **analysis** in LLVM. LLVM caches the analysis data so that it can be reused. A Pass can change the IR and as a result of this the cached data might become obsolete. LLVM provides an **AnalysisManager** to manage the analysis data.

**LLVM IR** is a target-independent **intermediate representation** for program analysis and compiler transformation (another way of seeing it is that LLVM IR is an alternative form of the code to optmize e compile).

**TODO: LLVM DESCRIBES THE PROGRAM IN A DIFFERENT WAY (HOW)?**

LLVM IR is organized in a hierarchical fashion, the levels in this hierarchy - from the top - are **Module, function, basic block** and **instruction.** A **module** represents a translation unit - usually a source file. Each module can contain multiple **functions** (or global variables). Each function contains a list of **basic blocks** where each basic block contains a list of **instructions.** A basic block is a list of instructions with only one entry point and one exit point. In other words, if a basic block is executed, the control flow is guaranteed to walk through every instruction in the block. Nearly every component in LLVM IR has a C++ class counterpart with the same name (see `Function` or `BasicBlock`). LLVM provides other data structures to view the relationships of different IR units (e.g. two basic blocks or two functions):
- **Control Flow Graph (CFG):** a graph structure organized into basic blocks to show their control flow relations. The vertices in the graph represent basic blocks, while the edges represent single control flow transfer.
- **Loop:** consists of multiple basic blocks that have at least one back edge.
- **Call graph:** a graph showing control flow transfers where the vertices become individual functions and the edges become function call relations.
In LLVM, a **value** represents:
- values stores in variabled
- constants
- global variables
- individual instructions
- basic blocks
**Single Static Assignment (SSA)** is a way of structuring and designing IR to make program analysis and compiler transformation easier to perform. In SSA, a variable (in the IR) will only be assigned a value exactly once. For example:
```c
x = 94;
y = 18
x = y + 18;
```
is not in SSA form, while the following
```c
x = 94;
y = x + 1;
z = x + 2;
```
is in SSA form (a value can be assigned only once but can be referenced multiple times). It is possible to rename variables in a non-SSA code block to transform it in SSA form:
```c
x0 = 94
y = 18
x1 = y + 18
```
Instructions in LLVM are organized in SSA form. Since each instruction in LLVM IR can only produce a single result values, an `Instruction` object also represents its **result value.** The concept of value in LLVM IR is represented by the C++ class `Value` (`Instruction`, `Constant`, `GlobalVariable` and `BasicBlock` are all child classes of `Value`). 

### Inserting a new Instruction
Most of the instruction classes in LLVM provide factory methods such as `BinaryOperator::Create()` to build a new instance. There are several ways to insert a new instruction into a `BasicBlock`:
- Factory methods in some instruction classes provide an option to insert the instruction right after it is created
- `Instruction` provides the methods `insertBefore/insertAfter` to insert a new instruction
- `IRBuilder` implements the builder design pattern that can insert new instructions one after another:
```c++
// BB is a basic block, the builder inserts the instructions it creates after the block
IRBuilder<> Builder(BB)
auto *AddI = Builder.CreateAdd(LHS, RHS);
// Builder inserts Ret after AddI
Builder.CreateRet(AddI)
```
## Diagnostic Tools
### Printing Pass pipeline details
There are many **optimization levels** in LLVM (e.g. `-O1`, `-O2` and `-Oz` flags). Each optimization level is running a different set of Passes and arraging them in different orders. Let's consider the following C program `test.c`
```c
int bar(int x) {
  int y = x;
  return y * 4;
}

int foo(int z) {
  return z + z * 2;
}
```
To generate the IR for it
```sh
$ clang -O0 -Xclang -disable-O0-optnone -emit-llvm -S test.c`
```
By default, `clang` will attach a special attribute, `optnone`, to each function under the `-O0` optimization level. This attribute will preven any further optimization on the attached functions. The flag `-disable-O0-optnone` is preventing `clang` from attaching this attribute.
Here, `--debug-pass-manager` causes `opt` to print out all of the Passes running under the optimization level `-O2`.
```sh
$ opt -O2 --disable-output --debug-pass-manager test.ll
```

### Printing changes to the IR after each Pass
```sh
$ opt -O2 --disable-output --print-changed ./test.ll
```
It is possible to filter the changes on the IR after specific Passes `--filter-passes=Pass[,Pass]`
and for specific functions `--filter-print-funcs=Function[,Function]`.

### Printing diagnostic messages
The most naive way to print out arbitrary messages to `stderr` is by using `errs()`.
```c++
errs() << "Found a multiplication with operands ";
LHS->printAsOperand(errs(), false);
errs() << " and ";
RHS->printAsOperand(errs(), false);
errs() << "\n";
```
`printAsOperand()` prints the textual representation of a `Value` to the given stream.
These messages are always printed, if we want to print certain messages only while debugging the program we need to use the utility `LLVM_DEBUG()` and `dbgs()` stream. `dbgs` and `errs` do the same thing so they can be used interchangeably (`dbgs` produces nicer error messages), `LLVM_DEBUG()` shows messages only when the `-debug` flag is provided to `opt` but it needs `DEBUG_TYPE` to be defined.

### `Expected` and `ErrorOr` pattern
**TODO.**

# Instrumentation
**Instrumentation** is a technique that inserts some probes into the code we are compiling in order to collect runtime information - e.g. how many times a function is called. Probes can be used to catch undesirable incidents that happened at runtime - e.g buffer overflows.