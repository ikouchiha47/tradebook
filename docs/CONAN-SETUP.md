# Conan 2 setup — lob/ C++20 (validated)

## conanfile.txt (lob/conanfile.txt)
```ini
[requires]
zlib/1.3.1
[test_requires]
gtest/1.18.0
benchmark/1.7.1
[generators]
CMakeDeps
CMakeToolchain
[layout]
cmake_layout
```
- `benchmark` = Criterion equivalent (repetitions + ComputeStatistics for p50/p99).
- `test_requires` keeps gtest/benchmark out of propagated deps.

## C++20 setting (not in conanfile.txt)
```bash
conan profile detect --force
conan install . --output-folder=build --build=missing -s compiler.cppstd=20
```
Or edit `~/.conan2/profiles/default` → `compiler.cppstd=20`.

## CMake excerpt (lob/CMakeLists.txt)
```cmake
cmake_minimum_required(VERSION 3.23)
project(lob CXX)
set(CMAKE_CXX_STANDARD 20)
find_package(ZLIB REQUIRED)
add_executable(lob src/main.cpp)
target_link_libraries(lob PRIVATE ZLIB::ZLIB)
enable_testing()
find_package(GTest REQUIRED)
find_package(benchmark REQUIRED)
```

## Build
```bash
conan install . --output-folder=build --build=missing -s compiler.cppstd=20
cmake --preset conan-release
cmake --build build/Release
# pin: conan lock create . ; conan install --lockfile=conan.lock --lockfile-out=conan.lock
```

## Global JD verdict (outside India)
C++-first 7-8 (Jump C++/Rust dual but C++ preferred, HRT C++ required, CitSec/Optiver/IMC C++20/Tower/Flow C++, Jane OCaml). Rust-primary 0 globally at top tier. Same pattern as India. C++20 + Conan satisfies both.
