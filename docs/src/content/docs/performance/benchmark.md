---
title: Benchmark Results
description: Performance comparison across 4 web server engines
---


:::note
All benchmark data on this page was collected using LiteHTTPD (Full edition) with all four OLS patches applied.
:::

## Test Environment

| Item | Value |
|------|-------|
| VPS | 4x Linode g6-standard-4 (4 vCPU, 8 GB RAM) |
| OS | AlmaLinux 9.5 |
| PHP | 8.4.19 |
| Database | MariaDB 10.11.15 |
| WordPress | 6.9.4 + 19 plugins |
| Benchmark | `ab -n 5000 -c 50 -k`, localhost, 3 rounds median |

## Throughput (Requests/sec)

| Scenario | Apache 2.4 | LiteHTTPD | Stock OLS | LSWS 6.3.5 |
|----------|-----------|-----------|-----------|------------|
| Static (no .htaccess) | 11,082 | **22,104** | 23,242 | 24,786 |
| Static (4-line .htaccess) | 13,020 | **37,038** | 75,908 | 83,779 |
| Static (200-line .htaccess) | 10,618 | **21,960** | 18,883 | 20,306 |
| Options -Indexes | 12,046 | **26,518** | 51,101 | 61,770 |
| Require denied | 14,735 | **31,105** | 68,878 | 43,824 |
| Header set | 13,620 | **39,133** | 70,482 | 90,972 |
| RewriteRule [R=301] | 14,144 | **29,758** | 48,849 | 78,885 |

:::note
Stock OLS numbers for directive tests are inflated because it does not actually execute .htaccess ACL/Header directives -- requests pass through without security checks.
:::

## .htaccess Parsing Overhead

| Engine | No .htaccess | 200-line .htaccess | Overhead |
|--------|-------------|-------------------|----------|
| Apache | 11,082 | 10,618 | -4.2% |
| **LiteHTTPD** | **22,104** | **21,960** | **-0.7%** |
| Stock OLS | 23,242 | 18,883 | -18.8% |
| LSWS 6.3.5 | 24,786 | 20,306 | -18.1% |

## Resource Usage

| Metric | Apache | LiteHTTPD | Stock OLS | LSWS |
|--------|--------|-----------|-----------|------|
| Baseline memory | 969 MB | **676 MB** | 663 MB | 819 MB |
| Module overhead | -- | +13 MB | -- | +156 MB |
| Peak CPU (static) | 98.5% | **66.0%** | 48.1% | 38.8% |
| RPS/CPU% efficiency | 525 | **1,324** | 1,890 | 1,265 |
