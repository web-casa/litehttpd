---
title: Introduction
description: What is LiteHTTPD and why use it
---

LiteHTTPD is an Apache .htaccess compatibility solution for [OpenLiteSpeed](https://openlitespeed.org/), consisting of the `litehttpd_htaccess.so` LSIAPI module and a patched OLS binary. Together they achieve 90%+ Apache compatibility with 80 supported directives.

## Why LiteHTTPD?

OpenLiteSpeed is fast, free, and lightweight -- but it does not natively process `.htaccess` directives like `Require`, `Header`, `RewriteRule`, or `ExpiresByType`. Applications like WordPress, Laravel, and Drupal rely heavily on `.htaccess` for security and routing.

LiteHTTPD bridges this gap:

| Feature | Apache httpd | Stock OLS | LiteHTTPD |
|---------|-------------|-----------|-----------|
| .htaccess compatibility | 100% | 44% | **90%+** |
| Static file throughput | 11K RPS | 23K RPS | **22K RPS** |
| .htaccess parsing overhead | -4.2% | -18.8% | **-0.7%** |
| Baseline memory | 969 MB | 663 MB | **676 MB** |

## How It Works

LiteHTTPD consists of the `litehttpd_htaccess.so` LSIAPI module and (in the Full edition) a patched OLS binary. The module hooks into OLS at two phases:

1. **URI_MAP** (request phase) -- processes ACL, authentication, redirects, rewrite rules, PHP config, and environment variables
2. **SEND_RESP_HEADER** (response phase) -- processes response headers, Expires/Cache-Control, Content-Type, and conditional blocks

The module reads `.htaccess` files from the document root up to the request path, merges directives with AllowOverride filtering, and executes them in Apache-compatible order.

## LiteHTTPD and LiteHTTPD-Thin

LiteHTTPD (Full) is the recommended installation. It includes the `litehttpd_htaccess.so` module plus 4 OLS patches that enable full RewriteRule execution, `php_value`/`php_flag` support, and engine-level `Options -Indexes`.

For environments where the OLS binary cannot be modified (Docker images, shared hosting), LiteHTTPD-Thin provides the module alone with 70+ directives but without RewriteRule execution or `php_value` support.

See [Editions](/getting-started/editions/) for a detailed feature comparison.

## Project

LiteHTTPD is a sub-project of [Web.Casa](https://web.casa), an AI-native open source server control panel.

Related project: [LLStack](https://llstack.com) -- a server management platform built on LiteHTTPD, providing a complete web hosting stack with OLS + .htaccess support out of the box.
