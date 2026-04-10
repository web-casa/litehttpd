---
title: 认证
description: 认证指令参考
---

认证指令用于通过 HTTP Basic 认证为目录启用密码保护。用户将通过浏览器内置的登录对话框输入凭据。

## 指令参考

| 指令 | 语法 | 说明 |
|------|------|------|
| `AuthType` | `AuthType Basic` | 设置认证类型（仅支持 `Basic`） |
| `AuthName` | `AuthName "realm"` | 设置登录提示中显示的认证领域名称 |
| `AuthUserFile` | `AuthUserFile /path/to/.htpasswd` | 使用 `htpasswd` 创建的密码文件的绝对路径 |

这三个指令必须一起使用，并配合 `Require` 指令来指定谁可以访问。

## 示例

### 保护目录

```apache
AuthType Basic
AuthName "Restricted Area"
AuthUserFile /home/user/example.com/.htpasswd
Require valid-user
```

### 保护 WordPress 管理页面

```apache
<Files "wp-login.php">
  AuthType Basic
  AuthName "WordPress Admin"
  AuthUserFile /home/user/example.com/.htpasswd
  Require valid-user
</Files>
```

### 创建密码文件

使用 `htpasswd` 工具创建和管理密码文件：

```bash
# 创建新文件并添加用户
htpasswd -c /home/user/example.com/.htpasswd admin

# 向已有文件中添加用户
htpasswd /home/user/example.com/.htpasswd editor
```

:::caution
请将 `.htpasswd` 文件存储在 Web 根目录之外，以防止访客下载该文件。如果无法做到，请添加规则拒绝对该文件的访问：

```apache
<Files ".htpasswd">
  Require all denied
</Files>
```
:::

:::note
仅支持 `AuthType Basic`，不支持 Digest 认证。
:::
