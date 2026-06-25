# LiteHTTPD Documentation Site

This is the Astro + Starlight documentation site for LiteHTTPD.

## Local Development

```bash
pnpm install
pnpm dev
```

The dev server runs at `http://localhost:4321`.

## Build

```bash
pnpm build
pnpm preview
```

The static site is written to `dist/`.

`pnpm build` runs `scripts/sync-directories-links.mjs` first. This fetches the
latest `link.json` and `assets/logos/` from
`https://github.com/yeagoo/directories-links` so the footer and links page use
fresh directory and documentation-site links for every build.

To update links without building:

```bash
pnpm sync-links
```

## Content Layout

- English docs: `src/content/docs/`
- Simplified Chinese docs: `src/content/docs/zh/`
- Sidebar and site metadata: `astro.config.mjs`
- Shared components: `src/components/`
- Theme overrides: `src/styles/custom.css`

When adding a page, add both English and Chinese versions and register the slug in `astro.config.mjs` if it should appear in the sidebar.

## Maintenance Checks

Before publishing:

```bash
pnpm build
```

Also check that internal links use existing slugs, for example `/directives/overview/` rather than a directory without an index page.
