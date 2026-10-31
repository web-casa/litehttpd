# RewriteRule 原生执行引擎（Native Rewrite Engine）

## Status
Implemented (v1). 1128/1128 tests green. Live-server verification pending.

## 动机
此前 RewriteRule/RewriteCond 仅解析（parse-only）：执行依赖打补丁的 OLS
（`lsiapi_has_rewrite_extensions()` ABI 探测），stock OLS 上直接跳过。这让
Thin 版（无补丁）无法支撑 WordPress 固定链接等真实场景——模块最大的单项
功能差距。

## 架构
`exec_rewrite_rules()` 的引擎选择：
1. 打补丁 OLS（Full 版）：原有 `lsiapi_*` 快路径优先；
2. stock OLS：回落到内置引擎 `rewrite_native_exec()`（本文件对应实现）。

全部正则经 P1 引擎（`htre_*`，PCRE2 三态、match_limit/depth_limit），
`HTRE_HIT_LIMIT` 中止整条链（对 ReDoS 形态 pattern 失败关闭）。
**tag 槽位**：regex cache 按 tag 隔离——redir=0/env=1/hdr=2/expr=3，
**rewrite=4**（`HTRE_MAX_TAGS=8`；曾用 12 导致静默编译失败，教训记录于此）。

## 支持范围（Apache 子集）
- `RewriteRule pattern substitution [flags]`
  - flags：`R[=301..308]` `L` `END` `NC` `QSA` `F`(403) `G`(410)；
    `NE/PT/CO` 等接受但不动作
  - pattern：PCRE2（`.` 含换行，Apache 语义）、前导 `!` 否定（否定匹配
    时无可用捕获组）
  - substitution：`$0..$9` 规则组、`%{VAR}`（expr 模块求值）、
    绝对 `http(s)://` = 外部重定向、`-` = 不替换、相对路径拼 `RewriteBase`
- `RewriteCond test cond [flags]`
  - flags：`NC` `OR`（OR 组语义：组内任一满足即过）
  - test：`%{VAR}` 展开 + 字面量
  - cond：PCRE2、`-f/-d/-s`（真实 stat，绝对或 docroot 相对）、
    `<` `>` `=` 字典序、前导 `!` 否定
- `RewriteBase`：相对替换的前缀
- QSA：外部重定向拼 `&orig`；内部重写拼进新 query

## 与 Apache 的文档化差异
- 规则按文件序**单遍**执行，`[L]/[END]` 停链；内部重写后**不**从头重扫
  （Apache per-dir 上下文会循环重扫）。覆盖主流形态（单一 catch-all 前端
  控制器，如 WordPress）；多轮重写链场景可能少一次迭代。
- RewriteCond 中的 `$N`（前一规则组）近似为空（首条规则前 Apache 语义
  本就未定义）。
- substitution 中 `%N`（条件组）v1 未填充（结构预留），Apache 场景少用。

## 文件
- `include/htaccess_rewrite_native.h` — 接口 + tag 槽位约定
- `src/htaccess_rewrite_native.c` — 引擎
- `src/htaccess_exec_rewrite.c` — 引擎选择回落点
- `tests/unit/test_exec_rewrite.cpp` — 19 项：WordPress 前端控制器、
  反向引用、301、F、QSA、-f 阻断、否定、无匹配不动
