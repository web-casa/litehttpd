---
title: 从 CyberPanel 迁移
description: 将 CyberPanel 的 OLS 切换到 LiteHTTPD
---

## 概述

CyberPanel 使用 OpenLiteSpeed 作为 Web 服务器。根据你的配置，你可能是：

- **原版 OLS**（大多数用户）— CyberPanel 从官方 LiteSpeed 仓库安装，没有付费 .htaccess 模块
- **带 htaccess 模块的 CyberPanel OLS**（付费授权）— CyberPanel 的 `cyberpanel_ols.so` 模块，提供约 29 种 .htaccess 指令

LiteHTTPD 适用于两种场景。它提供 80 种指令（vs CyberPanel 的 29 种），且免费（GPLv3）。

## 开始之前

检查你属于哪种情况：

```bash
# 检查 CyberPanel 的 htaccess 模块是否安装
ls -la /usr/local/lsws/modules/cyberpanel_ols.so 2>/dev/null && echo "CyberPanel 模块已安装" || echo "未安装 CyberPanel 模块"

# 检查模块是否在配置中启用
grep -q 'cyberpanel_ols' /usr/local/lsws/conf/httpd_config.conf && echo "模块已启用" || echo "模块未启用"
```

## 如果有 CyberPanel 的付费模块

如果 `cyberpanel_ols.so` 已安装并启用，先禁用它以避免冲突。两个模块都处理相同的 `.htaccess` 指令（`Header`、`ErrorDocument`、`php_value` 等）— 同时加载会导致不可预测的行为。

```bash
# 从配置中移除 CyberPanel 模块块
sed -i '/module cyberpanel_ols/,/}/d' /usr/local/lsws/conf/httpd_config.conf
```

### 功能对比

| 功能 | CyberPanel .htaccess | LiteHTTPD |
|------|---------------------|-----------|
| 指令数 | ~29 | 80 |
| RewriteRule 执行 | 不支持 | 支持（Full 模式） |
| If/ElseIf/Else | 不支持 | 支持 |
| ap_expr 引擎 | 不支持 | 支持 |
| Require 指令 | 不支持 | 支持 |
| AuthType Basic | 不支持 | 支持 |
| Options / AllowOverride | 不支持 | 支持 |
| php_value 传递 | 部分（仅 PHP_INI_ALL） | 完整 |
| 授权 | $59/年 或 $199 终身 | 免费（GPLv3） |

## 如果是原版 OLS（无付费模块）

大多数 CyberPanel 用户属于这种情况。你的 OLS 除了原生处理的部分（基本上只有 `RewriteFile` 的基本重写规则），没有 `.htaccess` 支持。可以直接安装 LiteHTTPD。

## 安装

### Full 模式（推荐）

```bash
# 添加 LiteHTTPD 仓库并安装
curl -s https://rpms.litehttpd.com/setup.sh | bash
dnf install openlitespeed-litehttpd

# 重启
systemctl restart lsws
```

### Thin 模式（保留原版二进制）

如果不想替换 OLS 二进制：

```bash
cp litehttpd_htaccess.so /usr/local/lsws/modules/

cat >> /usr/local/lsws/conf/httpd_config.conf <<'EOF'

module litehttpd_htaccess {
    ls_enabled              1
}
EOF

systemctl restart lsws
```

详见 [版本说明](/zh/getting-started/editions/) 了解 Full 和 Thin 的区别。

## CyberPanel 特别注意事项

### CyberPanel 升级会覆盖 OLS 二进制

这是最重要的注意事项。运行 `cyberpanel_upgrade` 或在 CyberPanel UI 中点击"升级"会从 `cyberpanel.net` 下载新的 OLS 二进制，替换 `/usr/local/lsws/bin/openlitespeed`。

任何 CyberPanel 升级后：

```bash
dnf reinstall openlitespeed-litehttpd
systemctl restart lsws
```

### vhost.conf 会被自动重写

CyberPanel 在以下操作时会**完全重写** `/usr/local/lsws/conf/vhosts/{domain}/vhost.conf`：
- 签发或续期 SSL 证书
- 更改 PHP 版本
- 修改域名别名
- 在面板中更改任何站点级设置

**不要**在 vhost.conf 中添加 LiteHTTPD 配置。请使用 `.htaccess` 文件 — CyberPanel 不会修改你的文档根目录中的 `.htaccess` 文件。

### httpd_config.conf 相对安全

CyberPanel 通过正则表达式局部修改 `httpd_config.conf`（非全量重写）。LiteHTTPD 模块配置块会保留。但建议在 CyberPanel 大版本升级后验证：

```bash
grep -c 'litehttpd_htaccess' /usr/local/lsws/conf/httpd_config.conf
# 应输出：1
```

### 禁用 OLS autoLoadHtaccess

CyberPanel 的 vhost 模板可能包含 `autoLoadHtaccess 1`，这会启用 OLS 原生的（有限的）`.htaccess` 解析。LiteHTTPD 同时加载时，`ErrorDocument` 和 `Options` 等指令会被双重处理。

检查并禁用：

```bash
# 检查所有 vhost 配置
grep -r 'autoLoadHtaccess' /usr/local/lsws/conf/vhosts/

# 如果有 autoLoadHtaccess 1，改为 0：
sed -i 's/autoLoadHtaccess.*1/autoLoadHtaccess 0/' /usr/local/lsws/conf/vhosts/*/vhost.conf
```

> **注意：** CyberPanel 重新生成 vhost.conf（如 SSL 续期）后，`autoLoadHtaccess` 可能会被重新启用。建议定期检查，或完全使用 `.htaccess` 文件管理站点级配置。

### 行为变化

安装 LiteHTTPD 后，服务器行为会有以下变化：

1. **`.ht*` 文件被阻止** — 对 `.htaccess`、`.htpasswd` 等文件的请求返回 403（Apache 兼容）。原版 OLS 会直接提供文件内容或返回 404。这是安全性改进。

2. **路径遍历返回 403** — 编码的 `../` 序列（如 `%2e%2e/`）返回 403，而非原版 OLS 的 400/404。这是安全性改进。

3. **所有 80 种 .htaccess 指令被处理** — 如果你的 `.htaccess` 文件包含原版 OLS 之前忽略的指令（如 `Header set`、`Require`、`FilesMatch`），它们现在会生效。在生产环境启用前请检查你的 `.htaccess` 文件。

## 验证

```bash
# 测试 .htaccess 处理
echo 'Header set X-LiteHTTPD "active"' > /home/example.com/public_html/.htaccess
curl -sI https://example.com/ | grep X-LiteHTTPD
# 预期：X-LiteHTTPD: active

# 清理
rm /home/example.com/public_html/.htaccess
```
