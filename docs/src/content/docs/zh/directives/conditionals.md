---
title: 条件块
description: 条件块指令参考
---

## 指令

| 指令 | 语法 |
|------|------|
| `<If>` | `<If "expression">` |
| `<ElseIf>` | `<ElseIf "expression">` |
| `<Else>` | `<Else>` |

## 表达式运算符

| 运算符 | 说明 | 示例 |
|--------|------|------|
| `==` | 字符串相等 | `"%{REQUEST_URI}" == "/admin"` |
| `!=` | 字符串不等 | `"%{HTTPS}" != "on"` |
| `=~` | 正则匹配 | `"%{REQUEST_URI}" =~ /\.php$/` |
| `!~` | 正则不匹配 | `"%{HTTP_USER_AGENT}" !~ /bot/i` |
| `-f` | 文件存在 | `-f "%{REQUEST_FILENAME}"` |
| `-d` | 目录存在 | `-d "%{REQUEST_FILENAME}"` |
| `-s` | 文件存在且非空 | `-s "%{REQUEST_FILENAME}"` |
| `-l` | 是符号链接 | `-l "%{REQUEST_FILENAME}"` |
| `-ipmatch` | IP/CIDR 匹配 | `"-ipmatch" "192.168.0.0/16"` |
| `&&` | 逻辑与 | |
| `\|\|` | 逻辑或 | |
| `!` | 逻辑非 | |

## 示例

### 基于环境的头设置

```apache
<If "%{HTTPS} == 'on'">
  Header set Strict-Transport-Security "max-age=31536000"
</If>
```

### 条件访问控制

```apache
<If "-ipmatch '10.0.0.0/8'">
  Require all granted
</If>
<Else>
  Require valid-user
</Else>
```

### 请求方法检查

```apache
<If "%{REQUEST_METHOD} == 'OPTIONS'">
  Header set Access-Control-Allow-Origin "*"
  Header set Access-Control-Allow-Methods "GET, POST, OPTIONS"
</If>
```

:::caution
不支持在 `<If>` 块内使用 RewriteRule 指令。请将重写规则放在 .htaccess 文件的顶层。
:::
