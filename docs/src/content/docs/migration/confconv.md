---
title: Config Converter (confconv)
description: Convert Apache httpd.conf to OpenLiteSpeed native config
---

`litehttpd-confconv` converts Apache `httpd.conf` and virtual host configurations to OpenLiteSpeed format.

## Usage

```bash
# Convert a single Apache config
litehttpd-confconv --input /etc/httpd/conf/httpd.conf --output /usr/local/lsws/conf/apacheconf/

# With port mapping (Apache 80 -> OLS 8088)
litehttpd-confconv --input /etc/httpd/conf/httpd.conf --output /usr/local/lsws/conf/apacheconf/ portmap=80:8088,443:8443

# Check for changes (for scripting)
litehttpd-confconv --check /etc/httpd/conf/httpd.conf --state /tmp/confconv.state

# Watch mode (auto-recompile on changes)
litehttpd-confconv --watch /etc/httpd/conf/httpd.conf --interval 30 --output /usr/local/lsws/conf/apacheconf/
```

## Output Structure

```
/usr/local/lsws/conf/apacheconf/
  listeners.conf         # Listen directives
  vhosts.conf            # VirtualHost registrations
  vhosts/
    example.com/
      vhconf.conf        # Per-vhost configuration
```

## Supported Apache Directives

Over 60 Apache configuration directives are supported including ServerName, DocumentRoot, VirtualHost, Directory, Redirect, RewriteRule, and more.
