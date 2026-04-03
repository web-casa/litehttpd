---
title: Testing
description: Test infrastructure and how to run tests
---

## Test Suite Overview

| Suite | Count | Framework | Coverage |
|-------|-------|-----------|----------|
| Unit tests | 786 | GoogleTest | Parser, executor, dirwalker |
| Property tests | 124 | RapidCheck | Fuzzing, invariants |
| Compatibility tests | 52 | GoogleTest | Apache behavior matching |
| Confconv tests | 55 | GoogleTest | Config converter |
| **Total** | **1,036** | | |

## Running Tests

```bash
# All tests
ctest --test-dir build --output-on-failure -j$(nproc)

# Specific suite
ctest --test-dir build -R "unit_tests" --output-on-failure
ctest --test-dir build -R "property_tests" --output-on-failure
ctest --test-dir build -R "compat_tests" --output-on-failure
ctest --test-dir build -R "confconv_tests" --output-on-failure
```

## Consistency Check

Validates all 80 directive types are implemented across parser, printer, executor, dirwalker, free, and generator:

```bash
bash tests/check_consistency.sh
```

## Fuzzing

```bash
# Build parser fuzzer with libFuzzer
clang -g -O1 -fsanitize=fuzzer,address -I include \
  -DHTACCESS_UNIT_TEST \
  tests/fuzz/fuzz_parser.cpp \
  src/htaccess_parser.c src/htaccess_directive.c src/htaccess_expires.c \
  -lstdc++ -lm -o fuzz_parser

# Run for 5 minutes
mkdir -p fuzz_corpus && cp tests/fuzz/corpus/* fuzz_corpus/
timeout 300 ./fuzz_parser fuzz_corpus/ -max_len=4096 -timeout=5
```

## End-to-End Tests

```bash
# Docker-based OLS directive tests
docker build -t ols-e2e -f tests/e2e/Dockerfile .
docker run -d --name ols-e2e -p 8088:8088 ols-e2e
bash tests/e2e/test_directives.sh

# Integration tests (WordPress, Laravel, Nextcloud, Drupal)
docker compose -f integration-tests/docker-compose.yml up -d --build
bash integration-tests/apps/wordpress/verify.sh
```

## CI Pipeline

- **PR gate** (ci.yml): build, unit/property/compat tests, ASan/UBSan, Apache comparison, OLS E2E
- **Nightly** (nightly.yml): fuzzing, smoke tests, integration tests (4 PHP apps)
- **Release** (release.yml): multi-platform build (x86_64 + ARM64), RPM packaging
