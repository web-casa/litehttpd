---
title: "Docker 部署"
description: "使用 Docker 部署 OpenLiteSpeed，支持 PHP 和数据库。"
---

LiteSpeed 在 Docker Hub 上提供了官方 Docker 镜像，镜像中预装了 lsphp83。

## 快速开始

拉取并运行最新镜像：

```bash
docker pull litespeedtech/openlitespeed:latest
docker run -d --name ols -p 80:80 -p 443:443 -p 7080:7080 litespeedtech/openlitespeed:latest
```

通过 `http://localhost` 访问默认站点，通过 `https://localhost:7080` 访问 WebAdmin。

## Docker Compose 搭配 PHP 和 MariaDB

创建 `docker-compose.yml` 文件：

```yaml
version: "3.8"

services:
  openlitespeed:
    image: litespeedtech/openlitespeed:latest
    container_name: ols
    ports:
      - "80:80"
      - "443:443"
      - "7080:7080"
    volumes:
      - ./sites/:/var/www/vhosts/
      - ./config/httpd_config.conf:/usr/local/lsws/conf/httpd_config.conf
      - ./config/vhosts/:/usr/local/lsws/conf/vhosts/
      - ./logs/:/usr/local/lsws/logs/
    environment:
      - TZ=UTC
    restart: unless-stopped
    depends_on:
      - mariadb

  mariadb:
    image: mariadb:11
    container_name: mariadb
    volumes:
      - db_data:/var/lib/mysql
    environment:
      MYSQL_ROOT_PASSWORD: changeme
      MYSQL_DATABASE: app
      MYSQL_USER: app
      MYSQL_PASSWORD: changeme
    restart: unless-stopped

volumes:
  db_data:
```

启动服务栈：

```bash
docker compose up -d
```

## 卷挂载

| 宿主机路径 | 容器路径 | 用途 |
|---|---|---|
| `./sites/` | `/var/www/vhosts/` | 虚拟主机的文档根目录 |
| `./config/httpd_config.conf` | `/usr/local/lsws/conf/httpd_config.conf` | 主服务器配置 |
| `./config/vhosts/` | `/usr/local/lsws/conf/vhosts/` | 虚拟主机配置文件 |
| `./logs/` | `/usr/local/lsws/logs/` | 服务器错误日志和访问日志 |

## 环境变量

官方镜像支持以下环境变量：

| 变量 | 默认值 | 说明 |
|---|---|---|
| `TZ` | `UTC` | 容器时区 |

在 Docker 中设置 WebAdmin 凭据，在容器内运行密码脚本：

```bash
docker exec -it ols /usr/local/lsws/admin/misc/admpass.sh
```

## 自定义 Dockerfile

如需添加扩展或自定义镜像：

```dockerfile
FROM litespeedtech/openlitespeed:latest

# 安装额外的 PHP 扩展
RUN apt-get update && apt-get install -y \
    lsphp83-curl \
    lsphp83-intl \
    lsphp83-json \
    lsphp83-redis \
    && rm -rf /var/lib/apt/lists/*

# 复制自定义配置
COPY config/httpd_config.conf /usr/local/lsws/conf/httpd_config.conf
COPY sites/ /var/www/vhosts/
```

构建并运行：

```bash
docker build -t my-ols .
docker run -d --name ols -p 80:80 -p 443:443 -p 7080:7080 my-ols
```

## 管理容器

```bash
# 查看日志
docker logs ols

# 在容器内重启 OLS
docker exec ols /usr/local/lsws/bin/lswsctrl restart

# 进入容器 shell
docker exec -it ols bash

# 停止并删除
docker compose down
```

## 下一步

- [基础配置](/zh/ols/basic-config/) -- 了解配置文件结构。
- [虚拟主机](/zh/ols/virtual-hosts/) -- 在容器中配置站点。
- [SSL / TLS](/zh/ols/ssl/) -- 为 HTTPS 设置证书。
