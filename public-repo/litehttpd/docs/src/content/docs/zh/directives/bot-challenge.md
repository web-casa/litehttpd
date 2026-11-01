---
title: Bot Challenge（人机挑战）
description: 自托管的 JS 工作量证明人机验证门
---

`BotChallenge` 是 Cloudflare Turnstile / OLS Enterprise "managed
challenge" 的全自托管替代：没有有效凭证 cookie 的客户端会收到一个
内联 HTML+JS 挑战页，在浏览器里解一个轻量工作量证明（PoW）后获得
签名 cookie。不依赖任何外部服务、不需要 JS 框架，浏览器开启
JavaScript 即可通过——而这恰恰是绝大多数爬虫、撞库脚本和 AI
抓取器所没有的。

## 指令

| 指令 | 语法 |
|------|------|
| `BotChallenge` | `On \| Off` |
| `BotChallengeTTL` | `seconds`（默认 `3600`，下限 `60`） |
| `BotChallengeDifficulty` | `bits`（默认 `16`，范围 `8`–`24`） |
| `BotChallengeExempt` | `"regex"`（对 User-Agent 匹配豁免） |
| `BotChallengeTrustedProxy` | `"cidr列表"` — 信任代理时按 XFF 绑定 |
| `BotChallengePage` | `模板.html` — 自定义挑战页 |
| `BotChallengeIf` | `"ap_expr"` — 按请求条件启用门 |

## 示例

```apache
BotChallenge On
BotChallengeDifficulty 20
BotChallengeExempt "(?i)(googlebot|bingbot)"
```

- 难度 16 对浏览器只需几毫秒；20 会把每次求解的哈希次数提高约
  30 倍——是对付激进爬虫的低成本旋钮。
- 豁免希望畅通抓取的已验证搜索引擎 UA。（需要更强验证时，可自行
  叠加 `RewriteCond %{HTTP_USER_AGENT}` + 反向 DNS 类规则。）

## 工作原理

1. 请求到达时没有有效的 `lhttpd_bot` cookie（且 UA 不在豁免名单）：
   模块返回内联挑战页——HTTP 200、`Cache-Control: no-store`——其中
   包含每日盐值和难度值。
2. 页面里的 JavaScript 对 `salt:nonce` 循环 FNV-1a（与 C 端校验器
   完全相同的 32 位哈希），直到高位连续 *difficulty* 个 0，然后跳转
   `<uri>?__lhc=<nonce>`。
3. 该请求到达门上：一次性校验 nonce，签发 HMAC-SHA256 凭证
   cookie，随后 **302 跳转到净化后的 URL**（去掉 `__lhc` 参数、保留
   其余参数）。浏览器带着新 cookie 重新请求并落在规范、可缓存的地址
   —— 带 `?__lhc=...` 的免凭证请求永远不会产生可缓存的 200。
4. 后续请求校验有效期 + MAC（常量时间比较）。凭证绑定客户端 IP
   段（IPv4 `/24`、IPv6 前 4 组），cookie 被盗后也难以跨网段复用。

## 运维要点

- **共享密钥**：`/var/run/litehttpd/botchallenge.secret`（0600，首次
  使用时自动创建，全部 worker 共享，cookie 因此可跨 worker 校验）。
  可用环境变量 `LITEHTTPD_BOT_SECRET` 覆盖路径。
- RPM 与 tarball 安装都会把 `/run/litehttpd` 预置为 OLS worker 用户
  可写（tmpfiles.d 负责重启后重建）。若密钥既无法读取也无法创建，
  挑战门仍会以较弱的密钥继续工作并记录一条警告——修复目录权限即可。
- 轮换密钥会使所有 cookie 失效；访问者只是再多解一次挑战。
- 调参：`TTL` 在便利性与重放窗口之间权衡；`Difficulty` 提高所有人
  （包括你自己）的单次请求成本。

## 反向代理 / CDN 之后

默认凭证绑定**直连对端** —— 在 Cloudflare 或 Nginx 代理后面，所有
访客共享代理的 IP 段，绑定会失效。`BotChallengeTrustedProxy` 解决
这个问题：

```apache
BotChallenge On
BotChallengeTrustedProxy "10.0.0.0/8 192.168.0.0/16"
```

当且仅当直连对端在所列 CIDR 内，凭证才绑定 `X-Forwarded-For` 的
**最右** token —— 即你的代理追加的那个地址。最左（客户端自报）的
条目可伪造、一律不用；不在名单内的对端发来的 XFF 完全忽略。
仅支持 IPv4 CIDR。

## 条件触发

`BotChallengeIf` 接受 ap_expr 表达式，按请求求值，成立时才启用
挑战门。结合 GeoIP 通路即可按国家圈定：

```apache
BotChallenge On
# 除美英以外的流量才要求过挑战
BotChallengeIf "%{ENV:GEOIP_COUNTRY_CODE} !~ /^(US|GB)$/"
```

任意表达式可用 —— URL、请求头、环境变量。表达式解析失败时
**fail-open**（门关闭）并在错误日志告警：配置写错绝不锁死真人。

## 自定义挑战页

`BotChallengePage` 用你自己的模板替代内置页 —— 加品牌、加 CSS、
做多语言：

```apache
BotChallengePage challenge.html
```

路径相对 `.htaccess` 所在目录（绝对路径必须解析到该目录内；
不跟随符号链接 —— 与 `AuthUserFile` 同族围堵）。模板可用
`%{SALT}`、`%{DIFF}`、`%{URI}` 三个变量；URI 在替换前做过 JS
转义。模板必须执行同款 FNV-1a 循环并跳转到
`%{URI}?__lhc=<nonce>` —— 可直接拷贝内置页的 `<script>` 块起步。
模板缺失或无效时回退内置页。

## 轮换密钥

`tools/rotate-botsecret.sh`（随 tarball / 仓库提供）原子替换密钥并
平滑重启 OLS 让 worker 重新加载。所有存量 cookie 随之失效；访客
只是再多解一次挑战。

## 与缓存的交互

挑战页带 `Cache-Control: no-store`，且对豁免 UA 根本不会产生挑战
响应，因此前置的反向代理或 LSCache 不会把挑战页缓存给真实用户。
