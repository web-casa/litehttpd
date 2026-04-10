---
title: "reCAPTCHA"
description: "Server-level reCAPTCHA anti-DDoS protection on OpenLiteSpeed"
---

## Overview

OpenLiteSpeed includes a built-in reCAPTCHA feature that presents a CAPTCHA challenge to suspicious clients at the server level. Unlike application-level CAPTCHA, this works before any request reaches PHP or your application, making it effective against DDoS attacks and brute force bots.

## How It Works

1. OLS monitors per-client request rates using the per-client throttling system.
2. When a client exceeds the configured thresholds, instead of immediately blocking, OLS serves a reCAPTCHA challenge page.
3. If the client solves the CAPTCHA, a validation cookie is set, and subsequent requests proceed normally.
4. If the client fails or ignores the CAPTCHA, requests continue to be challenged.

This approach is more user-friendly than outright banning, since legitimate users behind shared IPs (NAT, VPN, corporate networks) can pass the challenge.

## Configure via WebAdmin

1. Navigate to **Server Configuration > Security > reCAPTCHA**
2. Configure the following settings:

| Setting | Value | Description |
|---------|-------|-------------|
| **Enable reCAPTCHA** | Yes | Master switch |
| **reCAPTCHA Type** | Checkbox (v2) or Invisible (v2) | Checkbox shows a visible challenge; Invisible only challenges suspicious behavior |
| **Site Key** | (from Google) | Your reCAPTCHA site key |
| **Secret Key** | (from Google) | Your reCAPTCHA secret key |
| **Max Tries** | 3 | Failed attempts before triggering CAPTCHA |
| **Allowed Robot Hits** | 5 | Requests per 10 seconds before triggering |
| **Bot White List** | (User-Agent patterns) | Whitelist known good bots |
| **Connection Limit** | 100 | Per-IP connections triggering CAPTCHA |

3. Save and perform a graceful restart.

## Obtain reCAPTCHA Keys

1. Go to [Google reCAPTCHA Admin](https://www.google.com/recaptcha/admin)
2. Register a new site
3. Choose **reCAPTCHA v2** (Checkbox or Invisible)
4. Add your domain(s)
5. Copy the **Site Key** and **Secret Key**

:::note
OLS uses reCAPTCHA v2. reCAPTCHA v3 (score-based) is not supported by the built-in integration.
:::

## Configure via Configuration File

In `httpd_config.conf`:

```apacheconf
security {
  reCAPTCHA {
    enabled               1
    type                  0
    siteKey               your_site_key_here
    secretKey             your_secret_key_here
    maxTries              3
    allowedRobotHits      5
    botWhiteList          Googlebot, Bingbot, baiduspider
    connLimit             100
    regConnLimit          15000
  }
}
```

### Parameters

| Parameter | Values | Description |
|-----------|--------|-------------|
| `enabled` | `0` / `1` | Enable or disable |
| `type` | `0` (checkbox) / `1` (invisible) | reCAPTCHA variant |
| `siteKey` | string | Google reCAPTCHA site key |
| `secretKey` | string | Google reCAPTCHA secret key |
| `maxTries` | integer | Failed attempts before CAPTCHA |
| `allowedRobotHits` | integer | Requests per 10 sec before triggering |
| `botWhiteList` | comma-separated | User-Agent strings to whitelist |
| `connLimit` | integer | Per-IP connections triggering CAPTCHA for unknown visitors |
| `regConnLimit` | integer | Per-IP connections triggering CAPTCHA for returning/validated visitors |

## Per-Virtual Host Override

You can enable reCAPTCHA for specific virtual hosts only:

```apacheconf
virtualhost example {
  ...
  security {
    reCAPTCHA {
      enabled             1
      type                1
      siteKey             your_site_key_here
      secretKey           your_secret_key_here
    }
  }
}
```

## Bot Whitelist

Legitimate search engine bots should bypass reCAPTCHA. The `botWhiteList` setting accepts User-Agent substrings:

```apacheconf
botWhiteList          Googlebot, Bingbot, baiduspider, YandexBot, DuckDuckBot, Slurp
```

OLS matches these strings case-insensitively against the `User-Agent` header.

## Testing

1. Set `allowedRobotHits` to `1` temporarily to trigger reCAPTCHA quickly.
2. Open your site in a browser and refresh rapidly.
3. You should see the reCAPTCHA challenge page.
4. Solve it and verify that subsequent requests proceed normally.
5. Reset `allowedRobotHits` to a production-appropriate value.

## Troubleshooting

**reCAPTCHA page not appearing:**
- Verify `enabled` is set to `1`
- Check that `siteKey` and `secretKey` are correct
- Ensure the domain is registered in the Google reCAPTCHA admin console

**Legitimate users getting CAPTCHA too often:**
- Increase `allowedRobotHits` and `connLimit`
- Check if users are behind a shared IP (NAT/VPN) and increase `regConnLimit`

**Search engine bots being challenged:**
- Add the bot's User-Agent string to `botWhiteList`
- Verify with `curl -A "Googlebot" https://example.com` that the bot bypasses the challenge
