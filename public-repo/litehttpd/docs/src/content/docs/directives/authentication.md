---
title: Authentication
description: Protect directories with HTTP Basic authentication using AuthType, AuthName, and AuthUserFile.
---

Authentication directives enable password protection for directories using HTTP Basic authentication. Users are prompted for credentials via the browser's built-in login dialog.

## Directive Reference

| Directive | Syntax | Description |
|-----------|--------|-------------|
| `AuthType` | `AuthType Basic` | Set the authentication type (only `Basic` is supported) |
| `AuthName` | `AuthName "realm"` | Set the authentication realm shown in the login prompt |
| `AuthUserFile` | `AuthUserFile /path/to/.htpasswd` | Absolute path to the password file created with `htpasswd` |

All three directives must be used together, along with a `Require` directive to specify who is allowed access.

## Examples

### Protect a Directory

```apache
AuthType Basic
AuthName "Restricted Area"
AuthUserFile /home/user/example.com/.htpasswd
Require valid-user
```

### Protect WordPress Admin

```apache
<Files "wp-login.php">
  AuthType Basic
  AuthName "WordPress Admin"
  AuthUserFile /home/user/example.com/.htpasswd
  Require valid-user
</Files>
```

### Creating the Password File

Use the `htpasswd` utility to create and manage password files:

```bash
# Create a new file with a user
htpasswd -c /home/user/example.com/.htpasswd admin

# Add another user to an existing file
htpasswd /home/user/example.com/.htpasswd editor
```

:::caution
Store the `.htpasswd` file outside the web root so it cannot be downloaded by visitors. If that is not possible, add a rule to deny access to it:

```apache
<Files ".htpasswd">
  Require all denied
</Files>
```
:::

:::note
Only `AuthType Basic` is supported. Digest authentication is not available.
:::
