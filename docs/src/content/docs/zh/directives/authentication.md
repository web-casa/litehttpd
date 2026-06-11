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
| `AuthUserFile` | `AuthUserFile /path/to/.htpasswd` | 使用 `htpasswd` 创建的密码文件的绝对路径。**必须位于站点文档根目录内**（见下方安全提示） |

这三个指令必须一起使用，并配合 `Require` 指令来指定谁可以访问。

### Require 指令

| 指令 | 含义 |
|------|------|
| `Require valid-user` | 任何在 `AuthUserFile` 中拥有有效凭据的用户 |
| `Require user alice bob` | 仅列出的用户名（且凭据有效） |
| `Require group ...` | **不支持** —— 未实现用户组文件；该指令**安全失败（拒绝访问）**，而不是被忽略 |

任何无法识别的 `Require` 形式同样会安全失败（拒绝访问），而不会被静默丢弃，因此拼写错误绝不会导致目录失去保护。

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
**`AuthUserFile` 必须位于站点文档根目录内。** 出于安全考虑，该路径被限制在文档根子树内（通过 `realpath` 解析，且打开时不跟随符号链接），以防止 `.htaccess` 将其指向其他租户的密码文件或 `/etc/shadow` 等系统文件。文档根目录之外的路径会被拒绝并返回 `500` 错误。

由于文件位于 Web 根目录内，请用拒绝规则防止其被下载（`.htpasswd`/`.htaccess` 等名称默认也会被拦截）：

```apache
<Files ".htpasswd">
  Require all denied
</Files>
```
:::

:::note
仅支持 `AuthType Basic`，不支持 Digest 认证。
:::
