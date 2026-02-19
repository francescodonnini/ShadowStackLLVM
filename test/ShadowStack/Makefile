all:
	mkdir -p build
	clang -O2 -S -emit-llvm main.c -o build/test.ll
	opt --load-pass-plugin /home/francesco/Documents/Università/TESI/ShadowStackLLVM/ShadowStack/build/ShadowStack.so \
    -passes="shadow-stack" \
    -print-changed \
    -S build/test.ll -o build/test_instrumented.ll
	llc -O2 --x86-asm-syntax=intel build/test_instrumented.ll -o build/test.S
	clang -g build/test.S -o build/main.exe

mt:
	mkdir -p build
	clang -O2 -S -pthread -emit-llvm mt_main.c -o build/mt_test.ll
	opt --load-pass-plugin /home/francesco/Documents/Università/TESI/ShadowStackLLVM/ShadowStack/build/ShadowStack.so \
    -passes="shadow-stack" \
    -print-changed \
    -S build/mt_test.ll -o build/mt_test_instrumented.ll
	llc -O2 --x86-asm-syntax=intel build/mt_test_instrumented.ll -o build/mt_test.S
	clang -O2 -g -v -pthread build/mt_test.S -o build/mt_main.exe

clean:
	rm -rf build

inst:
	clang -O2 -S -emit-llvm main.c -o test.ll
	opt --load-pass-plugin /home/francesco/Documents/Università/TESI/ShadowStackLLVM/ShadowStack/build/ShadowStack.so \
    -passes="shadow-stack" \
    -print-changed \
    -S test.ll -o test_instrumented.ll
	llc -O2 --x86-asm-syntax=intel test_instrumented.ll -o test.s

run:
	env LD_PRELOAD=/home/francesco/Documents/Università/TESI/ShadowStackLLVM/SSRuntime/build/libruntime.so ./build/main.exe

mt_run:
	env LD_PRELOAD=/home/francesco/Documents/Università/TESI/ShadowStackLLVM/SSRuntime/build/libruntime.so ./build/mt_main.exe

O2:
	clang -O2 -S -emit-llvm main.c -o test.ll
	opt --load-pass-plugin /home/francesco/Documents/Università/TESI/ShadowStackLLVM/ShadowStack/build/ShadowStack.so \
    -passes="default<O2>" \
	-verify-each \
    -S test.ll \
    -o test_instrumented.ll