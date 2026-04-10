---
title: "Python (WSGI/ASGI)"
description: "在 OpenLiteSpeed 上运行 Python Web 应用"
---

## 概述

OpenLiteSpeed 可以通过两种方式提供 Python Web 应用服务：

1. **LSAPI** -- LiteSpeed 原生 Python LSAPI 模块（最佳性能）
2. **反向代理** -- OLS 代理到 Gunicorn、uWSGI 或 Uvicorn（更灵活）

## 方式 1：Python LSAPI

LiteSpeed 提供了 Python LSAPI 模块，使用与 lsphp 相同的高性能协议与 OLS 通信。

### 安装 Python LSAPI

```bash
pip install lswsgi
```

### Django 使用 LSAPI

在 `httpd_config.conf` 中配置外部应用：

```apacheconf
extprocessor python {
  type                    lsapi
  address                 uds://tmp/lshttpd/python.sock
  maxConns                10
  initTimeout             60
  retryTimeout            0
  persistConn             1
  respBuffer              0
  autoStart               1
  path                    /usr/bin/python3
  backlog                 100
  instances               1
  env                     PYTHONPATH=/var/www/myproject
  env                     LSAPI_CHILDREN=10
}
```

在项目根目录创建 `wsgi_entry.py`（或使用 Django 默认的 `wsgi.py`）：

```python
import os
os.environ.setdefault('DJANGO_SETTINGS_MODULE', 'myproject.settings')

from django.core.wsgi import get_wsgi_application
application = get_wsgi_application()
```

### Flask 使用 LSAPI

```python
# wsgi_entry.py
from myapp import app as application
```

### 脚本处理器和上下文

```apacheconf
context / {
  type                    lsapi
  handler                 python
  addDefaultCharset       off
}
```

## 方式 2：反向代理到 Gunicorn

### 安装并运行 Gunicorn

```bash
pip install gunicorn

# Django
cd /var/www/myproject
gunicorn myproject.wsgi:application --bind 127.0.0.1:8000 --workers 4

# Flask
gunicorn myapp:app --bind 127.0.0.1:8000 --workers 4
```

### Gunicorn systemd 服务

```ini
# /etc/systemd/system/gunicorn.service
[Unit]
Description=Gunicorn Django Server
After=network.target

[Service]
User=nobody
Group=nobody
WorkingDirectory=/var/www/myproject
ExecStart=/usr/local/bin/gunicorn myproject.wsgi:application \
  --bind unix:/run/gunicorn/gunicorn.sock \
  --workers 4 \
  --timeout 120
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
```

```bash
mkdir -p /run/gunicorn
chown nobody:nobody /run/gunicorn
systemctl enable --now gunicorn
```

### OLS 代理配置

```apacheconf
extprocessor gunicorn {
  type                    proxy
  address                 uds://run/gunicorn/gunicorn.sock
  maxConns                100
  initTimeout             60
  retryTimeout            0
  persistConn             1
  respBuffer              0
}
```

虚拟主机上下文：

```apacheconf
# 直接提供静态文件
context /static/ {
  location                /var/www/myproject/static/
  allowBrowse             1
  extraHeaders {
    set                   Cache-Control "public, max-age=31536000"
  }
}

context /media/ {
  location                /var/www/myproject/media/
  allowBrowse             1
}

# 代理动态请求
context / {
  type                    proxy
  handler                 gunicorn
  addDefaultCharset       off
}
```

## 方式 3：反向代理到 uWSGI

### 安装并运行 uWSGI

```bash
pip install uwsgi

uwsgi --http-socket 127.0.0.1:8000 \
  --wsgi-file myproject/wsgi.py \
  --master \
  --processes 4 \
  --threads 2
```

### OLS 配置

```apacheconf
extprocessor uwsgi_app {
  type                    proxy
  address                 127.0.0.1:8000
  maxConns                100
  initTimeout             60
  retryTimeout            0
  persistConn             1
  respBuffer              0
}
```

## ASGI 应用（Django Channels、FastAPI）

对于异步 Python 框架，在 OLS 后面使用 Uvicorn 或 Daphne。

### FastAPI 使用 Uvicorn

```bash
pip install fastapi uvicorn

uvicorn main:app --host 127.0.0.1 --port 8000 --workers 4
```

### Django Channels 使用 Daphne

```bash
pip install daphne
daphne -b 127.0.0.1 -p 8000 myproject.asgi:application
```

OLS 配置方式与任何反向代理相同：

```apacheconf
extprocessor asgi_app {
  type                    proxy
  address                 127.0.0.1:8000
  maxConns                100
  initTimeout             60
  retryTimeout            0
  persistConn             1
  respBuffer              0
}

# 在 vhconf.conf 中
context / {
  type                    proxy
  handler                 asgi_app
  addDefaultCharset       off
}
```

WebSocket 支持通过 proxy 上下文自动生效。

## Django 静态文件

收集静态文件并通过 OLS 直接提供服务：

```bash
python manage.py collectstatic
```

确保 Django 设置中的 `STATIC_ROOT` 指向 OLS 可以提供服务的目录：

```python
STATIC_URL = '/static/'
STATIC_ROOT = '/var/www/myproject/static/'
```

## 故障排除

**LSAPI 模式下出现 ModuleNotFoundError：**
- 在外部处理器配置中设置 `PYTHONPATH` 环境变量
- 确保虚拟环境已激活，或其 site-packages 在 `PYTHONPATH` 中

**502 Bad Gateway：**
- 确认 Python 进程正在运行
- 检查 Unix 套接字连接的套接字权限
- 查看 OLS 错误日志：`/usr/local/lsws/logs/error.log`

**静态文件返回 404：**
- 运行 `python manage.py collectstatic`
- 确认 OLS 上下文路径与 `STATIC_URL` 匹配
