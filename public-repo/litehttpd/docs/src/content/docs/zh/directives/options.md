---
title: 选项
description: 选项指令参考
---

## 指令

```apache
Options [+|-]flag [+|-]flag ...
```

## 可用标志

| 标志 | 说明 |
|------|------|
| `Indexes` | 当不存在索引文件时允许目录列表 |
| `FollowSymLinks` | 跟随符号链接 |
| `MultiViews` | 内容协商 |
| `ExecCGI` | 允许 CGI 执行（在 .htaccess 中被阻止） |

使用 `+` 启用，`-` 禁用。不带前缀时，该值将替换继承的设置。

## 示例

### 禁用目录列表

```apache
Options -Indexes
```

### 组合选项

```apache
Options -Indexes +FollowSymLinks -MultiViews
```

:::note
出于安全考虑，`ExecCGI` 在 `.htaccess` 中始终被阻止。请改用 OLS 的 `scriptHandler` 配置。
:::
