LLVM_CONFIG    = /home/ubuntu/Documents/llvm-project/build/bin/llvm-config
LLVM_BUILD_DIR = $(shell $(LLVM_CONFIG) --obj-root)
LLVM_BIN       = $(LLVM_BUILD_DIR)/bin
LLVM_CMAKE_DIR = $(LLVM_BUILD_DIR)/lib/cmake/llvm
CC             = $(LLVM_BIN)/clang
CXX            = $(LLVM_BIN)/clang++
OPT            = $(LLVM_BIN)/opt
LLC            = $(LLVM_BIN)/llc
LLINK          = $(LLVM_BIN)/llvm-link

PROJECT_ROOT   = /home/ubuntu/Documents/ShadowStackLLVM

OML            = -O3

all:
	mkdir -p build
	cd build && cmake -DLLVM_DIR=$(LLVM_CMAKE_DIR) \
		-DCMAKE_CXX_COMPILER=$(CXX) \
		-DCMAKE_C_COMPILER=$(CC) \
		..
	$(MAKE) -C build

clean:
	rm -rf build