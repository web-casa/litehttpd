# OLS RPM 构建踩坑记录

> 从动态链接到全静态构建，再到 patch 丢失修复的完整过程

## 问题一览

| # | 问题 | 根因 | 解决方案 |
|---|------|------|---------|
| 1 | EL10 缺 pcre-devel | EL10 移除了 pcre1，只有 pcre2 | 改为全静态编译（从源码构建 pcre-8.45） |
| 2 | 37MB binary vs 官方 9MB | 我们用系统动态库，官方全静态 | 切换到 litespeedtech/third-party 全静态构建 |
| 3 | EL10 编译失败 | OLS 1.8.5 在 EL10 GCC 14 下编译不过 | 用 EL9 容器编译 EL10 RPM（官方也这么做） |
| 4 | RPM 只含 patch 0003 | install.sh 覆盖 patched binary | 保存 patched binary 到 /tmp，install.sh 后恢复 |
| 5 | 恢复后仍被覆盖 | rpmbuild 后处理（brp-strip 等）替换 binary | 禁用 `__spec_install_post` + `__os_install_post` |
| 6 | sed 损坏 binary | `grep -rl | xargs sed -i` 扫描了 ELF 文件 | 排除 ELF 文件，只处理文本文件 |
| 7 | BoringSSL 构建失败 | 缺 golang（被 Codex review 建议移除） | 恢复 golang BuildRequires（BoringSSL 的 cmake 需要 Go） |
| 8 | build_ols.sh sed 静默失败 | 引号/行格式不匹配 | 放宽 sed 模式 + grep 验证 + FATAL 退出 |
| 9 | Dockerfile 中 Python 解析 RPM 失败 | heredoc 被 Dockerfile 解析器截断 | 改用 COPY 预提取的 binary |
| 10 | GLIBC 版本不兼容 | Docker 内 glibc 比 VPS 新 | glibc versionlock（效果有限） |
| 11 | aarch64 WebAdmin 不能启动 | installer 下载上游预编译 admin_php，要求 GLIBC_2.29 | 禁用下载分支，本地编译 PHP 7.4.33 LSAPI 并校验 |
| 12 | 构建期源码漂移/临时下载 | third-party、PHP、LSAPI 依赖在线获取 | release/E2E 先生成 pinned source cache，spec 以 `--with source_cache` 使用并校验 |
| 13 | 发布包包含共享 WebAdmin 密码 | installer 在 rpmbuild 的 BUILDROOT 中生成 `adminpasswd` | 打包前删除 `adminpasswd`，release/E2E 校验其不存在 |

---

## 详细过程

### Phase 1: 从动态链接开始（失败）

最初的 spec 使用系统 `-devel` 包：

```
BuildRequires: pcre-devel openssl-devel expat-devel zlib-devel ...
```

**问题**：
- EL10 没有 `pcre-devel`（pcre1 被移除）
- 产出 binary 37MB（动态链接一堆系统库）
- 跨 EL 版本不兼容（不同 glibc/pcre/openssl）

### Phase 2: 逆向官方 RPM（关键转折）

下载并逆向了 LiteSpeed 官方 EL8/9/10 RPM：

```python
# 用 zstandard 解压 RPM payload
dctx = zstd.ZstdDecompressor()
payload = dctx.decompress(data[idx:], max_output_size=200*1024*1024)
```

**关键发现**：
1. **EL8 和 EL9 binary SHA256 完全相同** — 同一个 binary 打包成两个 RPM
2. **EL10 只是重新链接**，编译器仍是 GCC 9.1.1 (RHEL 8)
3. **全静态链接** — 只依赖 glibc 基础库
4. **BoringSSL 而非 OpenSSL** — 官方从没用系统 OpenSSL
5. **pcre 8.45 静态编译** — 完全不依赖系统 pcre

官方构建链：`litespeedtech/third-party` repo → `build_ols.sh` 编译所有依赖为 `.a` 静态库 → 链接进 OLS binary。

