# LiteHTTPD — OpenLiteSpeed 的 Apache .htaccess 兼容模块

[English](README.md)

> **RPM 仓库**: [rpms.litehttpd.com](https://rpms.litehttpd.com/) · **文档**: [docs.litehttpd.com](https://docs.litehttpd.com/) · **源代码**: [GitHub](https://github.com/web-casa/litehttpd)

为 OpenLiteSpeed 提供完整的 Apache .htaccess 兼容支持。80 种指令，100% WordPress 兼容，静态文件性能比 Apache 快 2.5 倍。

## 快速安装

```bash
# EL 8/9/10 — 一条命令
curl -s https://rpms.litehttpd.com/setup.sh | bash
dnf install openlitespeed-litehttpd
```

RPM 包含：打补丁的 OLS 二进制 + litehttpd 模块 + 自动配置。浏览安装包请访问 [rpms.litehttpd.com](https://rpms.litehttpd.com/)。

## 功能特性

- **80 种 .htaccess 指令** — Header、Redirect、RewriteRule/RewriteCond、SetEnv、Expires、php_value/php_flag、Options、Require、AuthType Basic、DirectoryIndex、FilesMatch 等
- **If/ElseIf/Else** 条件块 + 完整 **ap_expr** 表达式引擎
- **RewriteOptions** inherit/IgnoreInherit 和 **RewriteMap** txt/rnd/int
- **IPv6 CIDR** 访问控制，支持 `Require ip` 前缀匹配
- **WordPress 暴力破解防护**（8 种指令）
- **AllowOverride** 分类过滤（AuthConfig、FileInfo、Indexes、Limit、Options）
- **readApacheConf**：OLS 二进制内嵌 Apache 配置解析器（类似 CyberPanel/LSWS）
- **litehttpd-confconv**：Apache httpd.conf 到 OLS 配置转换器（65+ 指令）
- **热重载**：.htaccess 修改即时生效，无需重启
- **目录层叠**：逐目录 .htaccess，支持父子继承

## 版本对比

| | LiteHTTPD-Full | LiteHTTPD-Thin |
|-|----------------|---------------|
| **安装方式** | `dnf install openlitespeed-litehttpd` | 将 `.so` 复制到原版 OLS |
| **指令数** | 80（全部功能） | 70+（无 RewriteRule 执行、无 php_value） |
| **适用场景** | 生产环境、完整 Apache 迁移 | 快速评估、Docker |

## 性能

基准测试：Linode 4C/8G，AlmaLinux 9，WordPress，PHP 8.3

| 指标 | Apache 2.4 | OLS-Full | 原版 OLS |
|------|-----------|----------|----------|
| 静态文件 RPS | 23,909 | **58,891** | 63,140 |
| PHP RPS | 274 | **292** | 258 |
| .htaccess 功能 | 10/10 | **10/10** | 6/10 |
| 服务器 RSS | 618 MB | **449 MB** | 320 MB |

WordPress 插件兼容性：**9/9 与 Apache 结果一致**（Wordfence、Yoast SEO、W3 Total Cache、WP Super Cache、All In One Security、Redirection、WPS Hide Login、Disable XML-RPC、iThemes Security、HTTP Headers）。

## 与 CyberPanel .htaccess 模块对比

LiteHTTPD 支持 **80 种指令**，CyberPanel 约 29 种。CyberPanel 的所有功能均已覆盖，另外新增 27 项能力，包括 RewriteRule 执行、If/ElseIf/Else 条件块、ap_expr 引擎、Require 指令、AuthType Basic、Options、AllowOverride 和 readApacheConf。

## OLS 补丁

4 个补丁扩展 OLS 以实现完整功能：

| 补丁 | 功能 |
|------|------|
| 0001 | PHPConfig LSIAPI（php_value/php_flag 传递给 lsphp） |
| 0002 | RewriteRule 引擎（parse/exec/free） |
| 0003 | 内嵌 Apache 配置解析器（readApacheConf） |
| 0004 | Options -Indexes 返回 403 |

## 从源码构建

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
# 输出：build/litehttpd_htaccess.so + build/litehttpd-confconv
```

## 测试

1036 个测试，四个测试套件：

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure -j$(nproc)
```

## 项目

LiteHTTPD 是 [Web.Casa](https://web.casa) 的子项目，Web.Casa 是一个 AI 原生开源服务器控制面板。

相关项目：[LLStack](https://llstack.com) — 基于 LiteHTTPD 的服务器管理平台。

## 许可证

GNU General Public License v3.0 — 详见 [LICENSE](LICENSE)。
