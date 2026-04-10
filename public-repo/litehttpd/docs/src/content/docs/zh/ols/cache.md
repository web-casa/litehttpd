---
title: "LiteSpeed Cache"
description: "在 OpenLiteSpeed 上配置 LiteSpeed Cache 模块"
---

## 概述

LiteSpeed Cache（LSCache）是 OLS 内置的服务器级全页面缓存。它存储已渲染的 HTML 页面，直接从 Web 服务器提供服务，完全绕过 PHP 处理缓存请求。对于可缓存的页面，响应速度可提升 10-100 倍。

## 启用缓存模块

在 `httpd_config.conf` 中：

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

### 关键参数

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `ls_enabled` | 1 | 加载缓存模块 |
| `checkPublicCache` | 1 | 检查公共缓存页面 |
| `checkPrivateCache` | 1 | 检查私有缓存页面（按用户） |
| `maxCacheObjSize` | 10000000 | 缓存对象最大字节数（约 10 MB） |
| `maxStaleAge` | 200 | 重新验证时可提供过期缓存的秒数 |
| `qsCache` | 1 | 缓存带查询字符串的页面 |
| `reqCookieCache` | 1 | 请求包含 Cookie 时允许缓存 |
| `respCookieCache` | 1 | 响应设置 Cookie 时允许缓存 |
| `ignoreReqCacheCtrl` | 1 | 忽略客户端 `Cache-Control` 头 |
| `storagePath` | /usr/local/lsws/cachedata | 缓存文件存储目录 |
| `enableCache` | 0 | 在服务器级启用公共缓存（应用仍需发送缓存头） |
| `enablePrivateCache` | 0 | 在服务器级启用私有缓存 |

:::note
在服务器级将 `enableCache` 设为 `1` 不会自动缓存所有内容。应用必须发送 `X-LiteSpeed-Cache-Control: public` 响应头，页面才会被缓存。
:::

## 按虚拟主机配置缓存

为特定虚拟主机启用缓存：

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

## LSCache 工作原理

LSCache 使用应用发送的**响应头**来决定缓存行为：

| 响应头 | 说明 |
|--------|------|
| `X-LiteSpeed-Cache-Control: public, max-age=3600` | 公共缓存 1 小时 |
| `X-LiteSpeed-Cache-Control: private, max-age=1800` | 按用户缓存 30 分钟 |
| `X-LiteSpeed-Cache-Control: no-cache` | 不缓存此响应 |
| `X-LiteSpeed-Tag: tag1, tag2` | 分配缓存标签用于选择性清除 |
| `X-LiteSpeed-Purge: tag1` | 清除具有此标签的所有条目 |
| `X-LiteSpeed-Purge: *` | 清除所有缓存条目 |

这些响应头由 OLS 消费，**不会**发送给客户端。

## WordPress 与 LiteSpeed Cache 插件

WordPress 的 LiteSpeed Cache 插件是最流行的 LSCache 集成方案。它自动发送相应的缓存头。

### 安装

```bash
wp plugin install litespeed-cache --activate --allow-root
```

### 推荐设置

在 LiteSpeed Cache > 常规 中：
- 启用 LiteSpeed Cache：**开启**
- 访客模式：**开启**

在 LiteSpeed Cache > 缓存 中：
- 缓存已登录用户：**关闭**
- 缓存移动端：**开启**（适用于响应式主题）
- 默认公共缓存 TTL：**604800**（7 天）
- 默认私有缓存 TTL：**1800**（30 分钟）

在 LiteSpeed Cache > 缓存 > 清除 中：
- 升级时清除全部：**开启**

### 缓存标签

WordPress 插件自动为缓存页面添加标签：

- 文章：`P.{post_id}`
- 分类：`T.{term_id}`
- 作者：`A.{author_id}`
- 首页：`FP`
- 404 页面：`404`

当文章更新时，仅清除标记了该文章 ID 的页面。

## 手动缓存控制（非 WordPress）

对于自定义应用，从应用中发送缓存头：

### PHP 示例

```php
// 公共缓存 1 小时
header('X-LiteSpeed-Cache-Control: public, max-age=3600');
header('X-LiteSpeed-Tag: page, page_' . $page_id);

// 动态页面 - 不缓存
header('X-LiteSpeed-Cache-Control: no-cache');
```

### Laravel 中间件

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

## 缓存清除

### 通过 HTTP 头清除

从应用中发送清除头：

```php
header('X-LiteSpeed-Purge: *');          // 清除所有
header('X-LiteSpeed-Purge: tag1, tag2'); // 清除特定标签
```

### 通过命令行清除

```bash
# 清除所有缓存文件
rm -rf /usr/local/lsws/cachedata/*

# 重启以清除内存中的缓存索引
systemctl restart lsws
```

### WordPress 清除

```bash
# WP-CLI
wp litespeed-purge all --allow-root

# 或从 WordPress 管理栏：LiteSpeed Cache > 清除全部
```

## 缓存存储

### 磁盘使用

监控缓存磁盘使用：

```bash
du -sh /usr/local/lsws/cachedata/
```

### 内存盘提升性能

为获得最佳缓存性能，将缓存存储挂载到 tmpfs：

```bash
mount -t tmpfs -o size=1G tmpfs /usr/local/lsws/cachedata
```

添加到 `/etc/fstab` 以在重启后持久化：

```
tmpfs /usr/local/lsws/cachedata tmpfs size=1G,noatime 0 0
```

## 验证缓存状态

检查响应头以验证缓存：

```bash
curl -I https://example.com
```

查找以下内容：

```
X-LiteSpeed-Cache: hit          # 从缓存提供
X-LiteSpeed-Cache: miss         # 未命中缓存，响应为动态生成
X-LiteSpeed-Cache: hit,private  # 从私有缓存提供
```

## 故障排除

**缓存始终显示 "miss"：**
- 确认虚拟主机的 `enableCache` 为 `1`
- 检查应用是否发送了 `X-LiteSpeed-Cache-Control` 头
- 确保响应没有 `Set-Cookie` 头（除非启用了 `respCookieCache`）
- 检查 URL 是否匹配了任何不缓存规则

**更新后内容过期：**
- 确认清除机制正常工作
- 检查缓存 TTL 值
- 对于 WordPress，确保 LiteSpeed Cache 插件的清除钩子处于活跃状态

**已登录用户缓存不生效：**
- 这是默认行为 -- 已登录用户获取动态响应
- 如需要，启用私有缓存：`X-LiteSpeed-Cache-Control: private, max-age=1800`