### Phase 3: 切换到全静态构建

修改 spec 的 `%build`：

```rpm
%build
# 先编译所有第三方静态库（pcre, BoringSSL, expat, zlib, brotli...）
cd ..
git clone https://github.com/litespeedtech/third-party.git
cd third-party/script && ./build_ols.sh
cd ../../openlitespeed-%{ols_version}

# 再编译 OLS
bash build.sh
```

去掉所有 `-devel` BuildRequires，只保留编译工具。

### Phase 4: EL10 编译失败

OLS 1.8.5 在 EL10 的 GCC 14 下编译失败。逆向发现**官方也不在 EL10 上编译**——他们在 RHEL 8 构建机上编译一次，然后打包成 EL8/9/10 三个 RPM。

**修复**：release.yml 中 EL10 也用 `almalinux:9` 容器编译。

### Phase 5: RPM 只含 patch 0003（最难的 bug）

构建出的 RPM 安装后：
```
P1 (PHPConfig):  0  ❌
P2 (Rewrite):    0  ❌
P3 (ReadApache): 6  ✅
P4 (Autoindex):  0  ❌
```

**调试过程**：

1. `%prep` 后检查 → 所有 patch 都在源码中 ✅
2. `build.sh` 后检查 → `dist/bin/openlitespeed` 包含所有 patch ✅
3. RPM 解包检查 → patch 丢失 ❌

`install.sh` 和 rpmbuild 后处理都在覆盖 binary。

### Phase 5a: install.sh 覆盖

OLS 的 `install.sh` 从 `dist/` 目录安装文件到 `%{buildroot}`，但它内部可能有自己的 binary copy 逻辑。

**修复**：`install.sh` 之后 `cp -f /tmp/openlitespeed-patched %{buildroot}/usr/local/lsws/bin/openlitespeed`

### Phase 5b: 还是被覆盖

加了 cp 后，`%install` 阶段末尾验证 P1=1 ✅，但 RPM 解包后 P1=0 ❌。

**发现**：`grep -rl "%{buildroot}" %{buildroot} | xargs sed -i` 这行清理脚本会扫描**所有**文件包括 ELF binary，sed 对 binary 文件的修改破坏了 patched binary。

**修复**：排除 ELF 文件：
```bash
grep -rl "%{buildroot}" %{buildroot} | while read f; do
    case "$(file -b "$f")" in ELF*) continue ;; esac
    sed -i "s#%{buildroot}##g" "$f"
done
```

### Phase 5c: 还是被覆盖！

排除了 sed 后仍然 P1=0。

**根因**：rpmbuild 在 `%install` section 结束后、打包前，自动执行 `__spec_install_post` 和 `__os_install_post` 宏。这些宏运行 `brp-strip`（strip ELF binary）、`brp-elfperms` 等后处理脚本，**从 BUILD 目录重新处理了 binary，覆盖了 BUILDROOT 中的 patched 版本**。

**最终修复**：
```rpm
%define debug_package %{nil}
%define __spec_install_post %{nil}
%define __os_install_post %{nil}
```

禁用所有后处理，同时在 `%install` 最后一步恢复 patched binary 并验证。

### 最终 spec 关键部分

```rpm
%build
# 1. 编译 third-party 静态库
cd .. && git clone third-party && build_ols.sh

# 2. 编译 OLS
cd openlitespeed-* && bash build.sh

# 3. 验证 patch 在 binary 中
strings dist/bin/openlitespeed | grep -c "set_php_config_value"

# 4. 保存 patched binary
cp dist/bin/openlitespeed /tmp/openlitespeed-patched

%install
bash install.sh

# 5. 恢复 patched binary（install.sh 会覆盖）
cp -f /tmp/openlitespeed-patched %{buildroot}/usr/local/lsws/bin/openlitespeed

# 6. 最终验证
P1=$(strings %{buildroot}/.../openlitespeed | grep -c "set_php_config_value")
[ "$P1" -ge 1 ] || exit 1  # FATAL if missing

%post
# 7. 自动调优（rewrite enable, indexFiles, PHP workers）
```

