#!/bin/bash
# Generate static index.html files for R2 directory listing
# Usage: generate-index.sh <repo-root-dir>

set -e
REPO_DIR="${1%/}"
SITE_NAME="LiteHTTPD RPM Repository"
SITE_URL="https://rpms.litehttpd.com"

generate_html() {
    local dir="$1"
    local rel_path="${dir#$REPO_DIR}"
    rel_path="${rel_path#/}"
    rel_path="${rel_path%/}"
    local title="/"
    [ -n "$rel_path" ] && title="/${rel_path}/"
    local index_file="$dir/index.html"

    # Collect entries
    local entries=""
    for item in "$dir"/*; do
        [ -e "$item" ] || continue
        local name=$(basename "$item")
        [ "$name" = "index.html" ] && continue

        if [ -d "$item" ]; then
            local size="-"
            local icon="📁"
            local href="$name/"
        else
            local bytes=$(stat -c%s "$item" 2>/dev/null || stat -f%z "$item" 2>/dev/null || echo 0)
            if [ "$bytes" -ge 1048576 ]; then
                local size="$((bytes / 1048576)).$((bytes % 1048576 * 10 / 1048576)) MB"
            elif [ "$bytes" -ge 1024 ]; then
                local size="$((bytes / 1024)).$((bytes % 1024 * 10 / 1024)) KB"
            else
                local size="${bytes} B"
            fi
            local icon="📄"
            case "$name" in
                *.rpm) icon="📦" ;;
                *.xml|*.xml.asc) icon="🔐" ;;
                *.sh) icon="⚙️" ;;
                *.gz|*.bz2|*.xz|*.zst) icon="🗜️" ;;
                RPM-GPG-KEY*) icon="🔑" ;;
            esac
            local href="$name"
        fi
        local mtime=$(stat -c%Y "$item" 2>/dev/null || stat -f%m "$item" 2>/dev/null || echo 0)
        local date=$(date -d "@$mtime" "+%Y-%m-%d %H:%M" 2>/dev/null || date -r "$mtime" "+%Y-%m-%d %H:%M" 2>/dev/null || echo "-")

        entries="$entries<tr><td class=\"icon\">$icon</td><td class=\"name\"><a href=\"$href\">$name</a></td><td class=\"size\">$size</td><td class=\"date\">$date</td></tr>\n"
    done

    # Parent link
    local parent=""
    if [ -n "$rel_path" ]; then
        parent='<tr><td class="icon">⬆️</td><td class="name"><a href="../">Parent Directory</a></td><td class="size">-</td><td class="date">-</td></tr>'
    fi

    cat > "$index_file" << HTMLEOF
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>$SITE_NAME — $title</title>
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
HTMLEOF

    # Breadcrumb
    echo -n '<p class="breadcrumb"><a href="/">root</a>' >> "$index_file"
    if [ -n "$rel_path" ]; then
        IFS='/' read -ra parts <<< "$rel_path"
        local accum=""
        for part in "${parts[@]}"; do
            accum="$accum/$part"
            echo -n " / <a href=\"$accum/\">$part</a>" >> "$index_file"
        done
    fi
    echo '</p>' >> "$index_file"

    # Install hint on root page
    if [ -z "$rel_path" ]; then
        cat >> "$index_file" << 'INSTALLEOF'
<div class="install">
<strong>Quick Install:</strong><br>
<code>curl -s https://rpms.litehttpd.com/setup.sh | bash && dnf install openlitespeed-litehttpd</code>
</div>
INSTALLEOF
    fi

    cat >> "$index_file" << TABLEEOF
<table>
<thead><tr><th></th><th>Name</th><th class="size">Size</th><th class="date">Modified</th></tr></thead>
<tbody>
$parent
$(echo -e "$entries")
</tbody>
</table>
<div class="footer">Powered by <a href="https://web.casa">Web.Casa</a> &middot; LiteHTTPD</div>
</div>
</body>
</html>
TABLEEOF
}

# Generate for all directories recursively
find "$REPO_DIR" -type d | sort | while read dir; do
    generate_html "$dir"
done

echo "Generated index.html files in $REPO_DIR"
