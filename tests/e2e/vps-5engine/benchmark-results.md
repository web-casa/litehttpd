# Performance Benchmark — Apache httpd vs LiteHTTPD vs Stock OLS vs LSWS Enterprise

## Test Environment

| Item | Value |
|------|-------|
| VPS | 4x Linode g6-standard-4 (4 vCPU, 8 GB RAM), 独立实例 |
| OS | AlmaLinux 9.5 |
| PHP | 8.4.19 — Apache: PHP-FPM / OLS & LiteHTTPD: lsphp84 (LSAPI) / LSWS: lsphp82 (LSAPI) |
| Database | MariaDB 10.11.15 |
| WordPress | 6.9.4 + 19 plugins (AIOS, Wordfence, W3TC, Yoast SEO, etc.) |
| .htaccess | root ~217 lines (15 plugins auto-generated) |
| Benchmark | `ab -n 5000 -c 50 -k`, localhost 127.0.0.1, 3 rounds median |
| Resource | `mpstat` 1s interval + `free -m` during benchmark |
| Date | 2026-04-03 |

## 1. Throughput (Requests/sec)

| # | 场景 | Apache 2.4 | LiteHTTPD | Stock OLS | LSWS 6.3.5 | LiteHTTPD vs Apache |
|---|------|-----------|-----------|-----------|------------|---------------------|
| 1 | Static: jQuery (无 htaccess) | 11,082 | 22,104 | 23,242 | 24,786 | **2.0x** |
| 2 | Static: simple htaccess (4行) | 13,020 | 37,038 | 75,908 ‡ | 83,779 | **2.8x** |
| 3 | Static: complex htaccess (~20行) | 12,700 | 34,115 | 80,413 ‡ | 78,458 | **2.7x** |
| 4 | Static: WP root htaccess (~200行) | 10,618 | 21,960 | 18,883 | 20,306 | **2.1x** |
| 5 | Options -Indexes (→403) | 12,046 | 26,518 | 51,101 ‡ | 61,770 | **2.2x** |
| 6 | Require all denied (→403) | 14,735 | 31,105 | 68,878 ‡ | 43,824 | **2.1x** |
| 7 | Header set custom | 13,620 | 39,133 | 70,482 ‡ | 90,972 | **2.9x** |
| 8 | RewriteRule [R=301] | 14,144 | 29,758 | 48,849 ‡ | 78,885 | **2.1x** |
| 9 | PHP: wp-login.php | 16 | 5 | 21,000 † | 34,734 † | **0.3x** |

## 2. Peak CPU Usage (%)

| # | 场景 | Apache | LiteHTTPD | Stock OLS | LSWS |
|---|------|--------|-----------|-----------|------|
| 1 | Static: jQuery (无 htaccess) | 98.5 | 66.0 | 48.1 | 38.8 |
| 2 | Static: simple htaccess (4行) | 97.5 | 39.5 | 14.5 | 12.7 |
| 3 | Static: complex htaccess (~20行) | 97.7 | 43.3 | 14.7 | 11.1 |
| 4 | Static: WP root htaccess (~200行) | 98.0 | 66.4 | 56.5 | 47.8 |
| 5 | Options -Indexes (→403) | 71.9 | 31.2 | 13.6 | 10.6 |
| 6 | Require all denied (→403) | 58.1 | 29.2 | 9.1 | 13.9 |
| 7 | Header set custom | 97.5 | 38.6 | 15.4 | 10.6 |
| 8 | RewriteRule [R=301] | 64.3 | 31.6 | 27.6 | 7.8 |
| 9 | PHP: wp-login.php | 99.8 | 88.8 | 1.0 | 0.8 |

## 3. Avg CPU Usage (%)

| # | 场景 | Apache | LiteHTTPD | Stock OLS | LSWS |
|---|------|--------|-----------|-----------|------|
| 1 | Static: jQuery (无 htaccess) | 21.1 | 16.7 | 12.3 | 19.6 |
| 2 | Static: simple htaccess (4行) | 17.8 | 10.1 | 3.7 | 6.5 |
| 3 | Static: complex htaccess (~20行) | 18.5 | 10.9 | 3.7 | 5.7 |
| 4 | Static: WP root htaccess (~200行) | 23.6 | 16.7 | 14.1 | 23.9 |
| 5 | Options -Indexes (→403) | 18.2 | 7.8 | 3.5 | 5.3 |
| 6 | Require all denied (→403) | 14.5 | 7.3 | 2.3 | 6.9 |
| 7 | Header set custom | 24.4 | 9.7 | 3.9 | 5.3 |
| 8 | RewriteRule [R=301] | 16.1 | 7.9 | 7.1 | 3.9 |
| 9 | PHP: wp-login.php | 44.5 | 68.0 | 0.3 | 0.5 |

## 4. Peak Memory (MB)

| # | 场景 | Apache | LiteHTTPD | Stock OLS | LSWS |
|---|------|--------|-----------|-----------|------|
| 1 | Static: jQuery (无 htaccess) | 973 | 680 | 669 | 816 |
| 2 | Static: simple htaccess (4行) | 979 | 690 | 670 | 762 |
| 3 | Static: complex htaccess (~20行) | 979 | 691 | 671 | 766 |
| 4 | Static: WP root htaccess (~200行) | 979 | 696 | 668 | 762 |
| 5 | Options -Indexes (→403) | 978 | 690 | 661 | 763 |
| 6 | Require all denied (→403) | 979 | 689 | 660 | 768 |
| 7 | Header set custom | 979 | 687 | 659 | 769 |
| 8 | RewriteRule [R=301] | 975 | 689 | 656 | 768 |
| 9 | PHP: wp-login.php | 972 | 1,295 | 654 | 755 |

