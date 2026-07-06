---
title: "自定义错误页"
description: "在 OpenLiteSpeed 中配置自定义错误页，以及通过 LiteHTTPD 模块在 .htaccess 中配置。"
---

自定义错误页可以用您自己的 HTML 页面或自定义 URL 替换 OLS 默认的错误响应。

## OLS 虚拟主机配置

在虚拟主机配置（`vhconf.conf`）中，使用 `errorPage` 指令定义自定义错误页：

```apacheconf
errorPage 404 {
    url                     /errors/404.html
}

errorPage 500 {
    url                     /errors/500.html
}

errorPage 403 {
    url                     /errors/403.html
}
```

`url` 可以是：

- 相对于文档根目录的本地路径：`/errors/404.html`
- 用于外部重定向的绝对 URL：`https://example.com/not-found`

### 通过 WebAdmin

1. 进入 **Virtual Hosts** > 您的虚拟主机 > **General**。
2. 在 **Customized Error Pages** 下，点击 **Add**。
3. 设置错误代码和 URL。
4. 保存并重启。

## .htaccess 配置（LiteHTTPD 模块）

使用 LiteHTTPD 模块（`ols_htaccess.so`）时，您可以在 `.htaccess` 文件中使用 Apache 风格的 `ErrorDocument` 指令：

```apacheconf
ErrorDocument 404 /errors/404.html
ErrorDocument 403 /errors/403.html
ErrorDocument 500 "Internal Server Error - please try again later."
```

`ErrorDocument` 指令支持：

- **本地路径**：`ErrorDocument 404 /errors/404.html` -- 从文档根目录提供文件。
- **外部 URL**：`ErrorDocument 404 https://example.com/not-found` -- 发送 302 重定向。
- **内联消息**：`ErrorDocument 500 "Something went wrong."` -- 将引号内的文本作为响应体返回。

### 前提条件

`.htaccess` 中的 ErrorDocument 要生效，需要满足：

1. LiteHTTPD 模块必须在 `httpd_config.conf` 中加载。
2. 虚拟主机的 `allowOverride` 必须设为 `All` 或包含 `FileInfo`。
3. 虚拟主机的 `autoLoadHtaccess` 必须设为 `1`。

请参阅[虚拟主机](/zh/ols/virtual-hosts/)了解 `allowOverride` 和 `autoLoadHtaccess` 设置。

### 限制

- `ErrorDocument 401` 出于安全原因受到限制 -- 不能重定向到外部 URL，以防止凭据被截获。
- 本地错误文档路径必须位于文档根目录内。

## 错误页示例

在 `/var/www/example.com/public/errors/404.html` 创建一个简单的自定义 404 页面：

```html
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <title>Page Not Found</title>
    <style>
        body { font-family: sans-serif; text-align: center; padding: 50px; }
        h1 { font-size: 48px; color: #333; }
        p { font-size: 18px; color: #666; }
    </style>
</head>
<body>
    <h1>404</h1>
    <p>The page you are looking for does not exist.</p>
    <p><a href="/">Return to the homepage</a></p>
</body>
</html>
```

## 下一步

- [虚拟主机](/zh/ols/virtual-hosts/) -- 虚拟主机配置，包括 allowOverride。
- [日志](/zh/ols/logs/) -- 在日志中监控错误发生情况。
