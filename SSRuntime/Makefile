all:
	mkdir -p build
	clang -shared -fPIC allocator.c main.c -o build/libruntime.so

clean:
	rm -rf build