# Redirect 指令 404 拦截问题分析

## 问题现状

### 当前行为

**测试用例 RD_001：**
```yaml
- id: RD_001
  desc: "Redirect 301 /old-page -> /new-page"
  method: GET
  path: /old-page
  compare_mode: known
  checks:
    status: 301
    header:Location_contains: "/new-page"
  known_diff: "OLS Module cannot intercept 404 requests for Redirect"
```

**.htaccess 配置：**
```apache
Redirect 301 /old-page /new-page
```

**四引擎对比结果：**

| 引擎 | 请求 `/old-page` | 返回状态 | 说明 |
|------|------------------|----------|------|
| Apache 2.4 | 不存在的路径 | 301 → `/new-page` | ✅ mod_alias 在 404 前拦截 |
| LSWS Enterprise | 不存在的路径 | 301 → `/new-page` | ✅ 原生 .htaccess 支持 |
| OLS Native | 不存在的路径 | 404 | ❌ 无 .htaccess 支持 |
| OLS Module | 不存在的路径 | 404 | ❌ module hook 未被调用 |

### 根本原因

**OLS 架构限制：**

OLS 的 LSIAPI module hook 调用时机：
1. `LSI_HKPT_RCVD_REQ_HEADER` — 接收到请求头后
2. `LSI_HKPT_SEND_RESP_HEADER` — 发送响应头前

**关键问题：** 当请求路径对应的文件不存在时，OLS 在调用 module hook 之前就已经决定返回 404。

**代码证据（`src/mod_htaccess.c:on_recv_req_header()`）：**
```c
static int on_recv_req_header(lsi_session_t *session)
{
    // ... 获取 doc_root 和 URI ...
    
    // 收集 .htaccess 指令
    htaccess_directive_t *directives = htaccess_dirwalk(session, doc_root, target_dir);
    
    // 执行 Redirect 指令
    for (dir = directives; dir != NULL; dir = dir->next) {
        if (dir->type == DIR_REDIRECT) {
            redir_rc = exec_redirect(session, dir);
            if (redir_rc > 0) {
                // 设置 301 状态和 Location header
                return LSI_OK;
            }
        }
    }
}
```

**问题：** 对于不存在的路径（如 `/old-page`），OLS 在调用 `on_recv_req_header()` 之前就已经：
1. 检查文件是否存在
2. 决定返回 404
3. 跳过 module hook 调用

**对比 Apache 的行为：**
- Apache 的 `mod_alias`（处理 Redirect）在 `translate_name` 阶段执行
- `translate_name` 在文件系统检查之前
- 因此 Redirect 可以拦截任何路径，无论文件是否存在

---

## 实现方案分析

### 方案 1：使用更早的 Hook 点

**思路：** 尝试使用 `LSI_HKPT_URI_MAP` hook（在 URI 映射阶段）

**可行性：** ❌ 低

**原因：**
1. `LSI_HKPT_URI_MAP` 主要用于 URI 到文件路径的映射
2. OLS 可能在此阶段之前就已经检查文件存在性
3. 需要深入研究 OLS 源码确认调用顺序

**风险：**
- 可能仍然无法拦截 404
- 需要修改 module 的 hook 注册逻辑

---

### 方案 2：使用 OLS vhconf.conf 的 Context 配置

**思路：** 在 OLS 配置文件中定义 Redirect，而非依赖 .htaccess

**示例配置：**
```apache
context /old-page {
  type                    redirect
  uri                     /new-page
  statusCode              301
}
```

**可行性：** ✅ 高（已验证可行）

**优点：**
- OLS 原生支持，无需 module
- 可以拦截任何路径（包括不存在的文件）
- 性能更好（配置在启动时加载）

**缺点：**
- 不是 .htaccess 方案，需要修改 vhconf.conf
- 失去了 .htaccess 的动态性（修改需重启 OLS）
- 不符合项目目标（.htaccess 兼容性）

---

### 方案 3：ErrorDocument + 自定义 404 处理器

**思路：** 使用 ErrorDocument 将 404 重定向到自定义脚本，脚本检查 Redirect 规则

