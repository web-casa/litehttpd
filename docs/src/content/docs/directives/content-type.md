---
title: Content Type
description: MIME type and content configuration directives
---

## Directives

| Directive | Syntax |
|-----------|--------|
| `AddType` | `AddType MIME-type extension [extension ...]` |
| `AddEncoding` | `AddEncoding encoding extension [extension ...]` |
| `AddCharset` | `AddCharset charset extension [extension ...]` |
| `AddDefaultCharset` | `AddDefaultCharset charset\|Off` |
| `DefaultType` | `DefaultType MIME-type` |
| `ForceType` | `ForceType MIME-type` |
| `DirectoryIndex` | `DirectoryIndex filename [filename ...]` |
| `RemoveType` | `RemoveType extension [extension ...]` |
| `RemoveHandler` | `RemoveHandler extension [extension ...]` |

## Examples

### Custom MIME Types

```apache
AddType application/json .json5
AddType application/wasm .wasm
AddType font/woff2 .woff2
```

### Character Set

```apache
AddCharset UTF-8 .html .css .js
AddDefaultCharset UTF-8
```

### Directory Index

```apache
DirectoryIndex index.php index.html index.htm
```

### Force Content Type

```apache
# Force all files in a directory to be served as plain text
ForceType text/plain
```
