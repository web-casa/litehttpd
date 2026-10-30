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

v1 的 `<esi:include>` 解析**静态文件**。指向 PHP 端点的 include（子请求
渲染、片段级缓存）在路线图中，当前会被移除而不执行。
