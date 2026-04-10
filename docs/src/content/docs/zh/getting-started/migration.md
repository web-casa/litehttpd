---
title: 迁移指南
description: 如何从原版 OLS 切换到 LiteHTTPD，涵盖各种安装方式
---

本指南涵盖从原版 OpenLiteSpeed 切换到 LiteHTTPD 的所有常见安装方式：手动/脚本安装、CyberPanel、aaPanel（宝塔面板）和 Docker。

## 基本概念

LiteHTTPD 有两个版本：

- **Full 模式**（`openlitespeed-litehttpd` RPM）— 替换 OLS 二进制为打补丁版本 + 安装 `litehttpd_htaccess.so`。全部 80 种指令，包括 RewriteRule 执行和 php_value。
- **Thin 模式**（仅模块）— 将 `litehttpd_htaccess.so` 复制到原版 OLS。70+ 指令，但无 RewriteRule 执行和 php_value 传递。

生产环境推荐 Full 模式。Thin 模式适合快速评估，无需替换 OLS 二进制。

---

## 从原版 OLS（脚本/手动安装）迁移

适用于以下方式安装的 OLS：
- [ols1clk.sh 一键脚本](https://docs.openlitespeed.org/installation/script/)
- 官方 LiteSpeed 仓库（`yum install openlitespeed` / `apt install openlitespeed`）
- 手动下载二进制

### Full 模式（推荐）

```bash
# 1. 添加 LiteHTTPD 仓库并安装（替换原版 OLS）
curl -s https://rpms.litehttpd.com/setup.sh | bash
dnf install openlitespeed-litehttpd

# 2. 重启
systemctl restart lsws
```

RPM 自动完成以下操作：
- 将 OLS 二进制替换为打补丁版本（4 个 patch）
- 安装 `litehttpd_htaccess.so` 到 `/usr/local/lsws/modules/`
- 在 `httpd_config.conf` 中添加模块配置（仅首次安装）
- 在 Example vhost 中启用 rewrite（仅首次安装）

**现有配置文件会被保留** — RPM 使用 `%config(noreplace)`，`httpd_config.conf` 和所有 vhost 配置在升级时不会被覆盖。

### 重要：防止自动升级覆盖二进制

如果你之前从官方 LiteSpeed 仓库安装了 OLS，`dnf update` 可能会拉取新的 `openlitespeed` 包并覆盖打补丁的二进制。防止方法：

```bash
# 锁定包版本（EL 8/9/10）
dnf install python3-dnf-plugin-versionlock
dnf versionlock add openlitespeed

# 或在更新时排除
echo "exclude=openlitespeed" >> /etc/dnf/dnf.conf
```

同时禁用 OLS 内置升级脚本：

```bash
# 重命名 lsup.sh 防止原地升级
mv /usr/local/lsws/admin/misc/lsup.sh /usr/local/lsws/admin/misc/lsup.sh.bak
```

### Thin 模式（快速评估）

如果不想替换 OLS 二进制：

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
- `RewriteRule` / `RewriteCond` 会被解析但**不执行**（无 patch 0002）
- `php_value` / `php_flag` 会被解析但**不传递给 lsphp**（无 patch 0001）
- `Options -Indexes` 不会返回 403（无 patch 0004）
- `readApacheConf` 不可用（无 patch 0003）

---

## 从 CyberPanel 迁移

CyberPanel 从官方仓库安装 OLS，然后替换为从 `cyberpanel.net` 下载的 **CyberPanel 定制二进制**。它还自带 `.htaccess` 模块（`cyberpanel_ols.so`，约 29 种指令，付费授权）。

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

### 迁移步骤

**方案 A：Full 模式（替换 CyberPanel 二进制）**

```bash
# 1. 先禁用 CyberPanel 的 htaccess 模块
sed -i '/module cyberpanel_ols/,/}/d' /usr/local/lsws/conf/httpd_config.conf

# 2. 安装 LiteHTTPD（替换 CyberPanel 定制二进制）
curl -s https://rpms.litehttpd.com/setup.sh | bash
dnf install openlitespeed-litehttpd

# 3. 重启
systemctl restart lsws
```

**方案 B：Thin 模式（保留 CyberPanel 二进制）**

```bash
# 1. 禁用 CyberPanel 的 htaccess 模块
sed -i '/module cyberpanel_ols/,/}/d' /usr/local/lsws/conf/httpd_config.conf

# 2. 仅安装 LiteHTTPD 模块
cp litehttpd_htaccess.so /usr/local/lsws/modules/
cat >> /usr/local/lsws/conf/httpd_config.conf <<'EOF'

module litehttpd_htaccess {
    ls_enabled              1
}
EOF

# 3. 重启
systemctl restart lsws
```

### CyberPanel 注意事项

1. **CyberPanel 升级会覆盖 OLS 二进制。** 运行 `cyberpanel_upgrade` 或在面板中点击"升级"会调用 `installCustomOLSBinaries()`，从 `cyberpanel.net` 下载并替换二进制。CyberPanel 升级后需要重新安装 LiteHTTPD：

   ```bash
   dnf reinstall openlitespeed-litehttpd
   systemctl restart lsws
   ```

2. **不要同时加载两个模块。** `cyberpanel_ols.so` 和 `litehttpd_htaccess.so` 都会处理 `Header`、`ErrorDocument`、`php_value` 等指令。同时加载会产生冲突。启用 LiteHTTPD 前务必删除 `cyberpanel_ols` 模块配置。

3. **vhost.conf 会被自动重写。** CyberPanel 在域名级操作（SSL 签发、PHP 版本切换、别名修改）时会完全重写 `/usr/local/lsws/conf/vhosts/{domain}/vhost.conf`。不要在 vhost.conf 中添加 LiteHTTPD 配置 — 请使用 `.htaccess` 文件，CyberPanel 不会修改这些文件。

4. **httpd_config.conf 相对安全。** CyberPanel 通过正则表达式对 `httpd_config.conf` 进行局部修改（非全量重写），LiteHTTPD 的模块配置块会保留。

5. **CyberPanel 授权检查。** `cyberpanel_ols.so` 每 24 小时向 `platform.cyberpersons.com` 验证授权。移除该模块后此检查停止，对 LiteHTTPD 无影响。

---

## 从 aaPanel（宝塔面板）迁移

aaPanel 通过应用商店一键安装 OLS。使用标准 OLS 路径（`/usr/local/lsws/`），但通过面板 UI 管理配置文件。

### 迁移步骤

```bash
# 1. 安装 LiteHTTPD
curl -s https://rpms.litehttpd.com/setup.sh | bash
dnf install openlitespeed-litehttpd

# 2. 重启
systemctl restart lsws
```

### aaPanel 注意事项

1. **不要通过应用商店升级 OLS。** "升级"按钮会重新安装原版 OLS 二进制，覆盖打补丁版本。aaPanel 触发的 OLS 升级后需要重新安装 LiteHTTPD：

   ```bash
   dnf reinstall openlitespeed-litehttpd
   systemctl restart lsws
   ```

2. **vhost 配置会被重写。** aaPanel 在修改站点设置（PHP 版本、SSL、域名别名）时会重新生成 vhost `.conf` 文件。直接添加到 `vhost.conf` 的自定义指令可能会丢失。请使用 `.htaccess` 文件设置站点级规则。

3. **httpd_config.conf 可能被修改。** aaPanel 在服务器级配置变更时会修改 `httpd_config.conf`。LiteHTTPD 模块配置块通常会保留，但建议在面板操作后验证：

   ```bash
   grep -c 'litehttpd_htaccess' /usr/local/lsws/conf/httpd_config.conf
   # 应输出：1
   ```

4. **启用 .htaccess 加载。** aaPanel 的 OLS 默认不启用 `.htaccess` 自动加载。安装 LiteHTTPD 后，确保每个 vhost 配置中有：

   ```
   # /usr/local/lsws/conf/vhosts/<name>/vhost.conf
   autoLoadHtaccess 1
   ```

   或通过 OLS WebAdmin 控制台（端口 7080）启用：Virtual Host > General > Auto Load .htaccess > Yes。

5. **PHP-FPM vs LSPHP。** 部分 aaPanel 版本为 OLS 安装了 PHP-FPM 而非 LSPHP。LiteHTTPD 的 `php_value`/`php_flag` 指令需要 LSPHP（LSAPI）。如果发现使用的是 PHP-FPM，需切换到 LSPHP：

   ```bash
   dnf install lsphp83 lsphp83-common lsphp83-mysqlnd
   ```

   然后更新 vhost 配置中的外部应用路径指向 `/usr/local/lsws/lsphp83/bin/lsphp`。

---

## 从 ols-docker-env 迁移

[ols-docker-env](https://github.com/litespeedtech/ols-docker-env) 使用 `docker-compose` 和官方 `litespeedtech/openlitespeed` 镜像。OLS 二进制已内置于镜像中。

### 方案 A：自定义 Docker 镜像（推荐）

构建包含打补丁 OLS 二进制和 LiteHTTPD 模块的自定义镜像：

```dockerfile
FROM litespeedtech/openlitespeed:1.8.5-lsphp83

# 安装 LiteHTTPD（Full 模式）
COPY openlitespeed-patched /usr/local/lsws/bin/openlitespeed
COPY litehttpd_htaccess.so /usr/local/lsws/modules/
COPY litehttpd-confconv /usr/local/lsws/bin/

# 启用模块
RUN if ! grep -q 'litehttpd_htaccess' /usr/local/lsws/conf/httpd_config.conf; then \
    printf '\nmodule litehttpd_htaccess {\n    ls_enabled              1\n}\n' \
    >> /usr/local/lsws/conf/httpd_config.conf; \
    fi
```

更新 `docker-compose.yml` 使用自定义镜像：

```yaml
services:
  litespeed:
    image: your-registry/openlitespeed-litehttpd:1.8.5
    # ... 其余配置不变
```

### 方案 B：Volume 挂载（快速测试）

将打补丁的二进制和模块挂载到原版容器中：

```yaml
services:
  litespeed:
    image: litespeedtech/openlitespeed:1.8.5-lsphp83
    volumes:
      - ./lsws/conf:/usr/local/lsws/conf
      - ./sites:/var/www/vhosts
      # LiteHTTPD 新增：
      - ./litehttpd/openlitespeed:/usr/local/lsws/bin/openlitespeed:ro
      - ./litehttpd/litehttpd_htaccess.so:/usr/local/lsws/modules/litehttpd_htaccess.so:ro
```

### Docker 注意事项

1. **容器重启会还原容器内修改。** 在容器内直接修改二进制（如运行 `lsup.sh`）会在重启后丢失。请使用自定义镜像或 volume 挂载。

2. **配置 volume 是持久的。** `./lsws/conf` 目录从宿主机挂载，`httpd_config.conf` 的修改（包括模块配置块）会在容器重启和镜像更新后保留。

3. **在镜像中禁用 lsup.sh。** 原版镜像包含 `/usr/local/lsws/admin/misc/lsup.sh`。如果使用自定义镜像，移除或重命名它以防止通过 WebAdmin 意外升级 OLS：

   ```dockerfile
   RUN mv /usr/local/lsws/admin/misc/lsup.sh /usr/local/lsws/admin/misc/lsup.sh.disabled
   ```

4. **镜像更新。** 当 `litespeedtech/openlitespeed` 发布新镜像标签时，不要直接 pull — 这会替换你的自定义镜像。在 `docker-compose.yml` 中固定镜像标签，更新前先测试。

---

## 从 OLS 官方 Docker 文档迁移

[OLS 官方 Docker 文档](https://docs.openlitespeed.org/installation/docker/) 使用相同的 `litespeedtech/openlitespeed` Docker 镜像。迁移方式与上面的 [ols-docker-env](#从-ols-docker-env-迁移) 完全相同。

官方文档推荐通过 `Dockerfile` 构建自定义镜像 — 这是集成 LiteHTTPD 的最佳方式。

---

## 迁移后检查清单

从任何安装方式切换到 LiteHTTPD 后：

1. **验证模块已加载：**

   ```bash
   grep 'litehttpd_htaccess' /usr/local/lsws/conf/httpd_config.conf
   ```

2. **验证补丁（仅 Full 模式）：**

   ```bash
   # 检查关键补丁符号是否存在于二进制中
   strings /usr/local/lsws/bin/openlitespeed | grep -q 'set_php_config_value' && echo "patch 0001 OK"
   strings /usr/local/lsws/bin/openlitespeed | grep -q 'parse_rewrite_rules' && echo "patch 0002 OK"
   strings /usr/local/lsws/bin/openlitespeed | grep -q 'readApacheConf' && echo "patch 0003 OK"
   ```

3. **测试 .htaccess 处理：**

   ```bash
   # 创建测试 .htaccess
   echo 'Header set X-LiteHTTPD "active"' > /var/www/html/.htaccess

   # 检查响应头
   curl -sI http://localhost/ | grep X-LiteHTTPD
   # 预期：X-LiteHTTPD: active
   ```

4. **为 WordPress 启用 rewrite：** 参见 [配置 — WordPress 固定链接](/zh/getting-started/configuration/#wordpress-permalinks-return-404)。

5. **检查模块冲突：** 确保没有其他 `.htaccess` 模块被加载（如 `cyberpanel_ols.so`）。

---

## 兼容性说明

### LiteHTTPD 不改变的部分

- OLS WebAdmin 控制台（端口 7080）— 正常工作
- OLS 监听器/vhost 配置 — 完全兼容
- LSCache / LiteSpeed Cache 插件 — 正常工作
- LSAPI PHP 处理器 — 正常工作（Full 模式下增强了 php_value 支持）
- SSL/TLS 配置 — 不变
- HTTP/3 / QUIC — 不变

### LiteHTTPD 改变的部分

- `.htaccess` 文件现在被完整处理（80 种指令）
- OLS 二进制包含 4 个额外补丁（仅 Full 模式）
- 加载了新模块（`litehttpd_htaccess.so`）
- `readApacheConf` 指令可在 `httpd_config.conf` 中使用（Full 模式，patch 0003）
