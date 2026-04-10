---
title: "Custom Error Pages"
description: "Configure custom error pages in OpenLiteSpeed and via .htaccess with the LiteHTTPD module."
---

Custom error pages replace the default OLS error responses with your own HTML pages or redirect to custom URLs.

## OLS Virtual Host Configuration

In the virtual host config (`vhconf.conf`), define custom error pages using the `errorPage` directive:

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

The `url` can be:

- A local path relative to the document root: `/errors/404.html`
- An absolute URL for external redirects: `https://example.com/not-found`

### Via WebAdmin

1. Go to **Virtual Hosts** > your vhost > **General**.
2. Under **Customized Error Pages**, click **Add**.
3. Set the error code and URL.
4. Save and restart.

## .htaccess Configuration (LiteHTTPD Module)

When using the LiteHTTPD module (`ols_htaccess.so`), you can use Apache-style `ErrorDocument` directives in `.htaccess` files:

```apacheconf
ErrorDocument 404 /errors/404.html
ErrorDocument 403 /errors/403.html
ErrorDocument 500 "Internal Server Error - please try again later."
```

The `ErrorDocument` directive supports:

- **Local paths**: `ErrorDocument 404 /errors/404.html` -- serves the file from the document root.
- **External URLs**: `ErrorDocument 404 https://example.com/not-found` -- sends a 302 redirect.
- **Inline messages**: `ErrorDocument 500 "Something went wrong."` -- returns the quoted text as the response body.

### Requirements

For `.htaccess` ErrorDocument to work:

1. The LiteHTTPD module must be loaded in `httpd_config.conf`.
2. The virtual host must have `allowOverride` set to `All` or include `FileInfo`.
3. The virtual host must have `autoLoadHtaccess` set to `1`.

See [Virtual Hosts](/ols/virtual-hosts/) for the `allowOverride` and `autoLoadHtaccess` settings.

### Limitations

- `ErrorDocument 401` is restricted for security reasons -- it cannot redirect to an external URL to prevent credential interception.
- Local error document paths must exist within the document root.

## Example Error Page

Create a simple custom 404 page at `/var/www/example.com/public/errors/404.html`:

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

## Next Steps

- [Virtual Hosts](/ols/virtual-hosts/) -- virtual host configuration including allowOverride.
- [Logs](/ols/logs/) -- monitor error occurrences in the logs.
