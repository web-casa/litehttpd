---
title: "Docker Deployment"
description: "Deploy OpenLiteSpeed using Docker with PHP and database support."
---

LiteSpeed provides official Docker images on Docker Hub. The images include lsphp83 pre-installed.

## Quick Start

Pull and run the latest image:

```bash
docker pull litespeedtech/openlitespeed:latest
docker run -d --name ols -p 80:80 -p 443:443 -p 7080:7080 litespeedtech/openlitespeed:latest
```

Access the default site at `http://localhost` and WebAdmin at `https://localhost:7080`.

## Docker Compose with PHP and MariaDB

Create a `docker-compose.yml` file:

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

Start the stack:

```bash
docker compose up -d
```

## Volume Mounts

| Host Path | Container Path | Purpose |
|---|---|---|
| `./sites/` | `/var/www/vhosts/` | Document roots for virtual hosts |
| `./config/httpd_config.conf` | `/usr/local/lsws/conf/httpd_config.conf` | Main server configuration |
| `./config/vhosts/` | `/usr/local/lsws/conf/vhosts/` | Virtual host configuration files |
| `./logs/` | `/usr/local/lsws/logs/` | Server error and access logs |

## Environment Variables

The official image supports the following environment variables:

| Variable | Default | Description |
|---|---|---|
| `TZ` | `UTC` | Container timezone |

For WebAdmin credentials in Docker, run the password script inside the container:

```bash
docker exec -it ols /usr/local/lsws/admin/misc/admpass.sh
```

## Custom Dockerfile

To add extensions or customize the image:

```dockerfile
FROM litespeedtech/openlitespeed:latest

# Install additional PHP extensions
RUN apt-get update && apt-get install -y \
    lsphp83-curl \
    lsphp83-intl \
    lsphp83-json \
    lsphp83-redis \
    && rm -rf /var/lib/apt/lists/*

# Copy custom config
COPY config/httpd_config.conf /usr/local/lsws/conf/httpd_config.conf
COPY sites/ /var/www/vhosts/
```

Build and run:

```bash
docker build -t my-ols .
docker run -d --name ols -p 80:80 -p 443:443 -p 7080:7080 my-ols
```

## Managing the Container

```bash
# View logs
docker logs ols

# Restart OLS inside the container
docker exec ols /usr/local/lsws/bin/lswsctrl restart

# Enter the container shell
docker exec -it ols bash

# Stop and remove
docker compose down
```

## Next Steps

- [Basic Configuration](/ols/basic-config/) -- understand the config file structure.
- [Virtual Hosts](/ols/virtual-hosts/) -- configure sites inside the container.
- [SSL / TLS](/ols/ssl/) -- set up certificates for HTTPS.
