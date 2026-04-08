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