---

## 教训总结

1. **rpmbuild 的后处理宏会在你不知道的时候修改文件** — 要么禁用，要么理解 `__spec_install_post` 的完整流程
2. **永远不要对 BUILDROOT 的 binary 文件运行 `sed -i`** — 用 `file` 命令排除 ELF
3. **OLS 的 install.sh 会从 dist/ 重新复制 binary** — 安装后必须覆盖
4. **官方 LiteSpeed 不在 EL10 上编译** — 在旧版本编译，只改 RPM metadata
5. **全静态构建是正确的做法** — 消除跨版本兼容性问题
6. **RPM spec 注释中的 `%install` 会被当作 section 标记** — 不要在注释里写 RPM 关键字
7. **`$变量` 不在 `%build` 和 `%install` 之间共享** — 用 `/tmp` 文件传递
8. **glibc versionlock 在容器里经常不生效** — 需要固定基础镜像版本
9. **WebAdmin admin_php 也是 ABI 边界** — full RPM/tgz 不能只验证 OLS binary/module，也要验证 `admin_php` 和 `lsphp5`

---

## 问题 10 详解：GLIBC 版本不兼容

### 现象

在 AlmaLinux 9 VPS（glibc 2.34）上安装 RPM 后：

```
./litespeed: /lib64/libc.so.6: version `GLIBC_2.35' not found
```

### 根因

CI 的 `almalinux:9` Docker 镜像（latest tag）可能自动拉到 9.5+，其 glibc 为 2.35。
编译出的 binary 引用了 `GLIBC_2.35` 符号，但用户 VPS 可能是 9.2/9.3（glibc 2.34）。

`dnf versionlock` 在 Docker 容器内不可靠——容器创建时 glibc 已经是 2.35，lock 只能防止升级不能降级。

### 官方做法

逆向发现官方 OLS binary 只需要 `GLIBC_2.11`（极低），因为他们在 **RHEL 8**（glibc 2.28）上编译。

```
$ readelf -V official-ols-binary | grep GLIBC_ | sort -uV
GLIBC_2.2.5
GLIBC_2.3
GLIBC_2.7
GLIBC_2.10
GLIBC_2.11
```

### 最终修复

**所有 EL8/9/10 RPM 都在 AlmaLinux 8 容器中编译**（跟官方一致）：

```yaml
# release.yml
matrix:
  include:
    - container: almalinux:8   # glibc 2.28
      dist_tag: el8
    - container: almalinux:8   # same binary, different RPM tag
      dist_tag: el9
    - container: almalinux:8   # same binary, different RPM tag
      dist_tag: el10
```

这样 binary 只需要 `GLIBC_2.28`，在 EL8/9/10 上都能运行。不再需要 versionlock。

---

## 问题 11 详解：aarch64 admin_php GLIBC_2.29

### 现象

OpenLiteSpeed 1.9.1 aarch64 full RPM/tgz 在 EL8 环境中安装后，WebAdmin 依赖的 `admin_php` 可能无法运行：

```
admin_php: /lib64/libc.so.6: version `GLIBC_2.29' not found
```

更严重的是，installer 调用失败后可能继续写入不完整的 WebAdmin 用户文件，导致 `admin/conf/htpasswd` 形如：

```
admin:
```

### 根因

OLS 的 installer 会先尝试下载上游预编译的 WebAdmin PHP：

```
http://www.litespeedtech.com/packages/lsphp7_bin/aarch64/lsphp
```

这个二进制不是在 EL8 ABI 上构建，依赖 `GLIBC_2.29`。同时 `build.sh` 会把真正的 installer 从 `dist/install.sh` 移到 `dist/_in.sh`，再生成一个顶层 `install.sh` 包装脚本；只 patch 顶层 `install.sh` 不够。

