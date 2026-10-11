# Convenient CMake forwarding wrapper

all:
	@cmake -B build
	@cmake --build build -j$(shell nproc 2>/dev/null || echo 4)
	@cp -f build/vgmstudio . 2>/dev/null || true

windows:
	@cmake -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/x86_64-w64-mingw32.cmake
	@cmake --build build-win -j$(shell nproc 2>/dev/null || echo 4)

clean:
	@rm -rf build build-win vgmstudio build_objs *.wav

.PHONY: all windows clean