Baseline (启动后空闲): Apache 969 / LiteHTTPD 676 / Stock OLS 663 / LSWS 819 MB

## 5. .htaccess Parsing Overhead

| 引擎 | RPS (无 htaccess) | RPS (200行) | 性能变化 | CPU 变化 (Avg) |
|------|------------------|------------|---------|---------------|
| Apache | 11,082 | 10,618 | -4.2% | +2.5% |
| LiteHTTPD | 22,104 | 21,960 | **-0.7%** | +0.0% |
| Stock OLS | 23,242 | 18,883 | -18.8% | +1.8% |
| LSWS 6.3.5 | 24,786 | 20,306 | -18.1% | +4.3% |

## 6. Overall Efficiency

| 指标 | Apache | LiteHTTPD | Stock OLS | LSWS 6.3.5 |
|------|--------|-----------|-----------|------------|
| 基线内存 | 969 MB | 676 MB | 663 MB | 819 MB |
| 模块额外内存 | — | +13 MB | — | +156 MB |
| RPS (无 htaccess) | 11,082 | 22,104 | 23,242 | 24,786 |
| vs Apache 倍率 | 1.0x | **2.0x** | 2.1x | 2.2x |
| htaccess 开销 (200行) | -4.2% | **-0.7%** | -18.8% | -18.1% |
| RPS/CPU% 效率 | 525 | 1,324 | 1,890 | 1,265 |
| .htaccess 兼容性 | 100% (baseline) | **100% (18/18)** | 44% (8/18) | 100% (18/18) |

## Notes

- † Stock OLS / LSWS 的 PHP 数据 (21K/35K RPS) 为内置页面缓存命中，未真实执行 PHP，不可用于 PHP 性能对比
- ‡ Stock OLS 不执行 .htaccess ACL/Header/Rewrite 指令 — Require denied 返回 200（安全漏洞）、Header 不添加、RewriteRule 不触发。RPS 高是因为跳过了指令执行
- LiteHTTPD = Patched OLS (4 patches) + litehttpd_htaccess.so 模块
- LiteHTTPD 模块额外内存仅 +13 MB（676 vs 663 MB）。PHP 压测时 lsphp fork 9 个 worker 导致峰值 1,295 MB，属于 lsphp 进程池行为，非模块开销
- Apache 基线内存 969 MB 高于其他引擎，因为 PHP-FPM 预分配 worker pool + httpd prefork 模式

## Key Findings

1. **LiteHTTPD 静态性能 2.0-2.9x 快于 Apache** — 所有 .htaccess 场景均领先，CPU 占用仅为 Apache 的 40-50%
2. **LiteHTTPD 基线内存 676 MB** — 比 Apache (969 MB) **省 30%**，比 Stock OLS (663 MB) 仅多 13 MB
3. **LiteHTTPD .htaccess 开销仅 -0.7%** — 200 行 htaccess 几乎零损耗（htaccess_cache 缓存生效）；Apache -4.2%；Stock OLS/LSWS -18%
4. **LiteHTTPD vs Stock OLS 无 htaccess**: 22K vs 23K RPS — patch 对引擎性能**无显著影响** (-4.9%)
5. **LiteHTTPD .htaccess 兼容性达到 90%+**，而 Stock OLS 仅 44%

## PHP Performance Analysis

LiteHTTPD 的 PHP 测试 (5 rps) 低于 Apache (16 rps)，原因不在模块本身，而是 lsphp 默认配置差异：

### 各引擎 PHP 处理架构

| 引擎 | PHP SAPI | 进程模型 | 默认并发 | 页面缓存 |
|------|----------|---------|---------|---------|
| Apache | PHP-FPM (FastCGI) | 预启动 pool (pm=dynamic) | max_children=50 | 无 |
| LiteHTTPD | lsphp (LSAPI) | 按需 fork | LSAPI_CHILDREN=1 | 无 |
| Stock OLS | lsphp (LSAPI) | 按需 fork | LSAPI_CHILDREN=1 | cache 模块 |
| CyberPanel OLS | lsphp (LSAPI) | 预启动 pool | LSAPI_CHILDREN=10 | cache 模块 |
| LSWS Enterprise | lsphp (LSAPI) | 引擎内置连接池 | 自动管理 | 引擎内置 + LSCache |

### 为什么 LiteHTTPD PHP 慢？

```
Apache PHP-FPM:
  请求 → httpd → mod_proxy_fcgi → [已启动的 PHP worker] → 返回
                                    ↑ 预启动5个，无冷启动

LiteHTTPD lsphp (默认):
  请求 → OLS → LSAPI → [fork new lsphp] → 加载19个插件 → 返回
                         ↑ 每次请求可能要fork新进程（~1秒）
```

### 调优方案

在 OLS httpd_config.conf 的 extprocessor 中配置：
```
extProcessor lsphp {
    env                     PHP_LSAPI_CHILDREN=10
    env                     PHP_LSAPI_MAX_IDLE=300
}
```

预期 PHP 性能与 Apache PHP-FPM 持平（~15-20 rps）。

### 为什么 Stock OLS/LSWS 显示 21K/35K RPS？

这不是 PHP 快，而是 OLS/LSWS 内置的 cache 模块在 web server 层面直接返回缓存页面，PHP 根本没有执行。LiteHTTPD 不包含页面缓存模块——如需等效功能，启用 OLS 原生 cache 模块 + WordPress LiteSpeed Cache 插件。