### 最终修复

1. 在 `%install` 里同时 patch `install.sh`、`dist/install.sh`、`dist/_in.sh`，把 Linux 预编译下载分支禁用。
2. 修复 `dist/admin/misc/build_admin_php.sh`：
   - `--with-litespeed` 改为 `--enable-litespeed`
   - `--enable-xml` 同时加 `--with-libxml`
   - 移除 `--with-libexpat-dir=/usr`
   - `BUILDCONF_FORCE=Y`
   - 用 `: > ${LOG_FILE}` 替代错误的 `cat /dev/null ${LOG_FILE}`
3. 加入强制校验：
   - `admin/fcgi-bin/admin_php -v` 必须包含 `PHP 7.4.33 (litespeed)`
   - `fcgi-bin/lsphp5 -v` 必须可运行
   - `readelf -V admin_php` 不允许出现 `GLIBC_2.29+`
4. full/patch tgz installer 在启动前检查 `libcrypt.so.1`，EL10 自动安装 `libxcrypt-compat`，Debian/Ubuntu 自动安装 `libcrypt1`。RPM 已通过 spec 的 EL10 `Requires: libxcrypt-compat` 覆盖该依赖。

这类问题只在 full RPM/tgz 的真实 installer 流程中暴露；只把 patched OLS binary/module overlay 到官方 Docker image 会漏掉，因为官方镜像里已经有可用的 `admin_php`。

## 问题 12 详解：构建期 source cache

### 目标

1. Release workflow 和私有 E2E Dockerfile 不再让 RPM spec 临时决定 third-party/PHP/LSAPI 的来源。
2. `third-party` 固定到已验证可构建的提交：
   `7297c534b844d808fce7ec42798ef2b1f3aa5259`
3. WebAdmin PHP 构建源做 SHA256 校验：
   - `php-7.4.33.tar.gz`: `5a2337996f07c8a097e03d46263b5c98d2c8e355227756351421003bea8f463e`
   - `php-litespeed-8.1.tgz`: `aa79814b939a0877d83910e11d21255a801842f0fca0796fde970774f41c296e`

### 实现

`scripts/create-release-source-cache.sh` 在构建前生成：

```
third-party-src.tar.gz
third-party-deps-src.tar.gz
php-7.4.33.tar.gz
php-litespeed-8.1.tgz
```

RPM 构建传入：

```
rpmbuild -bb --with source_cache ...
```

spec 只在 `--with source_cache` 模式声明 Source10/11/12/13。这样 CI 和私有 Docker 构建强制使用缓存源；手工本地 rpmbuild 不带该开关时仍能走 pinned online fallback，便于临时诊断。

### 仍需注意

P2 将 `third-party` 内部会额外下载的 tarball/tree 放进 `third-party-deps-src.tar.gz`，并在 `--with source_cache` 下优先使用本地 cache：

- libaio 0.3.113
- zlib `e3dc0a85b7032e98380dec011bc8f2c2ee0d8fca`
- libbcrypt `55ff64349dec3012cfbbb1c4f92d4dbd46920213`
- PCRE 8.45 / PCRE2 10.45
- PSOL 1.11.33.4
- libxml2 2.15.3
- YAJL 2.1.0
- BoringSSL `9fc1c33e9c21439ce5f87855a6591a9324e569fd`
- curl `curl-7_88_1`
- LMDB `0c8b94738866b1ba7ffac27287fd6cab83f2ade4`
- ModSecurity v3.0.15 及递归子模块 manifest

剩余风险是非 `--with source_cache` 的手工诊断构建仍允许 online fallback；release workflow 和私有 Docker E2E 均应使用 source cache 模式。

### 构建期凭据

