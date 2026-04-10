---
title: "PHP LSAPI (lsphp)"
description: "了解和配置 OpenLiteSpeed 的 PHP LSAPI。"
---

## 什么是 LSAPI？

LSAPI（LiteSpeed Server Application Programming Interface）是 LiteSpeed 用于与 PHP 通信的专有协议。与 FastCGI 通过套接字将数据序列化为二进制流不同，LSAPI 使用共享内存进行数据传输，减少了系统调用开销和内存拷贝。

### LSAPI 与 FastCGI 对比

| 方面 | LSAPI (lsphp) | FastCGI (PHP-FPM) |
|--------|---------------|-------------------|
| 协议 | 共享内存 + 套接字 | 仅套接字 |
| 进程管理 | OLS 管理工作进程 | PHP-FPM 管理工作进程 |
| 性能 | 更快（更少的拷贝） | 稍慢 |
| 内存 | 每请求开销更低 | 每请求开销更高 |
| PHP opcode 缓存 | 在子进程间共享 | 在 FPM 池内共享 |
| 配置方式 | OLS extprocessor | php-fpm.conf + pool.d |
| suEXEC | 内置支持 | 需要每用户一个池 |

大多数场景下推荐使用 LSAPI。仅当您需要独立的进程管理或与其他 Web 服务器兼容时，才考虑使用 PHP-FPM。

## LSAPI 工作原理

1. OLS 启动 lsphp 二进制文件作为父进程。
2. 父进程 fork 出 `PHP_LSAPI_CHILDREN` 个工作进程。
3. 每个工作进程通过 Unix 域套接字一次处理一个请求。
4. 工作进程处理 `LSAPI_MAX_REQS` 个请求后退出，父进程会 fork 一个替代进程。
5. 空闲超过 `PHP_LSAPI_MAX_IDLE` 秒的工作进程会被终止以回收内存。

## 外部应用程序配置

lsphp 外部应用程序在 `httpd_config.conf` 中定义：

```apacheconf
extprocessor lsphp {
  type                    lsapi
  address                 uds://tmp/lshttpd/lsphp.sock
  maxConns                10
  env                     PHP_LSAPI_CHILDREN=10
  env                     LSAPI_MAX_REQS=5000
  env                     PHP_LSAPI_MAX_IDLE=300
  initTimeout             60
  retryTimeout            0
  persistConn             1
  respBuffer              0
  autoStart               1
  path                    /usr/local/lsws/fcgi-bin/lsphp
  backlog                 100
  instances               1
  priority                0
  memSoftLimit            2047M
  memHardLimit            2047M
  procSoftLimit           1400
  procHardLimit           1500
}
```

### 关键参数

| 参数 | 说明 |
|-----------|-------------|
| `type` | 对于 lsphp 必须设为 `lsapi` |
| `address` | Unix 域套接字路径。必须使用 `uds://` 前缀。 |
| `maxConns` | 最大并发连接数。应与 `PHP_LSAPI_CHILDREN` 匹配。 |
| `autoStart` | 设为 `1` 让 OLS 自动启动 lsphp。 |
| `path` | lsphp 二进制文件的绝对路径。 |
| `instances` | lsphp 父进程数。通常为 `1`。 |
| `memSoftLimit` | 每进程内存软限制。 |
| `memHardLimit` | 每进程内存硬限制。 |

## 脚本处理器

将 PHP 文件扩展名映射到 lsphp 外部应用程序：

```apacheconf
scripthandler {
  add                     lsapi:lsphp  php
}
```

这告诉 OLS 将所有 `.php` 请求路由到 `lsphp` 外部处理器。

## suEXEC 支持

OLS 支持在每个虚拟主机下以不同的用户账户运行 lsphp。在虚拟主机配置中：

```apacheconf
virtualhost example {
  ...
  extprocessor lsphp {
    type                  lsapi
    address               uds://tmp/lshttpd/example_lsphp.sock
    maxConns              5
    env                   PHP_LSAPI_CHILDREN=5
    autoStart             1
    path                  /usr/local/lsws/fcgi-bin/lsphp
    instances             1
  }

  scripthandler {
    add                   lsapi:lsphp  php
  }

  setUIDMode              2
}
```

将 `setUIDMode` 设为 `2`（suEXEC）会使 OLS 以文档根目录所有者的身份启动 lsphp 进程。

## 进程生命周期调优

### 预热

OLS 启动时，会创建 lsphp 父进程，然后按需 fork 子进程。每个子进程的第一个请求会有冷启动开销（加载 PHP、扩展、opcode 缓存）。要预热：

```bash
# 设置子进程立即启动
env PHP_LSAPI_CHILDREN=10
```

### 平滑重启

运行 `systemctl restart lsws` 时，OLS 会发送平滑关闭信号。活动的 PHP 请求会在工作进程退出前完成。新的工作进程由新的父进程 fork 出来。

仅重启 lsphp 而不重启 OLS：

```bash
killall -USR1 lsphp
```

这会使 lsphp 父进程平滑重启所有子进程。

## 故障排查

**lsphp 未启动：**
- 检查配置的 `path` 路径下是否存在二进制文件
- 验证二进制文件具有执行权限
- 检查 `/usr/local/lsws/logs/stderr.log` 中的 PHP 启动错误

**502 Bad Gateway：**
- lsphp 进程崩溃或未运行
- 检查 `maxConns` 是否与 `PHP_LSAPI_CHILDREN` 匹配
- 查看 `/usr/local/lsws/logs/error.log`

**内存使用过高：**
- 减少 `PHP_LSAPI_CHILDREN`
- 降低 `LSAPI_MAX_REQS` 以更频繁地回收工作进程
- 检查应用程序代码中的 PHP 内存泄漏

## 下一步

- [PHP 环境变量](/zh/ols/php-env/) -- 调优工作进程行为
- [安装 PHP](/zh/ols/php-install/) -- 安装其他 PHP 版本
