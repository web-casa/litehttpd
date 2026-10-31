---
title: ModSecurity 桥接
description: 用 SecRuleEngine 实现目录级 WAF 控制 —— 将 LSWS 企业版特性带到 OpenLiteSpeed
---

`.htaccess` 中的 `SecRuleEngine` 提供目录级 ModSecurity 控制——这是
LiteSpeed 企业版具备而原版 OpenLiteSpeed 缺少的能力。LiteHTTPD 将其桥接到
OLS 的 `mod_security` 模块。

## 指令

| 指令 | 取值 | 效果 |
|------|------|------|
| `SecRuleEngine` | `On` | 无动作——引擎保持服务器配置的状态（默认） |
| `SecRuleEngine` | `Off` | 该目录下的所有请求跳过 WAF |
| `SecRuleEngine` | `DetectionOnly` | libmodsecurity 不支持逐请求设置；记录警告并按 `On` 处理（向保护侧失败） |

## 示例

为某个触发规则的旧插件端点关闭 WAF：

```apache
# /wp-content/plugins/legacy-form/.htaccess
SecRuleEngine Off
```

该目录之外的一切保持完整 WAF 检测。

## 工作原理

OLS 自带的 `mod_security` 模块在请求环境变量 `modsecurity` 等于 `off`
时完全跳过该请求——与 OLS 缓存模块处理缓存命中用的是同一机制。LiteHTTPD
在**早于** WAF 钩子的 URI 钩子上设置该变量，因此旁路必然赢得竞争。

合并遵循 Apache 目录语义：目录链中最深一级的 `SecRuleEngine` 生效
（子目录 `On` 覆盖父目录 `Off`）。

## 已验证行为

在 OLS 1.9.4 + libmodsecurity 3.0.15 + 真实 SQLi 规则下：

| 请求 | 结果 |
|------|------|
| `/?s=1' OR '1'='1`（无指令） | **403** —— 规则拦截 |
| 同一请求在 `SecRuleEngine Off` 目录下 | **200** —— 旁路生效 |

## 限制

- `SecRuleRemoveById` 与目录级规则排除在 libmodsecurity 中无逐请求接口，未被桥接。
- 需要 OLS 已安装并启用 `mod_security` 模块；未安装时指令被接受但无可旁路对象。
