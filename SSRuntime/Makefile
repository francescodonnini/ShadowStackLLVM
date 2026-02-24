all:
	mkdir -p build
	clang -shared -fPIC allocator.c mapping.c main.c -o build/libruntime.so

test:
	mkdir -p build
	clang -g mapping.c /home/francesco/Documents/Università/TESI/ShadowStackLLVM/SSRuntime/stckcpy.S -Wl,-e,relocate_stack -o build/test_mapping.exe	

clean:
	rm -rf build