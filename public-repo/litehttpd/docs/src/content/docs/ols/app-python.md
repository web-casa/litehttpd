---
title: "Python (WSGI/ASGI)"
description: "Running Python web applications on OpenLiteSpeed"
---

## Overview

OpenLiteSpeed can serve Python web applications through two approaches:

1. **LSAPI** -- LiteSpeed's native Python LSAPI module (best performance)
2. **Reverse proxy** -- OLS proxies to Gunicorn, uWSGI, or Uvicorn (more flexible)

## Method 1: Python LSAPI

LiteSpeed provides a Python LSAPI module that communicates with OLS using the same high-performance protocol used by lsphp.

### Install Python LSAPI

```bash
pip install lswsgi
```

### Django with LSAPI

Configure the external application in `httpd_config.conf`:

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

Create a `wsgi_entry.py` at your project root (or use Django's default `wsgi.py`):

```python
import os
os.environ.setdefault('DJANGO_SETTINGS_MODULE', 'myproject.settings')

from django.core.wsgi import get_wsgi_application
application = get_wsgi_application()
```

### Flask with LSAPI

```python
# wsgi_entry.py
from myapp import app as application
```

### Script Handler and Context

```apacheconf
context / {
  type                    lsapi
  handler                 python
  addDefaultCharset       off
}
```

## Method 2: Reverse Proxy to Gunicorn

### Install and Run Gunicorn

```bash
pip install gunicorn

# Django
cd /var/www/myproject
gunicorn myproject.wsgi:application --bind 127.0.0.1:8000 --workers 4

# Flask
gunicorn myapp:app --bind 127.0.0.1:8000 --workers 4
```

### Gunicorn systemd Service

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

### OLS Proxy Configuration

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

Virtual host context:

```apacheconf
# Serve static files directly
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

# Proxy dynamic requests
context / {
  type                    proxy
  handler                 gunicorn
  addDefaultCharset       off
}
```

## Method 3: Reverse Proxy to uWSGI

### Install and Run uWSGI

```bash
pip install uwsgi

uwsgi --http-socket 127.0.0.1:8000 \
  --wsgi-file myproject/wsgi.py \
  --master \
  --processes 4 \
  --threads 2
```

### OLS Configuration

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

## ASGI Applications (Django Channels, FastAPI)

For async Python frameworks, use Uvicorn or Daphne behind OLS.

### FastAPI with Uvicorn

```bash
pip install fastapi uvicorn

uvicorn main:app --host 127.0.0.1 --port 8000 --workers 4
```

### Django Channels with Daphne

```bash
pip install daphne
daphne -b 127.0.0.1 -p 8000 myproject.asgi:application
```

Configure OLS the same way as any reverse proxy:

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

# In vhconf.conf
context / {
  type                    proxy
  handler                 asgi_app
  addDefaultCharset       off
}
```

WebSocket support works automatically through the proxy context.

## Django Static Files

Collect static files and serve them directly through OLS:

```bash
python manage.py collectstatic
```

Ensure `STATIC_ROOT` in Django settings points to a directory OLS can serve:

```python
STATIC_URL = '/static/'
STATIC_ROOT = '/var/www/myproject/static/'
```

## Troubleshooting

**ModuleNotFoundError in LSAPI mode:**
- Set the `PYTHONPATH` environment variable in the extprocessor config
- Ensure the virtual environment is activated or its site-packages are in `PYTHONPATH`

**502 Bad Gateway:**
- Verify the Python process is running
- Check socket permissions for Unix socket connections
- Review OLS error log at `/usr/local/lsws/logs/error.log`

**Static files returning 404:**
- Run `python manage.py collectstatic`
- Verify the OLS context path matches `STATIC_URL`
