.PHONY: build test clean

build:
	cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCUDDLEPANEL_ENABLE_UPX=ON
	cmake --build build --config Release
	test -x bin/server

test: build
	ctest --test-dir build --output-on-failure

clean:
	rm -rf build bin
