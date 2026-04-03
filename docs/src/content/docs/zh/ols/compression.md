---
title: "压缩"
description: "在 OpenLiteSpeed 上配置 Gzip 和 Brotli 压缩"
---

## 概述

OLS 支持 **Gzip** 和 **Brotli** 两种 HTTP 响应压缩方式。压缩可减少带宽使用并提升页面加载速度，尤其对 HTML、CSS、JavaScript 和 JSON 等文本类内容效果显著。

## 启用压缩

在 `httpd_config.conf` 的 `tuning` 部分配置压缩：

```apacheconf
tuning {
  enableGzipCompress      1
  enableBrotliCompress    1
  enableDynGzipCompress   1
  gzipCompressLevel       6
  brotliCompressLevel     5
  compressibleTypes       default
  gzipAutoUpdateStatic    1
  gzipStaticCompressLevel 6
  gzipMaxFileSize         10M
  gzipMinFileSize         300
}
```

### 参数说明

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `enableGzipCompress` | 1 | 为静态文件启用 Gzip |
| `enableBrotliCompress` | 1 | 启用 Brotli 压缩 |
| `enableDynGzipCompress` | 1 | 为动态（PHP）响应启用 Gzip |
| `gzipCompressLevel` | 6 | 动态内容的 Gzip 级别（1-9） |
| `brotliCompressLevel` | 5 | 动态内容的 Brotli 级别（0-11） |
| `compressibleTypes` | default | 要压缩的 MIME 类型 |
| `gzipAutoUpdateStatic` | 1 | 自动为静态资源创建 `.gz` 文件 |
| `gzipStaticCompressLevel` | 6 | 预压缩静态文件的 Gzip 级别 |
| `gzipMaxFileSize` | 10M | 压缩的最大文件大小 |
| `gzipMinFileSize` | 300 | 压缩的最小文件大小（跳过过小文件） |

## 压缩级别

### Gzip（1-9）

| 级别 | CPU 使用 | 压缩比 | 推荐场景 |
|------|----------|--------|----------|
| 1 | 极低 | 约 60% | CPU 紧张的高流量站点 |
| 4-6 | 中等 | 约 75-80% | 通用场景（推荐） |
| 9 | 高 | 约 82% | 仅用于静态预压缩 |

### Brotli（0-11）

Brotli 在相似的 CPU 开销下提供比 Gzip 更好的压缩比。但级别高于 6 时 CPU 消耗显著增加，仅适合静态预压缩。

| 级别 | CPU 使用 | 压缩比 | 推荐场景 |
|------|----------|--------|----------|
| 1-4 | 低 | 优于 Gzip 6 | 动态内容 |
| 5-6 | 中等 | 显著优于 Gzip | 通用场景（推荐） |
| 7-11 | 极高 | 最大压缩 | 仅用于静态预压缩 |

## 可压缩 MIME 类型

`compressibleTypes` 的 `default` 值涵盖常见的文本类型。自定义配置：

```apacheconf
tuning {
  compressibleTypes       text/*, application/javascript, application/json, \
                          application/xml, application/xhtml+xml, \
                          application/rss+xml, application/atom+xml, \
                          application/x-javascript, application/x-httpd-php, \
                          application/x-font-ttf, application/vnd.ms-fontobject, \
                          image/svg+xml, image/x-icon, font/opentype, font/ttf, \
                          font/eot, font/otf
}
```

### 应压缩的类型

- `text/html`、`text/css`、`text/javascript`、`text/xml`、`text/plain`
- `application/javascript`、`application/json`、`application/xml`
- `image/svg+xml`（SVG 是基于文本的 XML）
- `application/x-font-ttf`、`font/woff`（某些字体格式可受益）

### 不应压缩的类型

不要压缩已经压缩过的格式：

- `image/jpeg`、`image/png`、`image/gif`、`image/webp`
- `video/*`、`audio/*`
- `application/zip`、`application/gzip`、`application/pdf`
- `font/woff2`（已经过 Brotli 压缩）

## 静态文件预压缩

OLS 可以预压缩静态文件，并在可用时提供压缩版本。这样可避免运行时 CPU 开销。

当 `gzipAutoUpdateStatic` 为 `1` 时，OLS 在首次请求时自动创建静态文件的 `.gz` 版本。当原始文件更改时，预压缩文件会自动更新。

对于 Brotli 预压缩，使用外部工具：

```bash
# 使用 Brotli 预压缩静态资源
find /var/www/example.com -type f \
  \( -name "*.css" -o -name "*.js" -o -name "*.html" -o -name "*.svg" \) \
  -exec brotli -f -Z {} \;
```

这会在原始文件旁创建 `.br` 文件。当客户端支持 Brotli 时，OLS 会提供这些文件。

## 按虚拟主机配置压缩

为特定虚拟主机覆盖压缩设置：

```apacheconf
virtualhost example {
  ...
  tuning {
    enableGzipCompress    1
    enableDynGzipCompress 1
    gzipCompressLevel     4
  }
}
```

## .htaccess 压缩（LiteHTTPD）

使用 LiteHTTPD 模块，也可通过 `.htaccess` 配置压缩：

```apacheconf
<IfModule mod_deflate.c>
  AddOutputFilterByType DEFLATE text/html text/css text/javascript
  AddOutputFilterByType DEFLATE application/javascript application/json
  AddOutputFilterByType DEFLATE application/xml image/svg+xml
</IfModule>
```

:::note
OLS 处理 `.htaccess` 中的 `mod_deflate` 指令以保持兼容性，但压缩行为主要由服务器级配置控制。`.htaccess` 指令作为提示使用。
:::

## 验证压缩

检查响应是否已压缩：

```bash
# 测试 Gzip
curl -H "Accept-Encoding: gzip" -I https://example.com

# 测试 Brotli
curl -H "Accept-Encoding: br" -I https://example.com
```

查找 `Content-Encoding` 响应头：

```
Content-Encoding: gzip
```

或

```
Content-Encoding: br
```

## 故障排除

**响应未被压缩：**
- 确认 `enableGzipCompress` 为 `1`
- 检查响应的 MIME 类型是否在 `compressibleTypes` 中
- 确保响应体大于 `gzipMinFileSize`
- 确认客户端发送了 `Accept-Encoding: gzip` 或 `Accept-Encoding: br`

**压缩导致 CPU 使用率高：**
- 降低 `gzipCompressLevel`（建议 4）或 `brotliCompressLevel`（建议 3）
- 禁用动态压缩（`enableDynGzipCompress 0`），依赖预压缩的静态文件
- 对动态内容使用较低的 Brotli 级别

**双重压缩异常：**
- 确保未对已压缩格式进行压缩
- 检查应用是否在 OLS 添加压缩之前已压缩了响应