上游 `install.sh` 会在安装阶段生成 `/usr/local/lsws/adminpasswd`。在 rpmbuild 中这会落到 BUILDROOT；如果不删除，RPM 和完整 tgz 会携带同一组构建时 WebAdmin 凭据。spec 必须在 `%install` 末尾删除该文件，release workflow 和 E2E Docker builder 也必须检查 `$LSWS/adminpasswd` 不存在。运行时管理员应通过 `admin/misc/admpass.sh` 自行设置 WebAdmin 密码。

## 问题 13 详解：发布前 P0/P1 守卫

1.9.1 发布前新增三类守卫，目标是把“看起来能构建”提升为“发布范围、矩阵和 RPM repo 链路都可审计”。

### 工作区范围

```
bash scripts/check-release-scope.sh
RELEASE_SCOPE_STRICT=1 bash scripts/check-release-scope.sh
bash scripts/check-release-scope.sh --manifest-dir /tmp/litehttpd-release-scope
```

默认模式只报告 dirty worktree 中哪些文件属于 1.9.1 发布范围，哪些属于生成物，哪些是发布范围外变更。真正发布前应使用 `RELEASE_SCOPE_STRICT=1`，确保 docs/public-repo/其它源码变更不会混进 release 提交。
`--manifest-dir` 会额外写出 `in-scope.txt`、`generated.txt`、`out-of-scope.txt` 和 `summary.env`，便于 review 发布提交范围。

### Release matrix

```
bash scripts/check-release-matrix.sh
```

该检查验证 `.github/workflows/release.yml` 至少满足：

- `.so` 覆盖 EL8/EL9/EL10/Ubuntu22 的 x86_64 + aarch64；
- module RPM 覆盖 x86_64 + aarch64；
- full OLS RPM 覆盖 EL8/EL9/EL10 的 x86_64 + aarch64；
- ARM job 有 QEMU；
- OLS RPM 使用 `--with source_cache`；
- release workflow 检查 `adminpasswd`、`phpbuild`、`admin_php` GLIBC ceiling；
- tarball 文件名包含 `${ASSET_ARCH}`；
- RPM repo 发布循环按 EL/arch 生成 repodata 并复制 tgz。
- standalone `.so` release build 使用 `-DBUILD_TESTING=OFF`，避免只构建模块时 FetchContent 拉取测试依赖。

### RPM repo dry-run

```
bash scripts/check-release-artifacts.sh
RELEASE_REQUIRE_FULL_ARTIFACTS=1 bash scripts/check-release-artifacts.sh output artifacts
bash scripts/check-rpm-repo-dry-run.sh
REPO_REQUIRE_FULL_MATRIX=1 bash scripts/check-rpm-repo-dry-run.sh output artifacts
```

`check-release-artifacts.sh` 校验 release asset matrix：

- `.so` 必须覆盖 EL8/EL9/EL10/Ubuntu22 的 x86_64 + aarch64；
- full OLS RPM 必须覆盖 EL8/EL9/EL10 的 x86_64 + aarch64；
- standalone module RPM 必须覆盖 EL9 的 x86_64 + aarch64；
- full/patch/module tgz 必须覆盖 x86_64 + aarch64；
- RPM metadata 的 name/version/release/arch 必须和 spec 及文件名一致；
- RPM/tgz 不允许包含 `adminpasswd` 或 `phpbuild`。

默认会从 `/tmp/ols-p2-x86-artifacts`、`/tmp/ols-p2-artifacts`、`output`、`artifacts` 收集 RPM/tgz，生成本地 repo dry-run：

```
/tmp/litehttpd-rpm-repo-dry-run/repo
```

检查内容：

- RPM 名称必须是 `openlitespeed-litehttpd-*` 或 `litehttpd-*`；
- RPM 必须带 EL dist 和 arch，且 RPM metadata 的 name/version/release/arch 必须和 spec 发布目标及文件名一致；
- RPM file list 不允许包含 `adminpasswd` 或 `phpbuild`；
- tgz 名称必须匹配当前 OLS/module version 和 x86_64/aarch64 arch，且内容不允许包含 `adminpasswd` 或 `phpbuild`；
- `repo/el{8,9,10}/{x86_64,aarch64}` 都生成 repodata；
- root repo 包含 `setup.sh`、GPG key、tgz 和静态 `index.html`；
- `setup.sh` 必须使用 EL/arch aware baseurl，并启用 `repo_gpgcheck=1`。

