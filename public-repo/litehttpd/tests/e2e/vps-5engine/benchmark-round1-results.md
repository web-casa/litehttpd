# Benchmark Results (Round 1) — 2026-04-03

**Note**: This round had inconsistent configs (different PHP versions, VPS specs).
Results are for reference only. Round 2 will use unified setup.

## Environment
- **LiteHTTPD + Apache**: Linode g6-standard-4 (4C/8G), PHP-FPM 8.0 + lsphp 8.3
- **Stock OLS / LSWS**: Linode g6-standard-2 (2C/4G), lsphp 8.3 / lsphp 8.2
- Tool: ab, 5000 reqs, 50 concurrent, keepalive, localhost

## Static File (Requests/sec)

| 场景 | Apache 2.4 (4C) | LiteHTTPD (4C) | Stock OLS (2C) | LSWS 6.3.5 (2C) |
|------|-----------------|----------------|----------------|------------------|
| jQuery (无 .htaccess) | 12,086 | 24,070 | 18,883 | 31,072 |
| Simple .htaccess (4行) | 13,078 | 25,235 | 55,172* | 57,329 |
| Complex .htaccess (~30行) | 12,134 | 24,280 | 53,666* | 46,721 |
| WP root .htaccess (~200行) | 8,843 | 24,529 | 50,609* | 70,498 |

\* Stock OLS 不处理 .htaccess 指令内容

## Directive Performance (同机 4C/8G)

| .htaccess 指令 | Apache | LiteHTTPD | 倍率 |
|---------------|--------|-----------|------|
| Options -Indexes (→403) | 10,294 | 32,522 | 3.2x |
| Header set 自定义头 | 11,313 | 33,625 | 3.0x |
| Require all denied (→403) | 12,343 | 28,988 | 2.3x |
| FilesMatch deny .bak (→403) | 11,735 | 28,145 | 2.4x |
| ExpiresByType + Cache-Control | 12,134 | 24,280 | 2.0x |
| RewriteRule [R=301] | 11,945 | 26,222 | 2.2x |

## PHP Dynamic (c=1, sequential)

| 场景 | Apache (PHP-FPM 8.0) | LiteHTTPD (lsphp 8.3) |
|------|---------------------|----------------------|
| WordPress 首页 | 1.62 rps | 0.98 rps |
| wp-login.php | 1.81 rps | 1.05 rps |
| WordPress 首页 (c=5) | 6.82 rps | 3.75 rps |

## Key Observation
- .htaccess 缓存机制有效: 200行 .htaccess 对 LiteHTTPD 仅 +1.9% 开销
- Apache 同场景 -27%
