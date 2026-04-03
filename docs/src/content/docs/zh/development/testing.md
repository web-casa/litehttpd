---
title: 测试
description: 测试基础设施及如何运行测试
---

## 测试套件概览

| 套件 | 数量 | 框架 | 覆盖范围 |
|-------|-------|-----------|----------|
| 单元测试 | 786 | GoogleTest | 解析器、执行器、目录遍历器 |
| 属性测试 | 124 | RapidCheck | 模糊测试、不变量 |
| 兼容性测试 | 52 | GoogleTest | Apache 行为匹配 |
| 配置转换器测试 | 55 | GoogleTest | 配置转换器 |
| **总计** | **1,036** | | |

## 运行测试

```bash
# 全部测试
ctest --test-dir build --output-on-failure -j$(nproc)

# 特定套件
ctest --test-dir build -R "unit_tests" --output-on-failure
ctest --test-dir build -R "property_tests" --output-on-failure
ctest --test-dir build -R "compat_tests" --output-on-failure
ctest --test-dir build -R "confconv_tests" --output-on-failure
```

## 一致性检查

验证全部 80 种指令类型在解析器、打印器、执行器、目录遍历器、释放和生成器中均有实现：

```bash
bash tests/check_consistency.sh
```

## 模糊测试

```bash
# 使用 libFuzzer 构建解析器模糊测试器
clang -g -O1 -fsanitize=fuzzer,address -I include \
  -DHTACCESS_UNIT_TEST \
  tests/fuzz/fuzz_parser.cpp \
  src/htaccess_parser.c src/htaccess_directive.c src/htaccess_expires.c \
  -lstdc++ -lm -o fuzz_parser

# 运行 5 分钟
mkdir -p fuzz_corpus && cp tests/fuzz/corpus/* fuzz_corpus/
timeout 300 ./fuzz_parser fuzz_corpus/ -max_len=4096 -timeout=5
```

## 端到端测试

```bash
# 基于 Docker 的 OLS 指令测试
docker build -t ols-e2e -f tests/e2e/Dockerfile .
docker run -d --name ols-e2e -p 8088:8088 ols-e2e
bash tests/e2e/test_directives.sh

# 集成测试（WordPress、Laravel、Nextcloud、Drupal）
docker compose -f integration-tests/docker-compose.yml up -d --build
bash integration-tests/apps/wordpress/verify.sh
```

## CI 流水线

- **PR 门禁** (ci.yml)：构建、单元/属性/兼容性测试、ASan/UBSan、Apache 对比、OLS E2E
- **每夜构建** (nightly.yml)：模糊测试、冒烟测试、集成测试（4 个 PHP 应用）
- **发布** (release.yml)：多平台构建（x86_64 + ARM64）、RPM 打包