本地 dry-run 不使用生产 GPG 私钥；真正 release workflow 仍负责对 RPM 和 `repomd.xml` 做生产签名。

### 一键预检

```
bash scripts/quality.sh release-preflight
bash scripts/quality.sh release-ready
bash scripts/quality.sh release-ready-staged
```

`release-preflight` 组合执行 scope report、release matrix、artifact matrix report、patch/directive consistency 和 RPM repo dry-run。发布前硬门禁使用 `release-ready`，它等价于 strict scope + full artifact matrix + full RPM repo matrix：

```
bash scripts/quality.sh release-ready
```

当前工作区如果同时包含文档、公仓同步或实验构建目录等非发布范围变更，不要为了跑门禁回滚它们。使用 staged gate：

```
bash scripts/quality.sh release-ready-staged
```

它会从 `HEAD` 建一个临时干净树，只覆盖 `check-release-scope.sh` 认定的发布范围文件，再在临时树中运行 `release-ready`。这验证的是“发布提交实际会包含的范围”，不会把当前工作区里无关 dirty files 当成发布阻断。

常规 CI 的 blocking gate 也会执行 release matrix 校验，用来防止 release workflow 中 aarch64、source cache、tarball arch 命名或 repo 发布循环被误删。

### 本地补齐发布 artifacts

full OLS RPM 仍以 release workflow + `packaging/openlitespeed-litehttpd.spec` 为权威来源。不要通过重命名或改 metadata 从 EL8 RPM 伪造 EL9/EL10 RPM；EL9/EL10 产物必须由同一 spec 以对应 `dist` tag 产出。

如果需要本地或临时 VPS 上补齐 full OLS RPM matrix，使用同一 spec/source-cache 路径：

```
bash scripts/build-release-ols-artifacts.sh --only el9-aarch64
bash scripts/build-release-ols-artifacts.sh --dist el9 --arch x86_64
```

该脚本复用 `scripts/create-release-source-cache.sh` 生成的 pinned source cache，并在 AlmaLinux 8 容器内用 `--with source_cache` 和对应 `--define "dist .elN"` 跑 `packaging/openlitespeed-litehttpd.spec`。这和 release workflow 的 full OLS RPM job 是同一类构建，不是改名补矩阵。

如果 full tgz 已经存在，但 patch/module tgz 缺失，可以从 full tgz 重新派生：

```
bash scripts/assemble-release-tarballs.sh --require
```

如果需要本地复现 `.so` matrix 和 EL9 standalone module RPM，可以使用 Docker 跑与 release workflow 一致的 module 构建矩阵：

```
bash scripts/build-release-module-artifacts.sh
```

该脚本普通 Docker 连接不可用时会尝试 `sudo -n docker`，输出默认写入 `/tmp/ols-p2-artifacts`，会被 `check-release-artifacts.sh` 和 repo dry-run 默认收集。

`packaging/litehttpd.spec` 的 RPM 构建使用 `-DBUILD_TESTING=OFF`，并在 `%check` 中明确跳过 FetchContent-based tests。原因是 release workflow 已有独立 `test` job 覆盖 `ctest`，RPM build 不应在打包阶段依赖 GitHub 拉取 googletest/rapidcheck。

同一个 spec 也显式清空 `%_lto_cflags`。模块 `.so` 很小，LTO 收益有限；而 EL9 `%optflags` 的 `-flto=auto` 在 emulated release builders 中可能触发 `lto-wrapper`/jobserver 失败，禁用 LTO 能提升发布构建稳定性。
