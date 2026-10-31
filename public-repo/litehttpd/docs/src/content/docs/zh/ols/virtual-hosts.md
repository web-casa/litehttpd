---
title: "虚拟主机"
description: "在 OpenLiteSpeed 中配置虚拟主机，包括文档根目录、上下文、脚本处理器和 .htaccess 支持。"
---

OpenLiteSpeed 中每个虚拟主机都有自己的配置文件，位于 `/usr/local/lsws/conf/vhosts/<vhost-name>/vhconf.conf`。

## 虚拟主机配置结构

```
vhconf.conf
  docRoot
  index
  errorlog
  accesslog
  scriptHandler { }
  context(s) { }
  rewrite { }
  accessControl { }
  general { }
```

## 创建虚拟主机

### 通过 WebAdmin

1. 在左侧菜单中点击 **Virtual Hosts**。
2. 点击 **Add** 创建新的虚拟主机。
3. 填写名称、文档根目录和配置文件路径。
4. 保存并重启。

### 通过配置文件

创建目录和配置文件：

```bash
mkdir -p /usr/local/lsws/conf/vhosts/example.com
```

创建 `/usr/local/lsws/conf/vhosts/example.com/vhconf.conf`：

```apacheconf
docRoot                   /var/www/example.com/public/

index {
    useServer               0
    indexFiles               index.php, index.html
}

errorlog /usr/local/lsws/logs/example.com-error.log {
    useServer               0
    logLevel                WARN
    rollingSize             10M
}

accesslog /usr/local/lsws/logs/example.com-access.log {
    useServer               0
    logFormat               "%h %l %u %t \"%r\" %>s %b"
    rollingSize             10M
}
```

然后在 `httpd_config.conf` 中引用它：

```apacheconf
virtualhost example.com {
    vhRoot                  /usr/local/lsws/conf/vhosts/example.com/
    configFile              $VH_ROOT/vhconf.conf
    allowSymbolLink         1
    enableScript            1
    restrained              1
}

listener Default {
    address                 *:80
    secure                  0
    map                     example.com example.com
}
```

## 文档根目录

```apacheconf
docRoot                   /var/www/example.com/public/
```

`docRoot` 可以使用 `$VH_ROOT` 变量：

```apacheconf
docRoot                   $VH_ROOT/public/
```

## 脚本处理器

脚本处理器定义 OLS 如何处理动态内容：

```apacheconf
scriptHandler {
    add                     lsapi:lsphp83 php
}
```

这会将所有 `.php` 文件通过 lsphp83 LSAPI 处理器进行路由。

## 上下文

上下文定义了按目录或按 URI 的行为：

```apacheconf
context / {
    location                $DOC_ROOT/
    allowBrowse             1
    indexFiles               index.php, index.html
}

context /protected/ {
    location                $DOC_ROOT/protected/
    allowBrowse             1
    realm                   SiteRealm
    authName                "Protected Area"
    required                valid-user
}

context /static/ {
    location                $DOC_ROOT/static/
    allowBrowse             1
    enableExpires           1
    expiresByType           image/*=A604800, text/css=A604800, application/javascript=A604800
}
```

## 使用 LiteHTTPD 支持 .htaccess

OpenLiteSpeed 内置的 .htaccess 支持有限。要获得完整的 Apache 兼容 .htaccess 处理能力，请使用 LiteHTTPD 模块（`ols_htaccess.so`）。

### 启用 .htaccess 处理

在虚拟主机配置中：

```apacheconf
general {
    allowOverride           All
    autoLoadHtaccess        1
    configFile              $VH_ROOT/vhconf.conf
}
```

- **allowOverride** -- 控制允许使用哪些 .htaccess 指令类别。可选值：`All`、`None`，或 `AuthConfig`、`FileInfo`、`Indexes`、`Limit`、`Options` 的组合。
- **autoLoadHtaccess** -- 设为 `1` 时，OLS 会自动从文档根目录层级中加载并处理 .htaccess 文件。

加载 LiteHTTPD 模块后，这些设置将启用完整的 .htaccess 支持，包括重写规则、访问控制、MIME 类型等。请参阅[指令参考](/zh/directives/overview/)获取完整的支持指令列表。

## 重写规则

OLS 支持在虚拟主机配置中使用重写规则：

```apacheconf
rewrite {
    enable                  1
    rules                   <<<END_rules
RewriteEngine On
RewriteCond %{REQUEST_FILENAME} !-f
RewriteCond %{REQUEST_FILENAME} !-d
RewriteRule ^(.*)$ /index.php?$1 [L,QSA]
    END_rules
}
```

使用 LiteHTTPD 模块时，重写规则也可以放在 `.htaccess` 文件中。

## 访问控制

按虚拟主机的 IP 访问控制：

```apacheconf
accessControl {
    allow                   ALL
    deny                    192.168.1.100
}
```

## 下一步

- [监听器](/zh/ols/listeners/) -- 将虚拟主机绑定到监听器。
- [SSL / TLS](/zh/ols/ssl/) -- 为虚拟主机启用 HTTPS。
- [自定义错误页](/zh/ols/custom-errors/) -- 配置错误文档。
- [日志](/zh/ols/logs/) -- 按虚拟主机配置日志。
