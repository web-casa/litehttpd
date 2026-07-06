---
title: Docker 环境
description: 在 ols-docker-env 和官方 OLS Docker 镜像中使用 LiteHTTPD
---

## 概述

本页涵盖以下环境中使用 LiteHTTPD：
- [ols-docker-env](https://github.com/litespeedtech/ols-docker-env)（docker-compose 方式）
- [OLS 官方 Docker 镜像](https://docs.openlitespeed.org/installation/docker/)（`litespeedtech/openlitespeed`）
- 自定义 Docker 部署

所有方式使用相同的基础镜像：`litespeedtech/openlitespeed`。主要挑战是 OLS 二进制已内置于镜像中，需要替换或覆盖。

## 方案 A：自定义 Docker 镜像（推荐）

构建包含打补丁 OLS 二进制和 LiteHTTPD 模块的自定义镜像：

```dockerfile
FROM litespeedtech/openlitespeed:1.8.5-lsphp83

# 复制打补丁的二进制和模块
COPY openlitespeed-patched /usr/local/lsws/bin/openlitespeed
COPY litehttpd_htaccess.so /usr/local/lsws/modules/
COPY litehttpd-confconv /usr/local/lsws/bin/

# 启用模块（仅在 httpd_config.conf 未被 volume 挂载时）
RUN if ! grep -q 'litehttpd_htaccess' /usr/local/lsws/conf/httpd_config.conf; then \
    printf '\nmodule litehttpd_htaccess {\n    ls_enabled              1\n}\n' \
    >> /usr/local/lsws/conf/httpd_config.conf; \
    fi

# 禁用 OLS 内置升级，防止意外覆盖二进制
RUN mv /usr/local/lsws/admin/misc/lsup.sh /usr/local/lsws/admin/misc/lsup.sh.disabled 2>/dev/null || true
```

更新 `docker-compose.yml`：

```yaml
services:
  litespeed:
    image: your-registry/openlitespeed-litehttpd:1.8.5
    # ... 其余配置不变
```

## 方案 B：Volume 挂载（快速测试）

将打补丁的二进制和模块挂载到原版容器中，无需构建自定义镜像：

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

在宿主机侧的 `./lsws/conf/httpd_config.conf` 中添加模块块：

```
module litehttpd_htaccess {
    ls_enabled              1
}
```

## 方案 C：Thin 模式（不替换二进制）

如果只需要 Thin 模式功能（无 RewriteRule 执行、无 php_value），只挂载模块 `.so` 即可：

```yaml
volumes:
  - ./litehttpd/litehttpd_htaccess.so:/usr/local/lsws/modules/litehttpd_htaccess.so:ro
```

## Docker 特别注意事项

### 容器重启会还原容器内修改

在运行中的容器内所做的修改（如运行 `lsup.sh`、编辑文件）在重启后丢失。请使用 volume 挂载或自定义镜像实现持久化。

### 配置 Volume 是持久的

如果 `./lsws/conf` 从宿主机挂载（ols-docker-env 标准做法），`httpd_config.conf` 的修改会在容器重启和镜像更新后保留。

### 固定镜像标签

当 `litespeedtech/openlitespeed` 发布新镜像时，不要直接 pull。在 `docker-compose.yml` 中固定标签，升级前先测试：

```yaml
image: litespeedtech/openlitespeed:1.8.5-lsphp83  # 固定版本
```

### 禁用 lsup.sh

原版镜像包含 `/usr/local/lsws/admin/misc/lsup.sh`。通过 WebAdmin 控制台运行它会在容器内下载并替换 OLS 二进制。方案 B 中 `:ro` 标志可防止覆盖。方案 A 中请在 Dockerfile 中移除该脚本。

### 禁用 autoLoadHtaccess

如果挂载的配置中有 OLS 的 `autoLoadHtaccess 1`，需禁用以避免双重处理：

```bash
sed -i 's/autoLoadHtaccess.*1/autoLoadHtaccess 0/' ./lsws/conf/vhosts/*/vhost.conf
```

## 验证

```bash
# 进入容器
docker exec -it litespeed bash

echo 'Header set X-LiteHTTPD "active"' > /var/www/vhosts/localhost/html/.htaccess
curl -sI http://localhost:8088/ | grep X-LiteHTTPD
# 预期：X-LiteHTTPD: active

# 验证补丁（Full 模式）
strings /usr/local/lsws/bin/openlitespeed | grep -q 'set_php_config_value' && echo "patch 0001 OK"
strings /usr/local/lsws/bin/openlitespeed | grep -q 'parse_rewrite_rules' && echo "patch 0002 OK"
strings /usr/local/lsws/bin/openlitespeed | grep -q 'readApacheConf' && echo "patch 0003 OK"
```
