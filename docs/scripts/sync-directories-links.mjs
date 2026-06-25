import { createWriteStream } from 'node:fs';
import { copyFile, mkdir, mkdtemp, readFile, readdir, rm, stat } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { pipeline } from 'node:stream/promises';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';

const repo = process.env.DIRECTORIES_LINKS_REPO ?? 'yeagoo/directories-links';
const ref = process.env.DIRECTORIES_LINKS_REF ?? 'main';
const archiveUrl = `https://codeload.github.com/${repo}/tar.gz/refs/heads/${ref}`;
const requiredArrays = [
  'footer_navigation_sites',
  'authority_documentation_sites',
  'all_friend_links',
];
const logoPathPattern = /^\/assets\/logos\/[A-Za-z0-9][A-Za-z0-9._-]*\.svg$/;
const unsafeSvgPattern = /<\s*script\b|javascript:|[\s<]on[a-z]+\s*=|<\s*foreignObject\b/i;

const scriptDir = path.dirname(fileURLToPath(import.meta.url));
const docsRoot = path.resolve(scriptDir, '..');
const dataPath = path.join(docsRoot, 'src/data/link.json');
const logosPath = path.join(docsRoot, 'public/assets/logos');

async function download(url, target) {
  const response = await fetch(url, {
    headers: {
      'user-agent': 'litehttpd-docs-link-sync',
    },
  });

  if (!response.ok || !response.body) {
    throw new Error(`Failed to download ${url}: ${response.status} ${response.statusText}`);
  }

  await pipeline(response.body, createWriteStream(target));
}

function extract(archivePath, targetDir) {
  const result = spawnSync('tar', ['-xzf', archivePath, '-C', targetDir], {
    encoding: 'utf8',
  });

  if (result.status !== 0) {
    throw new Error(`Failed to extract archive: ${result.stderr || result.stdout}`);
  }
}

async function validateLinks(data, sourceLogosPath) {
  for (const key of requiredArrays) {
    if (!Array.isArray(data[key])) {
      throw new Error(`link.json is missing required array: ${key}`);
    }
  }

  if (data.schema_version !== 2) {
    throw new Error(`Unsupported link.json schema_version: ${data.schema_version}`);
  }

  const logoRefs = new Set();

  for (const key of requiredArrays) {
    data[key].forEach((site, index) => validateSite(site, `${key}[${index}]`, logoRefs));
  }

  for (const logoPath of logoRefs) {
    const sourceLogoPath = path.join(sourceLogosPath, path.posix.basename(logoPath));
    const logoStats = await stat(sourceLogoPath).catch(() => null);

    if (!logoStats?.isFile()) {
      throw new Error(`link.json references a missing logo file: ${logoPath}`);
    }

    await validateLogoFile(sourceLogoPath, logoPath);
  }

  return logoRefs;
}

async function validateLogoFile(sourceLogoPath, logoPath) {
  const content = await readFile(sourceLogoPath, 'utf8');

  if (unsafeSvgPattern.test(content)) {
    throw new Error(`Refusing unsafe SVG logo content: ${logoPath}`);
  }
}

function validateSite(site, label, logoRefs) {
  if (!site || typeof site !== 'object' || Array.isArray(site)) {
    throw new Error(`Invalid site entry at ${label}`);
  }

  if (typeof site.url !== 'string') {
    throw new Error(`Missing URL at ${label}`);
  }

  let parsedUrl;
  try {
    parsedUrl = new URL(site.url);
  } catch {
    throw new Error(`Invalid URL at ${label}: ${site.url}`);
  }

  if (!['http:', 'https:'].includes(parsedUrl.protocol)) {
    throw new Error(`Unsupported URL protocol at ${label}: ${site.url}`);
  }

  if (site.logo_svg == null || site.logo_svg === '') {
    return;
  }

  if (typeof site.logo_svg !== 'string' || !logoPathPattern.test(site.logo_svg)) {
    throw new Error(
      `Invalid logo_svg at ${label}: expected /assets/logos/*.svg, got ${site.logo_svg}`
    );
  }

  logoRefs.add(site.logo_svg);
}

async function copyReferencedLogos(sourceLogosPath, targetLogosPath, logoRefs) {
  await rm(targetLogosPath, { recursive: true, force: true });
  await mkdir(targetLogosPath, { recursive: true });

  for (const logoPath of [...logoRefs].sort()) {
    const fileName = path.posix.basename(logoPath);
    await copyFile(path.join(sourceLogosPath, fileName), path.join(targetLogosPath, fileName));
  }
}

async function main() {
  const workDir = await mkdtemp(path.join(tmpdir(), 'directories-links-'));
  const archivePath = path.join(workDir, 'source.tar.gz');

  try {
    await download(archiveUrl, archivePath);
    extract(archivePath, workDir);

    const entries = await readdir(workDir);
    const sourceDir = entries
      .map((entry) => path.join(workDir, entry))
      .find((entry) => path.basename(entry).startsWith('directories-links-'));

    if (!sourceDir) {
      throw new Error('Downloaded archive did not contain directories-links source directory');
    }

    const sourceDataPath = path.join(sourceDir, 'link.json');
    const sourceLogosPath = path.join(sourceDir, 'assets/logos');
    const raw = await readFile(sourceDataPath, 'utf8');
    const data = JSON.parse(raw);
    const logoRefs = await validateLinks(data, sourceLogosPath);

    await mkdir(path.dirname(dataPath), { recursive: true });
    await mkdir(path.dirname(logosPath), { recursive: true });
    await copyFile(sourceDataPath, dataPath);
    await copyReferencedLogos(sourceLogosPath, logosPath, logoRefs);

    console.log(
      `Synced ${repo}@${ref}: ` +
      `${data.footer_navigation_sites.length} footer links, ` +
      `${data.authority_documentation_sites.length} authority links, ` +
      `${data.all_friend_links.length} total links, ` +
      `${logoRefs.size} logos.`
    );
  } finally {
    await rm(workDir, { recursive: true, force: true });
  }
}

main().catch((error) => {
  console.error(error instanceof Error ? error.message : error);
  process.exit(1);
});
