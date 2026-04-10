---
title: 简介
description: LiteHTTPD 是什么，为什么要使用它
---

LiteHTTPD 是 [OpenLiteSpeed](https://openlitespeed.org/) 的 Apache .htaccess 兼容方案，由 `litehttpd_htaccess.so` LSIAPI 模块和打补丁的 OLS 二进制文件组成。两者配合可实现 90% 以上的 Apache 兼容性，支持 80 条指令。

## 为什么选择 LiteHTTPD？

OpenLiteSpeed 快速、免费、轻量——但它原生不处理 `.htaccess` 中的 `Require`、`Header`、`RewriteRule` 或 `ExpiresByType` 等指令。WordPress、Laravel、Drupal 等应用大量依赖 `.htaccess` 实现安全和路由。

LiteHTTPD 填补了这一空白：

| 特性 | Apache httpd | 原生 OLS | LiteHTTPD |
|------|-------------|---------|-----------|
| .htaccess 兼容性 | 100% | 44% | **90%+** |
| 静态文件吞吐量 | 11K RPS | 23K RPS | **22K RPS** |
| .htaccess 解析开销 | -4.2% | -18.8% | **-0.7%** |
| 基线内存 | 969 MB | 663 MB | **676 MB** |

## 工作原理

LiteHTTPD 由 `litehttpd_htaccess.so` LSIAPI 模块和（Full 版中的）打补丁 OLS 二进制文件组成。模块在 OLS 的两个阶段挂钩：

1. **URI_MAP**（请求阶段）—— 处理 ACL、认证、重定向、重写规则、PHP 配置和环境变量
2. **SEND_RESP_HEADER**（响应阶段）—— 处理响应头、Expires/Cache-Control、Content-Type 和条件块

模块从文档根目录到请求路径逐级读取 `.htaccess` 文件，通过 AllowOverride 过滤合并指令，并按照与 Apache 兼容的顺序执行。

## LiteHTTPD 和 LiteHTTPD-Thin

LiteHTTPD（完整版）是推荐的安装方式。它包含 `litehttpd_htaccess.so` 模块以及 4 个 OLS 补丁，可实现完整的 RewriteRule 执行、`php_value`/`php_flag` 支持和引擎级 `Options -Indexes`。

对于无法修改 OLS 二进制文件的环境（Docker 镜像、共享主机），LiteHTTPD-Thin 仅提供模块本身，支持 70+ 指令，但不包含 RewriteRule 执行和 `php_value` 支持。

详见[版本对比](/zh/getting-started/editions/)。

## 项目

LiteHTTPD 是 [Web.Casa](https://web.casa) 的子项目，Web.Casa 是一个 AI Native 的开源服务器控制面板。

相关项目：[LLStack](https://llstack.com) —— 基于 LiteHTTPD 构建的服务器管理平台，提供开箱即用的 OLS + .htaccess 完整 Web 托管方案。
