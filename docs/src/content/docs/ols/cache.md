---
title: "LiteSpeed Cache"
description: "Configuring the LiteSpeed Cache module on OpenLiteSpeed"
---

## Overview

LiteSpeed Cache (LSCache) is a server-level full-page cache built into OLS. It stores rendered HTML pages and serves them directly from the web server, bypassing PHP entirely for cached requests. This can improve response times by 10-100x for cacheable pages.

## Enable the Cache Module

In `httpd_config.conf`:

```apacheconf
module cache {
  ls_enabled              1

  checkPublicCache        1
  checkPrivateCache       1
  maxCacheObjSize         10000000
  maxStaleAge             200
  qsCache                 1
  reqCookieCache          1
  respCookieCache         1
  ignoreReqCacheCtrl      1
  ignoreRespCacheCtrl     0

  storagePath             /usr/local/lsws/cachedata

  enableCache             0
  enablePrivateCache      0
}
```

### Key Parameters

| Parameter | Default | Description |
|-----------|---------|-------------|
| `ls_enabled` | 1 | Load the cache module |
| `checkPublicCache` | 1 | Check for publicly cached pages |
| `checkPrivateCache` | 1 | Check for privately cached pages (per-user) |
| `maxCacheObjSize` | 10000000 | Maximum size of a cached object in bytes (~10 MB) |
| `maxStaleAge` | 200 | Seconds a stale cache entry can be served while revalidating |
| `qsCache` | 1 | Cache pages with query strings |
| `reqCookieCache` | 1 | Allow caching when request has cookies |
| `respCookieCache` | 1 | Allow caching when response sets cookies |
| `ignoreReqCacheCtrl` | 1 | Ignore client `Cache-Control` headers |
| `storagePath` | /usr/local/lsws/cachedata | Directory for cached files |
| `enableCache` | 0 | Enable public cache at server level (application must still send cache headers) |
| `enablePrivateCache` | 0 | Enable private cache at server level |

:::note
Setting `enableCache` to `1` at the server level does not automatically cache everything. The application must send the `X-LiteSpeed-Cache-Control: public` response header for a page to be cached.
:::

## Per-VHost Cache Configuration

Enable caching for a specific virtual host:

```apacheconf
virtualhost example {
  ...
  module cache {
    enableCache           1
    enablePrivateCache    1
    storagePath           /usr/local/lsws/cachedata/example
  }
}
```

## How LSCache Works

LSCache uses **response headers** from the application to determine caching behavior:

| Header | Description |
|--------|-------------|
| `X-LiteSpeed-Cache-Control: public, max-age=3600` | Cache publicly for 1 hour |
| `X-LiteSpeed-Cache-Control: private, max-age=1800` | Cache per-user for 30 minutes |
| `X-LiteSpeed-Cache-Control: no-cache` | Do not cache this response |
| `X-LiteSpeed-Tag: tag1, tag2` | Assign cache tags for selective purging |
| `X-LiteSpeed-Purge: tag1` | Purge all entries with this tag |
| `X-LiteSpeed-Purge: *` | Purge all cached entries |

These headers are consumed by OLS and **not** sent to the client.

## WordPress with LiteSpeed Cache Plugin

The LiteSpeed Cache plugin for WordPress is the most popular LSCache integration. It automatically sends the appropriate cache headers.

### Install

```bash
wp plugin install litespeed-cache --activate --allow-root
```

### Recommended Settings

In LiteSpeed Cache > General:
- Enable LiteSpeed Cache: **On**
- Guest Mode: **On**

In LiteSpeed Cache > Cache:
- Cache Logged-in Users: **Off**
- Cache Mobile: **On** (for responsive themes)
- Default Public Cache TTL: **604800** (7 days)
- Default Private Cache TTL: **1800** (30 min)

In LiteSpeed Cache > Cache > Purge:
- Purge All On Upgrade: **On**

### Cache Tags

The WordPress plugin automatically tags cached pages:

- Posts: `P.{post_id}`
- Categories: `T.{term_id}`
- Authors: `A.{author_id}`
- Front page: `FP`
- 404 pages: `404`

When a post is updated, only pages tagged with that post's ID are purged.

## Manual Cache Control (Non-WordPress)

For custom applications, send cache headers from your application:

### PHP Example

```php
// Cache publicly for 1 hour
header('X-LiteSpeed-Cache-Control: public, max-age=3600');
header('X-LiteSpeed-Tag: page, page_' . $page_id);

// Dynamic pages - no cache
header('X-LiteSpeed-Cache-Control: no-cache');
```

### Laravel Middleware

```php
class LiteSpeedCache
{
    public function handle($request, Closure $next)
    {
        $response = $next($request);

        if ($request->isMethod('GET') && !auth()->check()) {
            $response->header('X-LiteSpeed-Cache-Control', 'public, max-age=3600');
        }

        return $response;
    }
}
```

## Cache Purge

### Purge via HTTP Header

Send a purge header from your application:

```php
header('X-LiteSpeed-Purge: *');          // Purge everything
header('X-LiteSpeed-Purge: tag1, tag2'); // Purge specific tags
```

### Purge via Command Line

```bash
# Purge all cached files
rm -rf /usr/local/lsws/cachedata/*

# Restart to clear in-memory cache index
systemctl restart lsws
```

### WordPress Purge

```bash
# WP-CLI
wp litespeed-purge all --allow-root

# Or from the WordPress admin bar: LiteSpeed Cache > Purge All
```

## Cache Storage

### Disk Usage

Monitor cache disk usage:

```bash
du -sh /usr/local/lsws/cachedata/
```

### RAM Disk for Better Performance

For maximum cache performance, mount the cache storage on a tmpfs:

```bash
mount -t tmpfs -o size=1G tmpfs /usr/local/lsws/cachedata
```

Add to `/etc/fstab` for persistence across reboots:

```
tmpfs /usr/local/lsws/cachedata tmpfs size=1G,noatime 0 0
```

## Verify Cache Status

Check the response headers to verify caching:

```bash
curl -I https://example.com
```

Look for:

```
X-LiteSpeed-Cache: hit          # Served from cache
X-LiteSpeed-Cache: miss         # Not in cache, response was generated
X-LiteSpeed-Cache: hit,private  # Served from private cache
```

## Troubleshooting

**Cache always shows "miss":**
- Verify `enableCache` is `1` for the virtual host
- Check that the application sends `X-LiteSpeed-Cache-Control` headers
- Ensure the response does not have `Set-Cookie` headers (unless `respCookieCache` is enabled)
- Check that the URL does not match any no-cache rules

**Stale content after updates:**
- Verify purge mechanism is working
- Check cache TTL values
- For WordPress, ensure the LiteSpeed Cache plugin purge hooks are active

**Cache not working for logged-in users:**
- This is by default -- logged-in users get dynamic responses
- Enable private cache if needed: `X-LiteSpeed-Cache-Control: private, max-age=1800`
