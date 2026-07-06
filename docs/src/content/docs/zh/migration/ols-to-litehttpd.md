---
title: 从原版 OLS 迁移到 LiteHTTPD
description: 为现有 OpenLiteSpeed 安装添加 .htaccess 支持
---

## 概述

如果你正在运行原版 OpenLiteSpeed（通过[官方仓库](https://docs.openlitespeed.org/installation/)、[ols1clk.sh 脚本](https://docs.openlitespeed.org/installation/script/)或手动下载安装），添加 LiteHTTPD 即可获得完整的 `.htaccess` 支持，无需更改现有配置。

原版 OLS 只处理少量 `.htaccess` 指令（主要是 `RewriteFile` 的基本重写规则）。LiteHTTPD 添加 80 种指令，包括 `Header`、`Require`、`FilesMatch`、`AuthType Basic`、`If/ElseIf/Else` 等。

## 获得的功能

| 功能 | 原版 OLS | 加装 LiteHTTPD |
|------|---------|---------------|
| .htaccess 指令 | ~6（仅 RewriteFile） | **80** |
| `Require all denied` | 返回 200（失效） | 返回 403 |
| `Header set` | 被忽略 | 正常生效 |
| `RewriteRule [R=301]` | 404 | 301 重定向 |
| `Options -Indexes` | 404 | 403（需 patch 0004） |
| `FilesMatch` ACL | 被忽略 | 正常执行 |
| `AuthType Basic` | 不支持 | 完整支持 |
| PHP `php_value` | 不支持 | 支持（Full 模式） |
| `.ht*` 文件保护 | 可能被访问或 404 | 始终 403 |
| 静态文件性能 | 基线 | -5%（可忽略） |
| 内存开销 | 基线 | +13 MB |

## 安装

### RPM 安装（推荐）

```bash
# 添加 LiteHTTPD 仓库并安装
curl -s https://rpms.litehttpd.com/setup.sh | bash
dnf install openlitespeed-litehttpd

# 重启
systemctl restart lsws
```

RPM 安装 Full 模式：打补丁的 OLS 二进制 + `litehttpd_htaccess.so` 模块 + 自动配置。RPM 使用 `Conflicts: openlitespeed` 替换原版包。

RPM 在首次安装时自动：
- 将 OLS 二进制替换为打补丁版本（4 个 patch）
- 安装 `litehttpd_htaccess.so` 到 `/usr/local/lsws/modules/`
- 在 `httpd_config.conf` 中添加模块配置
- 在 Example vhost 中启用 rewrite
- 在 `indexFiles` 中添加 `index.php`

**现有配置文件会被保留** — RPM 使用 `%config(noreplace)`。

### Thin 模式（仅模块）

快速评估，不替换 OLS 二进制：

```bash
# 复制模块
cp litehttpd_htaccess.so /usr/local/lsws/modules/

# 在 httpd_config.conf 中启用
cat >> /usr/local/lsws/conf/httpd_config.conf <<'EOF'

module litehttpd_htaccess {
    ls_enabled              1
}
EOF

# 重启
systemctl restart lsws
```

Thin 模式限制：
- `RewriteRule` / `RewriteCond` 会被解析但**不执行**
- `php_value` / `php_flag` 会被解析但**不传递给 lsphp**
- `Options -Indexes` 不会返回 403
- `readApacheConf` 不可用

之后可以通过安装 RPM 升级到 Full 模式。

### 从源码构建

```bash
# 克隆 OLS，应用补丁，构建
git clone --branch v1.8.5 https://github.com/litespeedtech/openlitespeed.git
cd openlitespeed
patch -p1 < /path/to/patches/0001-lsiapi-phpconfig.patch
patch -p1 < /path/to/patches/0002-lsiapi-rewrite.patch
patch -p1 < /path/to/patches/0003-readapacheconf.patch
patch -p1 < /path/to/patches/0004-autoindex-403.patch
bash build.sh

# 构建模块
cd /path/to/litehttpd
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target litehttpd_htaccess
cp build/litehttpd_htaccess.so /usr/local/lsws/modules/
```

## 防止自动升级覆盖二进制

如果你之前从官方 LiteSpeed 仓库安装了 OLS，包管理器更新可能会恢复到原版二进制。

```bash
# 锁定包版本（EL 8/9/10）
dnf install python3-dnf-plugin-versionlock
dnf versionlock add openlitespeed

# 或在更新时排除
echo "exclude=openlitespeed" >> /etc/dnf/dnf.conf
```

同时禁用 OLS 内置升级脚本，防止 WebAdmin 控制台触发原地升级：

```bash
mv /usr/local/lsws/admin/misc/lsup.sh /usr/local/lsws/admin/misc/lsup.sh.bak
```

## 禁用 OLS autoLoadHtaccess

如果 vhost 配置中有 OLS 原生的 `autoLoadHtaccess 1`，需禁用它。OLS 原生 `.htaccess` 解析器处理少量指令（主要是 `ErrorDocument`、`Options`）。LiteHTTPD 也处理这些指令，同时启用会导致双重处理。

```bash
# 检查当前状态
grep -r 'autoLoadHtaccess' /usr/local/lsws/conf/vhosts/

# 如果设为 1，改为 0
sed -i 's/autoLoadHtaccess.*1/autoLoadHtaccess 0/' /usr/local/lsws/conf/vhosts/*/vhost.conf
```

## 行为变化

安装 LiteHTTPD 后，注意以下变化：

### 安全性改进（自动生效）

- **`.ht*` 文件被阻止** — 对 `.htaccess`、`.htpasswd` 等文件的请求返回 403。原版 OLS 可能会直接提供文件内容（安全风险）或返回 404。LiteHTTPD 匹配 Apache 默认的 `<Files ".ht*"> Require all denied</Files>` 行为。

- **路径遍历被阻止** — 编码的 `../` 序列（如 `%2e%2e/`）返回 403，而非原版 OLS 的 400/404。

### .htaccess 指令现在生效了

**这一点很重要。** 如果你的文档根目录中的 `.htaccess` 文件包含原版 OLS 之前忽略的指令，这些指令现在会生效。例如：

- `Require all denied` 在某个目录中现在会真正阻止访问（403）
- `Header set X-Frame-Options DENY` 现在会添加响应头
- `FilesMatch` 规则现在会执行访问控制

**在生产环境启用 LiteHTTPD 前请检查你的 `.htaccess` 文件。**

### Handler 指令（无操作）

`AddHandler`、`SetHandler`、`RemoveHandler` 和 `Action` 会被解析但不会改变请求处理方式。OLS 使用 vhost 配置中的 `scriptHandler` 进行处理器映射。

### ExecCGI 被阻止

`.htaccess` 中的 `Options +ExecCGI` 会被静默忽略。OLS 不支持通过 `.htaccess` 执行 CGI。

## 验证

```bash
# 检查模块已加载
grep 'litehttpd_htaccess' /usr/local/lsws/conf/httpd_config.conf

# 验证补丁（仅 Full 模式）
strings /usr/local/lsws/bin/openlitespeed | grep -q 'set_php_config_value' && echo "patch 0001 OK"
strings /usr/local/lsws/bin/openlitespeed | grep -q 'parse_rewrite_rules' && echo "patch 0002 OK"
strings /usr/local/lsws/bin/openlitespeed | grep -q 'readApacheConf' && echo "patch 0003 OK"

# 测试 .htaccess 处理
echo 'Header set X-LiteHTTPD "active"' > /var/www/html/.htaccess
curl -sI http://localhost/ | grep X-LiteHTTPD
# 预期：X-LiteHTTPD: active
```

## 不受影响的部分

- 现有 OLS 配置文件保持不变
- 虚拟主机设置、监听器和 SSL 配置不变
- PHP（lsphp）配置不变
- OLS 管理面板继续正常工作
- LSCache / LiteSpeed Cache 插件正常工作
- HTTP/3 / QUIC 不变