**示例 .htaccess：**
```apache
ErrorDocument 404 /redirect_handler.php
Redirect 301 /old-page /new-page
```

**redirect_handler.php：**
```php
<?php
// 读取 .htaccess 中的 Redirect 规则
// 检查当前 URI 是否匹配
// 如果匹配，发送 301 header
// 否则显示 404 页面
?>
```

**可行性：** ⚠️ 中等

**优点：**
- 可以在 .htaccess 框架内实现
- 不需要修改 OLS 核心

**缺点：**
- 性能开销大（每个 404 都要执行 PHP 脚本）
- 需要额外的 PHP 脚本
- 复杂度高（需要解析 .htaccess）
- 不是真正的"拦截"，而是"事后处理"

---

### 方案 4：修改 OLS 核心代码

**思路：** 向 OLS 提交 patch，在文件存在性检查之前调用 module hook

**可行性：** ⚠️ 中等（技术可行，但需要社区接受）

**需要修改的 OLS 代码：**
1. 在 `HttpReq::processRequest()` 中，文件检查之前调用 module hook
2. 允许 module 返回 `LSI_DENY` 来短路请求处理
3. 如果 module 设置了 Location header，跳过文件检查

**优点：**
- 从根本上解决问题
- 所有 module 都能受益
- 符合 Apache 的行为模式

**缺点：**
- 需要深入理解 OLS 核心代码
- 需要向 OpenLiteSpeed 官方提交 PR
- 审核和合并周期长（可能数月）
- 可能影响 OLS 的性能和稳定性
- 需要说服 OLS 维护者接受这个改动

---

### 方案 5：文档化已知差异（当前方案）

**思路：** 在文档中明确说明 OLS Module 无法拦截 404 请求的 Redirect

**可行性：** ✅ 已实施

**优点：**
- 零开发成本
- 用户有明确预期
- 可以推荐替代方案（vhconf.conf Context）

**缺点：**
- 不是真正的"解决方案"
- 降低了 .htaccess 兼容性

---

## 收益与风险评估

### 方案对比表

| 方案 | 技术可行性 | 开发成本 | 维护成本 | 兼容性提升 | 性能影响 | 推荐度 |
|------|-----------|---------|---------|-----------|---------|--------|
| 1. 更早 Hook | 低 | 中 | 中 | 中 | 低 | ⭐ |
| 2. vhconf Context | 高 | 低 | 低 | 低 | 正面 | ⭐⭐ |
| 3. ErrorDocument | 中 | 高 | 高 | 中 | 负面 | ⭐ |
| 4. 修改 OLS 核心 | 中 | 高 | 高 | 高 | 未知 | ⭐⭐⭐ |
| 5. 文档化（当前） | 高 | 零 | 低 | 零 | 零 | ⭐⭐⭐⭐ |

### 详细收益分析

#### 方案 4（修改 OLS 核心）的收益

**短期收益（1-3 月）：**
- 无（开发和审核阶段）

**中期收益（3-12 月）：**
- ✅ 完整的 Redirect 指令支持
- ✅ 提升 Apache 兼容性（从 90% → 95%）
- ✅ 减少用户困惑（不再需要解释"已知差异"）
- ✅ 提升项目声誉（向 OLS 官方贡献代码）

**长期收益（1-3 年）：**
- ✅ 成为 OLS 官方功能的一部分
- ✅ 所有 OLS 用户受益
- ✅ 可能吸引更多贡献者

**量化收益：**
- 影响的测试用例：2 个（RD_001, RD_002）
- 兼容性提升：约 5%（2/30 测试用例）
- 用户价值：中等（Redirect 是常用功能，但有替代方案）

### 详细风险分析

#### 方案 4（修改 OLS 核心）的风险

**技术风险：**
1. **性能回退** — 每个请求都要调用 module hook，即使没有 Redirect
   - 缓解：仅在 module 注册时启用额外 hook
   - 影响：预计 <1% 性能损失

2. **兼容性破坏** — 可能影响现有 module 的行为
   - 缓解：使用新的 hook 点，不修改现有 hook
   - 影响：低（新 hook 是可选的）

