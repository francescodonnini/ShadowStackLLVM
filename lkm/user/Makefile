all: raw_tests preload_test

raw_tests:
	mkdir -p build
	gcc test_raw_fork.c -o build/test_raw_fork
	gcc test_raw_multithread.c -o build/test_raw_multithread -lpthread

preload_test:
	mkdir -p build
	gcc test_ld_preload.c -o build/test_ld_preload -lpthread

run_preload: preload_test
	LD_PRELOAD=alt_runtime/build/libruntime.so ./build/test_ld_preload