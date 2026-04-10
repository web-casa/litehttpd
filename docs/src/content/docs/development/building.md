---
title: Building from Source
description: How to build LiteHTTPD module and tools from source
---

## Prerequisites

| Dependency | Minimum Version | Package |
|-----------|----------------|---------|
| CMake | 3.14 | `cmake` |
| GCC or Clang | C11 support | `gcc` / `clang` |
| C++ compiler | C++17 (tests only) | `g++` / `clang++` |
| libcrypt | any | `libcrypt-devel` / `libcrypt-dev` |

## Quick Build (LiteHTTPD-Thin)

Building just the module produces LiteHTTPD-Thin -- the `.so` file that runs on stock OLS. For LiteHTTPD-Full, you also need to apply the OLS patches and rebuild the OLS binary (see [Patches](/development/patches/)), or install the pre-built `openlitespeed-litehttpd` RPM.

```bash
# Release build (module only -- produces Thin edition)
cmake -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build -j$(nproc) --target litehttpd_htaccess

# Output: build/litehttpd_htaccess.so
```

## Full Build (module + tools + tests)

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)

# Run all 1036 tests
ctest --test-dir build --output-on-failure -j$(nproc)
```

## Build Targets

| Target | Output | Description |
|--------|--------|-------------|
| `litehttpd_htaccess` | `litehttpd_htaccess.so` | LSIAPI module |
| `litehttpd-confconv` | `litehttpd-confconv` | Config converter CLI |
| `unit_tests` | `tests/unit_tests` | Unit test binary |
| `property_tests` | `tests/property_tests` | Property-based tests |
| `compat_tests` | `tests/compat_tests` | Compatibility tests |
| `confconv_tests` | `tests/confconv_tests` | Config converter tests |

## Build with Sanitizers

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build -j$(nproc)
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build --output-on-failure
```

## Cross-compilation (ARM64)

```bash
cmake -B build-arm64 \
  -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF
cmake --build build-arm64 --target litehttpd_htaccess
```
