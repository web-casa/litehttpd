// @ts-check
import { defineConfig } from "astro/config";
import starlight from "@astrojs/starlight";

export default defineConfig({
  site: "https://litehttpd.com",
  integrations: [
    starlight({
      title: "LiteHTTPD",
      logo: {
        src: "./public/favicon.svg",
      },
      description:
        "Apache .htaccess compatibility module for OpenLiteSpeed",
      defaultLocale: "root",
      locales: {
        root: { label: "English", lang: "en" },
        zh: { label: "简体中文", lang: "zh-CN" },
      },
      social: [
        {
          icon: "github",
          label: "GitHub",
          href: "https://github.com/web-casa/litehttpd",
        },
      ],
      customCss: ["./src/styles/custom.css"],
      head: [
        { tag: "link", attrs: { rel: "icon", type: "image/png", href: "/favicon-96x96.png", sizes: "96x96" } },
        { tag: "link", attrs: { rel: "icon", type: "image/svg+xml", href: "/favicon.svg" } },
        { tag: "link", attrs: { rel: "shortcut icon", href: "/favicon.ico" } },
        { tag: "link", attrs: { rel: "apple-touch-icon", sizes: "180x180", href: "/apple-touch-icon.png" } },
        { tag: "meta", attrs: { name: "apple-mobile-web-app-title", content: "LiteHTTPD" } },
        { tag: "link", attrs: { rel: "manifest", href: "/site.webmanifest" } },
      ],
      components: {
        PageFrame: "./src/components/PageFrame.astro",
        Footer: "./src/components/Footer.astro",
      },
      sidebar: [
        {
          label: "Getting Started",
          translations: { "zh-CN": "快速开始" },
          items: [
            { slug: "getting-started/introduction" },
            { slug: "getting-started/editions" },
            { slug: "getting-started/installation" },
            { slug: "getting-started/configuration" },
            { slug: "getting-started/quick-start" },
          ],
        },
        {
          label: "Compare",
          translations: { "zh-CN": "对比" },
          items: [
            { slug: "compare" },
            { slug: "solutions/wordpress" },
          ],
        },
        {
          label: "OLS Installation",
          translations: { "zh-CN": "OLS 安装" },
          items: [
            { slug: "ols/install-overview" },
            { slug: "ols/install-repository" },
            { slug: "ols/install-docker" },
            { slug: "ols/install-source" },
            { slug: "ols/upgrade" },
          ],
        },
        {
          label: "OLS Configuration",
          translations: { "zh-CN": "OLS 配置" },
          items: [
            { slug: "ols/basic-config" },
            { slug: "ols/virtual-hosts" },
            { slug: "ols/listeners" },
            { slug: "ols/ssl" },
            { slug: "ols/logs" },
            { slug: "ols/custom-errors" },
          ],
        },
        {
          label: "PHP",
          translations: { "zh-CN": "PHP 配置" },
          items: [
            { slug: "ols/php-install" },
            { slug: "ols/php-lsapi" },
            { slug: "ols/php-fpm" },
            { slug: "ols/php-env" },
          ],
        },
        {
          label: "Applications",
          translations: { "zh-CN": "应用部署" },
          items: [
            { slug: "ols/app-wordpress" },
            { slug: "ols/app-laravel" },
            { slug: "ols/app-drupal" },
            { slug: "ols/app-nodejs" },
            { slug: "ols/app-python" },
          ],
        },
        {
          label: "Security",
          translations: { "zh-CN": "安全" },
          items: [
            { slug: "ols/security-overview" },
            { slug: "ols/security-modsecurity" },
            { slug: "ols/security-access-control" },
            { slug: "ols/security-recaptcha" },
            { slug: "ols/security-headers" },
          ],
        },
        {
          label: "Advanced",
          translations: { "zh-CN": "高级功能" },
          items: [
            { slug: "ols/reverse-proxy" },
            { slug: "ols/load-balancing" },
            { slug: "ols/cache" },
            { slug: "ols/compression" },
            { slug: "ols/control-panels" },
          ],
        },
        {
          label: "Directives",
          translations: { "zh-CN": "指令参考" },
          collapsed: true,
          items: [
            { slug: "directives/overview" },
            { slug: "directives/headers" },
            { slug: "directives/access-control" },
            { slug: "directives/authentication" },
            { slug: "directives/rewrite" },
            { slug: "directives/redirects" },
            { slug: "directives/caching" },
            { slug: "directives/content-type" },
            { slug: "directives/environment" },
            { slug: "directives/containers" },
            { slug: "directives/conditionals" },
            { slug: "directives/php" },
            { slug: "directives/options" },
            { slug: "directives/brute-force" },
          ],
        },
        {
          label: "Migration",
          translations: { "zh-CN": "迁移指南" },
          collapsed: true,
          items: [
            { slug: "migration/apache-to-ols" },
            { slug: "migration/ols-to-litehttpd" },
            { slug: "migration/lsws-to-litehttpd" },
            { slug: "migration/confconv" },
            { slug: "migration/wordpress" },
            { slug: "migration/known-differences" },
          ],
        },
        {
          label: "Performance",
          translations: { "zh-CN": "性能" },
          collapsed: true,
          items: [
            { slug: "performance/benchmark" },
            { slug: "performance/compatibility" },
            { slug: "performance/php-tuning" },
          ],
        },
        {
          label: "Development",
          translations: { "zh-CN": "开发指南" },
          collapsed: true,
          items: [
            { slug: "development/architecture" },
            { slug: "development/patches" },
            { slug: "development/building" },
            { slug: "development/testing" },
            { slug: "development/contributing" },
          ],
        },
        {
          label: "Links",
          translations: { "zh-CN": "友情链接" },
          items: [
            { slug: "links" },
          ],
        },
      ],
    }),
  ],
});
