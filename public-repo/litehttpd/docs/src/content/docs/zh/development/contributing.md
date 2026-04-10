---
title: 贡献指南
description: 如何为 LiteHTTPD 做贡献
---

## 开发流程

1. Fork 仓库
2. 创建功能分支：`git checkout -b feature/my-change`
3. 进行修改并添加测试
4. 运行完整测试套件：`ctest --test-dir build --output-on-failure`
5. 运行一致性检查：`bash tests/check_consistency.sh`
6. 提交 Pull Request

## 添加新指令

1. 将指令类型添加到 `include/htaccess_directive.h`（枚举 + AllowOverride 类别）
2. 在 `src/htaccess_parser.c` 中添加解析逻辑
3. 在 `src/htaccess_printer.c` 中添加打印逻辑
4. 在 `src/htaccess_exec_*.c` 中创建或更新执行器
5. 在 `src/mod_htaccess.c` 中添加执行分发
6. 在 `src/htaccess_dirwalker.c` 中添加 AllowOverride 过滤
7. 在 `src/htaccess_directive.c` 中添加释放逻辑
8. 在 `tests/unit/` 中添加单元测试
9. 更新 `tests/check_consistency.sh` 中的预期计数
10. 更新 README.md 指令列表

## 代码风格

- 模块代码使用 C11，测试使用 C++17
- 4 空格缩进
- 函数和变量使用 `snake_case`
- 常量和宏使用 `UPPER_CASE`
- 热路径中不使用动态内存分配（使用线程本地缓冲区）
- 所有公共函数使用 `htaccess_`、`exec_` 或 `lsi_` 前缀

## 许可证

LiteHTTPD 采用 GPLv3 许可证，与 OpenLiteSpeed 保持一致。
