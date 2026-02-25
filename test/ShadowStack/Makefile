all:
	mkdir -p build
	clang -O2 -S -emit-llvm main.c -o build/test.ll
	opt --load-pass-plugin /home/francesco/Documents/Università/TESI/ShadowStackLLVM/ShadowStack/build/ShadowStack.so \
    -passes="shadow-stack" \
    -print-changed \
    -S build/test.ll -o build/test_instrumented.ll
	llc -O2 --x86-asm-syntax=intel build/test_instrumented.ll -o build/test_instrumented.S
	clang -g build/test_instrumented.S /home/francesco/Documents/Università/TESI/ShadowStackLLVM/SSRuntime/stckcpy.S -Wl,-e,relocate_stack -o build/test_instrumented.exe	

mask:
	mkdir -p build
	clang -O2 -S -emit-llvm mask_test.c -o build/mask_test.ll
	opt --load-pass-plugin /home/francesco/Documents/Università/TESI/ShadowStackLLVM/ShadowStack/build/ShadowStack.so \
    -passes="shadow-stack" \
    -print-changed \
    -S build/mask_test.ll -o build/mask_test_instrumented.ll
	llc -O2 --x86-asm-syntax=intel build/mask_test_instrumented.ll -o build/mask_test_instrumented.S
	clang -g build/mask_test_instrumented.S /home/francesco/Documents/Università/TESI/ShadowStackLLVM/SSRuntime/stckcpy.S -Wl,-e,relocate_stack -o build/mask_test_instrumented.exe

mask_run:
	env LD_PRELOAD=/home/francesco/Documents/Università/TESI/ShadowStackLLVM/SSRuntime/build/libruntime.so ./build/mask_test_instrumented.exe

run:
	env LD_PRELOAD=/home/francesco/Documents/Università/TESI/ShadowStackLLVM/SSRuntime/build/libruntime.so ./build/test_instrumented.exe

mt:
	mkdir -p build
	clang -O2 -S -pthread -emit-llvm mt_main.c -o build/mt_test.ll
	opt --load-pass-plugin /home/francesco/Documents/Università/TESI/ShadowStackLLVM/ShadowStack/build/ShadowStack.so \
    -passes="shadow-stack" \
    -print-changed \
    -S build/mt_test.ll -o build/mt_test_instrumented.ll
	llc -O2 --x86-asm-syntax=intel build/mt_test_instrumented.ll -o build/mt_test.S
	clang -O2 -g -v -pthread build/mt_test.S -o build/mt_main.exe

mt_run:
	env LD_PRELOAD=/home/francesco/Documents/Università/TESI/ShadowStackLLVM/SSRuntime/build/libruntime.so ./build/mt_main.exe

clean:
	rm -rf build