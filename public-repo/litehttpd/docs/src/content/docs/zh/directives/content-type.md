---
title: 内容类型
description: 内容类型指令参考
---

## 指令

| 指令 | 语法 |
|------|------|
| `AddType` | `AddType MIME-type extension [extension ...]` |
| `AddEncoding` | `AddEncoding encoding extension [extension ...]` |
| `AddCharset` | `AddCharset charset extension [extension ...]` |
| `AddDefaultCharset` | `AddDefaultCharset charset\|Off` |
| `DefaultType` | `DefaultType MIME-type` |
| `ForceType` | `ForceType MIME-type` |
| `DirectoryIndex` | `DirectoryIndex filename [filename ...]` |
| `RemoveType` | `RemoveType extension [extension ...]` |
| `RemoveHandler` | `RemoveHandler extension [extension ...]` |

## 示例

### 自定义 MIME 类型

```apache
AddType application/json .json5
AddType application/wasm .wasm
AddType font/woff2 .woff2
```

### 字符集

```apache
AddCharset UTF-8 .html .css .js
AddDefaultCharset UTF-8
```

### 目录索引

```apache
DirectoryIndex index.php index.html index.htm
```

### 强制内容类型

```apache
# 强制目录中的所有文件以纯文本格式提供
ForceType text/plain
```
