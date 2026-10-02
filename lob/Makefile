# lob/ Makefile — single entry for Conan + CMake + replay + bench
# Usage: make setup && make build && make test && make replay LIMIT=1000000
# Toolchain: Apple Clang 17 (system) has full C++20, partial C++23. P1 pins C++20.
# CMakeLists must set CMAKE_CXX_STANDARD 20; this guard fails fast otherwise.

BUILD_DIR ?= build
PRESET ?= conan-release
# NOTE: cmake_layout + --output-folder=build nests binaries at build/build/Release.
BINDIR ?= $(BUILD_DIR)/build/Release
BUILD_TYPE ?= Release
CONAN_STD ?= compiler.cppstd=20
CXX_STD ?= c++20

.PHONY: setup build test bench replay perf flame clean lock help check-cxx

help:
	@echo "setup build test bench replay perf flame clean lock"

setup:
	conan install . --output-folder=$(BUILD_DIR) --build=missing -s $(CONAN_STD)

check-cxx:
	@echo "Checking $(CXX) for -std=$(CXX_STD)..."
	@echo 'int main(){}' | $$(CXX="$(CXX)" sh -c 'echo $${CXX:-clang++}') -std=$(CXX_STD) -x c++ - -o /tmp/cxx_check 2>&1 || (echo "FAIL: no -std=$(CXX_STD). System Apple Clang 17 supports C++20; for newer use: brew install llvm" && exit 1)
	@echo "OK: -std=$(CXX_STD) supported ($$(clang++ --version | head -1))"

build: check-cxx setup
	cmake --preset $(PRESET)
	cmake --build $(BINDIR) -j

test: build
	ctest --preset $(PRESET) --output-on-failure

bench: build
	./$(BINDIR)/lob_bench --benchmark_repetitions=5 --benchmark_report_aggregates_only=true

replay: build
	./$(BINDIR)/lob-replay --limit $${LIMIT:-1000000} --stats-every $${STATS:-100000} $${ARGS}

perf: build
	perf stat -e cycles,instructions,cache-misses,branch-misses ./$(BINDIR)/lob-replay --limit $${LIMIT:-1000000}

flame: build
	cargo flamegraph --bin lob-replay -- --limit $${LIMIT:-500000} || echo "install cargo-flamegraph or use perf record"

lock:
	conan lock create . --lockfile-out=conan.lock
	conan install --lockfile=conan.lock --lockfile-out=conan.lock --output-folder=$(BUILD_DIR) --build=missing -s $(CONAN_STD)

clean:
	rm -rf $(BUILD_DIR)