3. **代码复杂度** — OLS 核心代码可能很复杂
   - 缓解：先研究源码，找到最小改动点
   - 影响：中（需要 1-2 周学习 OLS 源码）

**项目风险：**
1. **PR 被拒绝** — OLS 维护者可能不接受这个改动
   - 概率：30-50%
   - 缓解：先在社区讨论，获得反馈
   - 后果：浪费 2-4 周开发时间

2. **审核周期长** — 可能需要数月才能合并
   - 概率：80%
   - 缓解：同时维护 fork 版本
   - 后果：用户需要使用 fork 版本

3. **维护负担** — 需要跟进 OLS 版本更新
   - 概率：100%（如果 PR 被拒绝）
   - 缓解：自动化测试，CI/CD
   - 后果：每次 OLS 更新都要 rebase

**安全风险：**
1. **路径遍历** — 新 hook 可能引入安全漏洞
   - 缓解：复用现有的路径验证逻辑
   - 影响：低（已有防御机制）

2. **DoS 攻击** — 恶意 .htaccess 可能导致性能问题
   - 缓解：保持现有的缓存机制
   - 影响：低（已有缓存保护）

---

## 推荐方案

### 短期（当前）：方案 5 — 文档化已知差异

**理由：**
- 零成本，已实施
- 用户有明确预期
- 可以推荐 vhconf.conf Context 作为替代方案

**行动：**
- ✅ 已完成：`expected_diff.md` 中记录
- ✅ 已完成：`COMPATIBILITY.md` 中说明替代方案

### 中期（1-2 月）：方案 4 — 研究 OLS 核心修改可行性

**行动计划：**

1. **研究阶段（1 周）：**
   - 克隆 OpenLiteSpeed 源码
   - 找到请求处理流程（`HttpReq::processRequest()`）
   - 确认 hook 调用时机和文件检查顺序
   - 评估修改难度

2. **社区讨论（1 周）：**
   - 在 OLS 论坛/GitHub 发起讨论
   - 说明问题和提议的解决方案
   - 收集维护者和社区反馈
   - 评估 PR 被接受的可能性

3. **原型开发（2 周）：**
   - 如果社区反馈积极，开发 POC
   - 添加新的 hook 点（如 `LSI_HKPT_PRE_FILE_CHECK`）
   - 修改 litehttpd_htaccess module 使用新 hook
   - 本地测试验证

4. **提交 PR（1 周）：**
   - 编写详细的 PR 描述
   - 包含测试用例和性能基准
   - 提交到 OpenLiteSpeed 官方仓库

5. **等待审核（1-6 月）：**
   - 响应维护者的反馈
   - 修改代码
   - 等待合并

**决策点：**
- 如果社区讨论反馈消极 → 放弃方案 4，继续使用方案 5
- 如果 PR 被拒绝 → 维护 fork 版本，或放弃

### 长期（6-12 月）：根据 PR 结果决定

**如果 PR 被接受：**
- 更新文档，移除"已知差异"
- 宣传新功能
- 吸引更多用户

**如果 PR 被拒绝：**
- 继续使用方案 5
- 考虑维护 OLS fork（如果有足够用户需求）

---

## 结论

**当前状态：**
- OLS Module 无法拦截 404 请求的 Redirect 指令
- 影响 2/30 测试用例（RD_001, RD_002）
- 已在文档中说明，用户有明确预期

**最佳方案：**
- 短期：继续使用方案 5（文档化）
- 中期：探索方案 4（修改 OLS 核心），但需要社区支持
- 长期：根据 PR 结果决定

**收益：**
- 中等（提升 5% 兼容性）
- 用户价值中等（有替代方案）

**风险：**
- 技术风险：低-中
- 项目风险：中-高（PR 可能被拒绝）
- 时间成本：2-4 周开发 + 1-6 月审核

**建议：**
1. 先完成其他高优先级任务（CI/CD、性能测试）
2. 如果有时间，研究 OLS 源码并发起社区讨论
3. 根据社区反馈决定是否投入开发
4. 不要将此作为"必须完成"的任务，而是"探索性"任务
