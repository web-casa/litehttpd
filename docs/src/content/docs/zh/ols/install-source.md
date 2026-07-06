---
title: "从源码编译"
description: "使用自定义选项从源码编译 OpenLiteSpeed。"
---

当您需要自定义编译选项、应用补丁（如 LiteHTTPD LSIAPI 补丁）或面向非标准平台时，从源码编译非常有用。

## 前置条件

安装所需的编译依赖。

### AlmaLinux / Rocky Linux 9

```bash
dnf groupinstall "Development Tools"
dnf install gcc gcc-c++ cmake pcre-devel openssl-devel expat-devel zlib-devel
```

### Ubuntu / Debian

```bash
apt update
apt install build-essential gcc g++ cmake libpcre3-dev libssl-dev libexpat1-dev zlib1g-dev git
```

## 下载并编译

```bash
git clone https://github.com/litespeedtech/openlitespeed.git
cd openlitespeed
bash build.sh
```

`build.sh` 脚本会处理 `cmake` 配置和编译过程，构建服务器二进制文件和所有内置模块。

## 安装

编译成功后，安装服务器：

```bash
cd dist
bash install.sh
```

这会将 OLS 安装到 `/usr/local/lsws/`，目录结构与软件包安装相同。

## 编译选项

`build.sh` 脚本接受多个环境变量：

```bash
# 使用调试符号编译
XTRABUILDFLAGS="-g -O0" bash build.sh

# 指定自定义 OpenSSL 路径
CFLAGS="-I/opt/openssl/include" LDFLAGS="-L/opt/openssl/lib" bash build.sh
```

如需更精细的控制，可以直接运行 cmake：

```bash
mkdir build && cd build
cmake .. \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr/local/lsws
make -j$(nproc)
```

## 应用 LiteHTTPD 补丁

如果您使用 LiteHTTPD 模块（`ols_htaccess.so`）且需要自定义 OLS 支持的功能（PHPConfig 透传、LSIAPI Rewrite），请在编译前应用补丁：

```bash
cd openlitespeed
git apply /path/to/patches/0001-lsiapi-phpconfig.patch
git apply /path/to/patches/0002-lsiapi-rewrite.patch
bash build.sh
cd dist && bash install.sh
```

请参阅 [LiteHTTPD 指令参考](/zh/directives/overview/) 了解哪些指令需要自定义 OLS 编译版本，哪些可以在原版 OLS 上使用。

## 安装后配置

编译安装完成后，按照与仓库安装相同的步骤进行配置：

```bash
# 设置管理员密码
/usr/local/lsws/admin/misc/admpass.sh

# 启动服务器
/usr/local/lsws/bin/lswsctrl start

# 开机自启（如需要，创建 systemd 服务单元）
```

WebAdmin 管理界面可通过 `https://your-server-ip:7080` 访问。

## 下一步

- [从仓库安装](/zh/ols/install-repository/) -- 如果您更倾向使用预编译包。
- [升级与降级](/zh/ols/upgrade/) -- 管理后续版本更新。
- [基础配置](/zh/ols/basic-config/) -- 配置服务器。
