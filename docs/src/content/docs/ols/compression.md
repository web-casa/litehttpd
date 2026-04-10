---
title: "Compression"
description: "Configuring Gzip and Brotli compression on OpenLiteSpeed"
---

## Overview

OLS supports both **Gzip** and **Brotli** compression for HTTP responses. Compression reduces bandwidth usage and improves page load times, particularly for text-based content like HTML, CSS, JavaScript, and JSON.

## Enable Compression

Compression is configured in `httpd_config.conf` under the `tuning` section:

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

### Parameters

| Parameter | Default | Description |
|-----------|---------|-------------|
| `enableGzipCompress` | 1 | Enable Gzip for static files |
| `enableBrotliCompress` | 1 | Enable Brotli compression |
| `enableDynGzipCompress` | 1 | Enable Gzip for dynamic (PHP) responses |
| `gzipCompressLevel` | 6 | Gzip level for dynamic content (1-9) |
| `brotliCompressLevel` | 5 | Brotli level for dynamic content (0-11) |
| `compressibleTypes` | default | MIME types to compress |
| `gzipAutoUpdateStatic` | 1 | Auto-create `.gz` files for static resources |
| `gzipStaticCompressLevel` | 6 | Gzip level for pre-compressed static files |
| `gzipMaxFileSize` | 10M | Maximum file size to compress |
| `gzipMinFileSize` | 300 | Minimum file size to compress (skip tiny files) |

## Compression Levels

### Gzip (1-9)

| Level | CPU usage | Compression ratio | Recommended for |
|-------|-----------|-------------------|-----------------|
| 1 | Very low | ~60% | High-traffic sites where CPU is scarce |
| 4-6 | Moderate | ~75-80% | General purpose (recommended) |
| 9 | High | ~82% | Static pre-compression only |

### Brotli (0-11)

Brotli provides better compression ratios than Gzip at similar CPU cost. However, levels above 6 become significantly more CPU-intensive and are only suitable for static pre-compression.

| Level | CPU usage | Compression ratio | Recommended for |
|-------|-----------|-------------------|-----------------|
| 1-4 | Low | Better than Gzip 6 | Dynamic content |
| 5-6 | Moderate | Significantly better | General purpose (recommended) |
| 7-11 | Very high | Maximum compression | Static pre-compression only |

## Compressible MIME Types

The `default` value for `compressibleTypes` covers common text-based types. To customize:

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

### Types to Compress

- `text/html`, `text/css`, `text/javascript`, `text/xml`, `text/plain`
- `application/javascript`, `application/json`, `application/xml`
- `image/svg+xml` (SVG is text-based XML)
- `application/x-font-ttf`, `font/woff` (some font formats benefit)

### Types NOT to Compress

Do not compress already-compressed formats:

- `image/jpeg`, `image/png`, `image/gif`, `image/webp`
- `video/*`, `audio/*`
- `application/zip`, `application/gzip`, `application/pdf`
- `font/woff2` (already Brotli-compressed)

## Static File Pre-Compression

OLS can pre-compress static files and serve the compressed version when available. This avoids runtime CPU usage.

When `gzipAutoUpdateStatic` is `1`, OLS automatically creates `.gz` versions of static files on first request. The pre-compressed file is updated when the original file changes.

For Brotli pre-compression, use an external tool:

```bash
# Pre-compress static assets with Brotli
find /var/www/example.com -type f \
  \( -name "*.css" -o -name "*.js" -o -name "*.html" -o -name "*.svg" \) \
  -exec brotli -f -Z {} \;
```

This creates `.br` files alongside the originals. OLS will serve these when the client supports Brotli.

## Per-VHost Compression

Override compression settings for a specific virtual host:

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

## .htaccess Compression (LiteHTTPD)

With the LiteHTTPD module, compression can also be configured via `.htaccess`:

```apacheconf
<IfModule mod_deflate.c>
  AddOutputFilterByType DEFLATE text/html text/css text/javascript
  AddOutputFilterByType DEFLATE application/javascript application/json
  AddOutputFilterByType DEFLATE application/xml image/svg+xml
</IfModule>
```

:::note
OLS processes the `mod_deflate` directives from `.htaccess` for compatibility, but compression behavior is primarily controlled by the server-level configuration. The `.htaccess` directives serve as hints.
:::

## Verify Compression

Check that responses are compressed:

```bash
# Test Gzip
curl -H "Accept-Encoding: gzip" -I https://example.com

# Test Brotli
curl -H "Accept-Encoding: br" -I https://example.com
```

Look for the `Content-Encoding` header:

```
Content-Encoding: gzip
```

or

```
Content-Encoding: br
```

## Troubleshooting

**Responses not compressed:**
- Verify `enableGzipCompress` is `1`
- Check that the response MIME type is in `compressibleTypes`
- Ensure the response body is larger than `gzipMinFileSize`
- Confirm the client sends `Accept-Encoding: gzip` or `Accept-Encoding: br`

**High CPU usage from compression:**
- Lower `gzipCompressLevel` (try 4) or `brotliCompressLevel` (try 3)
- Disable dynamic compression (`enableDynGzipCompress 0`) and rely on pre-compressed static files
- Use lower Brotli levels for dynamic content

**Double compression artifacts:**
- Ensure you are not compressing already-compressed formats
- Check that your application is not compressing responses before OLS adds its compression
