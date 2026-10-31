---
title: ESI
description: Edge Side Includes —— 片段包含、注释剥离与变量替换
---

ESI 处理按目录通过 `EsiEnable` 开启。开启后，**处理器输出**（PHP 等动态
处理器）的响应体会经过 ESI 引擎过滤；静态文件走 OLS sendfile 快路径，
不做转换。

## 指令

| 指令 | 语法 | 默认 |
|------|------|------|
| `EsiEnable` | `On\|Off` | Off |

## 支持的标签

| 标签 | 效果 |
|------|------|
| `<esi:include src="/frag.html"/>` | 内联文档根内的片段 |
| `<esi:remove> … </esi:remove>` | 剥离包裹的内容 |
| `<esi:comment> … </esi:comment>` | 剥离（注释不会到达客户端） |
| `<esi:vars> … </esi:vars>` | 替换 `$(VARIABLE)` 变量引用 |

`<esi:vars>` 内支持的变量：`$(HTTP_USER_AGENT)`、`$(HTTP_COOKIE)`、
`$(HTTP_REFERER)`、`$(HTTP_HOST)`、`$(QUERY_STRING)`、
`$(REQUEST_METHOD)`、`$(REMOTE_ADDR)`。未知变量展开为空串（W3C 语义）。

## 示例

```apache
# .htaccess
EsiEnable On
```

```php
<?php
echo 'A<esi:include src="/frag.html"/>B';
echo '<esi:comment>internal note</esi:comment>C';
```

客户端收到的响应体（设 `/frag.html` 内容为 `FRAGMENT`）：`AFRAGMENTBC`。

## Include 安全加固

`<esi:include>` 针对文件泄露做了加固：

- `src` 必须是**文档根内**的绝对路径——经 `realpath` 围堵解析，中间
  目录为符号链接也无法逃逸（攻击者可写的 `uploads/` 软链指向 `/etc`
  会被拒绝）
- 拒绝路径穿越（`..`）
- `O_NOFOLLOW` 打开；仅常规文件；单个片段上限 64 KB
- 片段缺失或超限只是移除标签——页面照常输出

## Content-Type 门控

ESI 仅作用于 `text/html` 与 `application/xhtml+xml` 响应，且从不处理
压缩过的响应体。其它类型原样通过。

## 分块边界正确性

跨响应块分裂的标签（流式输出）会先重组再处理；页面最后一个块在标签
中间结束时，由下一个块续上或在流结束时按字面冲刷。ESI 标记不会以
可见文本形式泄漏给客户端。

## 动态 Include

`src` 不匹配静态文件（或指向 `.php` 等处理器）时按**回环子请求**解析：
片段走完整 OLS 管线——`.htaccess`、PHP/LSAPI、mod_security——其响应体
被拼接进页面。这正是 LSWS 企业版的动态 ESI 行为。

```apache
EsiEnable On
```

```php
<?php echo 'Hello <esi:include src="/cart-summary.php"/>!'; ?>
```

`/cart-summary.php` 执行时会透传父请求的 **Cookie** 与 **Host** 头，
因此依赖会话的片段能正确渲染。查询串原样传递（`/frag.php?n=7`）。

语义与防护：

- **持留-汇聚**：从首个动态 include 起输出被持留，在响应末尾统一拼接
  ——页面绝不会乱序流出。
- **硬预算**：每片段 1 秒、每页 2.5 秒（环境变量
  `LITEHTTPD_ESI_DYN_TIMEOUT_MS` / `LITEHTTPD_ESI_DYN_TOTAL_MS`）、
  每页 16 个片段、每片段响应体 64 KB。任何超限都移除标签并照常完成
  页面（fail-open）。
- **防递归**：片段请求携带 `X-LiteHttpd-Esi-Subrequest: 1`（PHP 可在
  `$_SERVER` 中看到）；带标记的请求拒绝进一步的**动态** include——
  片段内的静态 ESI 照常工作。
- **处理器文件绝不作为源码内联**：`.php` 等后缀无条件跳过静态快径。
- **Worker 数**：join 期间由**另一个** OLS worker 服务片段（带重试以
  对抗连接路由竞态）。OLS 需 **Children ≥ 2**；单 worker 实例上所有
  片段都会超时并按 fail-open 完成页面。
- 回环目标默认 `127.0.0.1:80`；用 `LITEHTTPD_ESI_LOOPBACK=host:port`
  覆盖。
