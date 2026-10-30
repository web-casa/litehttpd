---
title: 从源码构建
description: 如何从源码构建 LiteHTTPD 模块和工具
---

## 前提条件

| 依赖 | 最低版本 | 包名 |
|-----------|----------------|---------|
| CMake | 3.14 | `cmake` |
| GCC 或 Clang | 支持 C11 | `gcc` / `clang` |
| C++ 编译器 | C++17（仅测试需要） | `g++` / `clang++` |
| libcrypt | 任意 | `libcrypt-devel` / `libcrypt-dev` |

## 快速构建 (LiteHTTPD-Thin)

仅构建模块即可生成 LiteHTTPD-Thin -- 运行在原生 OLS 上的 `.so` 文件。要获得 LiteHTTPD-Full，还需要应用 OLS 补丁并重新构建 OLS 二进制文件（参见[补丁说明](/zh/development/patches/)），或安装预构建的 `openlitespeed-litehttpd` RPM。

```bash
# Release 构建（仅模块 -- 生成 Thin 版本）
cmake -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build -j$(nproc) --target litehttpd_htaccess

# 输出：build/litehttpd_htaccess.so
```

## 完整构建（模块 + 工具 + 测试）

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)

# 运行全部 1036 个测试
ctest --test-dir build --output-on-failure -j$(nproc)
```

## 构建目标

| 目标 | 输出 | 描述 |
|--------|--------|-------------|
| `litehttpd_htaccess` | `litehttpd_htaccess.so` | LSIAPI 模块 |
| `litehttpd-confconv` | `litehttpd-confconv` | 配置转换器 CLI |
| `unit_tests` | `tests/unit_tests` | 单元测试二进制文件 |
| `property_tests` | `tests/property_tests` | 基于属性的测试 |
| `compat_tests` | `tests/compat_tests` | 兼容性测试 |
| `confconv_tests` | `tests/confconv_tests` | 配置转换器测试 |

## 使用 Sanitizers 构建

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build -j$(nproc)
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build --output-on-failure
```

## 交叉编译 (ARM64)

```bash
cmake -B build-arm64 \
  -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF
cmake --build build-arm64 --target litehttpd_htaccess
```
