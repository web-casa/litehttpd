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
| `AuthUserFile` | `AuthUserFile /path/to/.htpasswd` | Absolute path to the password file created with `htpasswd`. **Must be inside the site's document root** (see security note below). |

All three directives must be used together, along with a `Require` directive to specify who is allowed access.

### Require directives

| Directive | Meaning |
|-----------|---------|
| `Require valid-user` | Any user with valid credentials in the `AuthUserFile` |
| `Require user alice bob` | Only the listed usernames (with valid credentials) |
| `Require group ...` | **Not supported** — group files are not implemented; this **fails closed** (access denied) rather than being ignored |

Any unrecognised `Require` form also fails closed (denies access) instead of being silently dropped, so a typo can never leave a directory unprotected.

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

### Restrict to specific users

```apache
AuthType Basic
AuthName "Staff Only"
AuthUserFile /home/user/example.com/.htpasswd
Require user alice bob
```

Only `alice` and `bob` (with valid passwords) are allowed; any other valid user is denied.

### Creating the Password File

Use the `htpasswd` utility to create and manage password files:

```bash
# Create a new file with a user
htpasswd -c /home/user/example.com/.htpasswd admin

# Add another user to an existing file
htpasswd /home/user/example.com/.htpasswd editor
```

:::caution
**`AuthUserFile` must be inside the site's document root.** For security, the
path is confined to the document-root subtree (resolved with `realpath` and
opened without following symlinks) so a `.htaccess` cannot point it at another
tenant's password file or a system file like `/etc/shadow`. A path outside the
document root is rejected with a `500` error.

Because the file lives within the web root, protect it from download with a
deny rule (the `.htpasswd`/`.htaccess` names are also blocked by default):

```apache
<Files ".htpasswd">
  Require all denied
</Files>
```
:::

:::note
Only `AuthType Basic` is supported. Digest authentication is not available.
:::
