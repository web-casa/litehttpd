#!/bin/bash
# Generate static index.html files for R2 directory listing
# Usage: generate-index.sh <repo-root-dir>

set -e
REPO_DIR="${1%/}"
SITE_NAME="LiteHTTPD RPM Repository"
SITE_URL="https://rpms.litehttpd.com"

# HTML-escape: & < > "
html_escape() {
    printf '%s' "$1" | sed 's/&/\&amp;/g; s/</\&lt;/g; s/>/\&gt;/g; s/"/\&quot;/g'
}

# URL-encode path segment
url_encode() {
    local input="$1"
    local encoded=""
    local i char hex
    local LC_ALL=C

    for ((i = 0; i < ${#input}; i++)); do
        char="${input:i:1}"
        case "$char" in
            [a-zA-Z0-9.~_-])
                encoded="${encoded}${char}"
                ;;
            *)
                printf -v hex '%%%02X' "'$char"
                encoded="${encoded}${hex}"
                ;;
        esac
    done
    printf '%s' "$encoded"
}

generate_html() {
    local dir="$1"
    local rel_path="${dir#"$REPO_DIR"}"
    rel_path="${rel_path#/}"
    rel_path="${rel_path%/}"
    local title="/"
    [ -n "$rel_path" ] && title="/${rel_path}/"
    local safe_title
    safe_title=$(html_escape "$title")
    local index_file="$dir/index.html"
    local tmpfile
    tmpfile=$(mktemp)

    # Collect entries into temp file (avoids O(n^2) string concat)
    for item in "$dir"/*; do
        [ -e "$item" ] || continue
        local name
        name=$(basename "$item")
        [ "$name" = "index.html" ] && continue

        local safe_name href size icon date_str
        safe_name=$(html_escape "$name")

        if [ -d "$item" ]; then
            size="-"
            icon="&#x1F4C1;"
            href="$(url_encode "$name")/"
        else
            local bytes
            bytes=$(stat -c%s "$item" 2>/dev/null || stat -f%z "$item" 2>/dev/null || echo 0)
            if [ "$bytes" -ge 1048576 ]; then
                size="$((bytes / 1048576)).$((bytes % 1048576 * 10 / 1048576)) MB"
            elif [ "$bytes" -ge 1024 ]; then
                size="$((bytes / 1024)).$((bytes % 1024 * 10 / 1024)) KB"
            else
                size="${bytes} B"
            fi
            icon="&#x1F4C4;"
            case "$name" in
                *.rpm) icon="&#x1F4E6;" ;;
                *.xml|*.xml.asc|*.asc) icon="&#x1F510;" ;;
                *.sh) icon="&#x2699;&#xFE0F;" ;;
                *.gz|*.bz2|*.xz|*.zst) icon="&#x1F5DC;&#xFE0F;" ;;
                RPM-GPG-KEY*) icon="&#x1F511;" ;;
            esac
            href=$(url_encode "$name")
        fi
        local mtime
        mtime=$(stat -c%Y "$item" 2>/dev/null || stat -f%m "$item" 2>/dev/null || echo 0)
        date_str=$(date -d "@$mtime" "+%Y-%m-%d %H:%M" 2>/dev/null || date -r "$mtime" "+%Y-%m-%d %H:%M" 2>/dev/null || echo "-")

        printf '<tr><td class="icon">%s</td><td class="name"><a href="%s">%s</a></td><td class="size">%s</td><td class="date">%s</td></tr>\n' \
            "$icon" "$href" "$safe_name" "$size" "$date_str" >> "$tmpfile"
    done

    # Parent link
    local parent=""
    if [ -n "$rel_path" ]; then
        parent='<tr><td class="icon">&#x2B06;&#xFE0F;</td><td class="name"><a href="../">Parent Directory</a></td><td class="size">-</td><td class="date">-</td></tr>'
    fi

    # Breadcrumb
    local breadcrumb='<a href="/">root</a>'
    if [ -n "$rel_path" ]; then
        local accum=""
        local accum_href=""
        IFS='/' read -ra parts <<< "$rel_path"
        for part in "${parts[@]}"; do
            accum="$accum/$part"
            accum_href="$accum_href/$(url_encode "$part")"
            local safe_part
            safe_part=$(html_escape "$part")
            breadcrumb="$breadcrumb / <a href=\"$accum_href/\">$safe_part</a>"
        done
    fi

    # Install hint
    local install_block=""
    if [ -z "$rel_path" ]; then
        install_block='<div class="install"><strong>Quick Install:</strong><br><code>curl -s https://rpms.litehttpd.com/setup.sh | bash &amp;&amp; dnf install openlitespeed-litehttpd</code></div>'
    fi

    cat > "$index_file" << HTMLEOF
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>$SITE_NAME — $safe_title</title>
<style>
* { margin: 0; padding: 0; box-sizing: border-box; }
body { font-family: -apple-system, 'Segoe UI', Roboto, monospace; background: #0d1117; color: #c9d1d9; }
.header { background: #161b22; border-bottom: 1px solid #30363d; padding: 20px 0; }
.container { max-width: 960px; margin: 0 auto; padding: 0 20px; }
.logo { display: flex; align-items: center; gap: 12px; }
.logo svg { width: 32px; height: 32px; fill: #58a6ff; }
.logo h1 { font-size: 18px; font-weight: 600; color: #f0f6fc; }
.logo span { font-size: 13px; color: #8b949e; }
.breadcrumb { margin: 16px 0; font-size: 14px; color: #8b949e; }
.breadcrumb a { color: #58a6ff; text-decoration: none; }
.breadcrumb a:hover { text-decoration: underline; }
table { width: 100%; border-collapse: collapse; margin: 8px 0 32px; }
th { text-align: left; padding: 8px 12px; border-bottom: 1px solid #30363d; color: #8b949e; font-size: 12px; font-weight: 500; text-transform: uppercase; letter-spacing: 0.5px; }
td { padding: 8px 12px; border-bottom: 1px solid #21262d; font-size: 14px; }
tr:hover { background: #161b22; }
.icon { width: 28px; text-align: center; }
.name a { color: #58a6ff; text-decoration: none; }
.name a:hover { text-decoration: underline; }
.size { width: 100px; text-align: right; color: #8b949e; font-variant-numeric: tabular-nums; }
.date { width: 150px; color: #8b949e; font-variant-numeric: tabular-nums; }
.footer { margin: 32px 0; padding-top: 16px; border-top: 1px solid #21262d; font-size: 12px; color: #484f58; }
.footer a { color: #58a6ff; text-decoration: none; }
.install { background: #161b22; border: 1px solid #30363d; border-radius: 6px; padding: 16px; margin: 16px 0; font-size: 13px; }
.install code { color: #7ee787; }
@media (max-width: 600px) { .date { display: none; } .size { width: 70px; } }
</style>
</head>
<body>
<div class="header">
<div class="container">
<div class="logo">
<svg viewBox="0 0 24 24"><path d="M12 2L2 7l10 5 10-5-10-5zM2 17l10 5 10-5M2 12l10 5 10-5"/></svg>
<div><h1>$SITE_NAME</h1><span>$SITE_URL</span></div>
</div>
</div>
</div>
<div class="container">
<p class="breadcrumb">$breadcrumb</p>
$install_block
<table>
<thead><tr><th></th><th>Name</th><th class="size">Size</th><th class="date">Modified</th></tr></thead>
<tbody>
$parent
$(cat "$tmpfile")
</tbody>
</table>
<div class="footer">Powered by <a href="https://web.casa">Web.Casa</a> &middot; LiteHTTPD</div>
</div>
</body>
</html>
HTMLEOF

    rm -f "$tmpfile"
}

# Generate for all directories recursively (null-delimited for safety)
find "$REPO_DIR" -type d -print0 | sort -z | while IFS= read -r -d '' dir; do
    generate_html "$dir"
done

echo "Generated index.html files in $REPO_DIR"
