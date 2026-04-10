#!/usr/bin/env bash
# =============================================================================
# Four-Engine Comparison Test Runner
# Engines: Apache 2.4, OLS Native, OLS+Module, LSWS Enterprise
# Usage:
#   ./compare_runner.sh                    # Run all P0 cases
#   ./compare_runner.sh --group headers_core
#   ./compare_runner.sh --case HD_001
#   ./compare_runner.sh --fail-fast
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CASES_DIR="${SCRIPT_DIR}/cases"
OUT_DIR="${SCRIPT_DIR}/out"

# 支持多个测试用例文件
PRIORITY="${PRIORITY:-p0}"  # 默认 P0，可通过环境变量设置
CASES_FILE="${CASES_DIR}/${PRIORITY}_cases.yaml"

APACHE_URL="http://localhost:18080"
OLS_NATIVE_URL="http://localhost:28080"
OLS_MODULE_URL="http://localhost:38080"
LSWS_URL="http://localhost:48080"

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
CYAN='\033[0;36m'; NC='\033[0m'

TOTAL=0; PASS=0; FAIL=0; SKIP=0; KNOWN_DIFF=0
FILTER_GROUP=""; FILTER_CASE=""; FAIL_FAST=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --group) FILTER_GROUP="$2"; shift 2 ;;
    --case)  FILTER_CASE="$2"; shift 2 ;;
    --fail-fast) FAIL_FAST=1; shift ;;
    *) echo "Unknown option: $1"; exit 1 ;;
  esac
done

mkdir -p "$OUT_DIR"

# =============================================================================
# YAML mini-parser
# =============================================================================
parse_yaml_cases() {
  local file="$1"
  local idx=-1
  local current_key=""

  while IFS= read -r line || [[ -n "$line" ]]; do
    [[ "$line" =~ ^[[:space:]]*# ]] && continue
    [[ "$line" =~ ^[[:space:]]*$ ]] && continue

    if [[ "$line" =~ ^-[[:space:]]+id:[[:space:]]*(.+) ]]; then
      idx=$((idx + 1))
      CASE_ID[$idx]="${BASH_REMATCH[1]//\"/}"
      CASE_GROUP[$idx]=""; CASE_DESC[$idx]=""; CASE_METHOD[$idx]="GET"
      CASE_PATH[$idx]=""; CASE_MODE[$idx]="all4"; CASE_AUTH[$idx]=""
      CASE_CHECKS[$idx]=""; CASE_KNOWN[$idx]=""
      continue
    fi

    [[ $idx -lt 0 ]] && continue

    if [[ "$line" =~ ^[[:space:]]+([a-z_]+):[[:space:]]*\"?([^\"]*)\"? ]]; then
      local key="${BASH_REMATCH[1]}"
      local val="${BASH_REMATCH[2]}"
      val="${val%\"}"
      case "$key" in
        group)        CASE_GROUP[$idx]="$val" ;;
        desc)         CASE_DESC[$idx]="$val" ;;
        method)       CASE_METHOD[$idx]="$val" ;;
        path)         CASE_PATH[$idx]="$val" ;;
        compare_mode) CASE_MODE[$idx]="$val" ;;
        auth)         CASE_AUTH[$idx]="$val" ;;
        known_diff)   CASE_KNOWN[$idx]="$val" ;;
      esac
    fi

    if [[ "$line" =~ ^[[:space:]]+checks: ]]; then
      current_key="checks"; continue
    fi
    if [[ "$current_key" == "checks" ]]; then
      if [[ "$line" =~ ^[[:space:]]{4,}([a-zA-Z_:.\-]+):[[:space:]]*(.+) ]]; then
        local ck="${BASH_REMATCH[1]}"
        local cv="${BASH_REMATCH[2]}"
        cv="${cv#\"}" ; cv="${cv%\"}"
        CASE_CHECKS[$idx]+="${ck}=${cv}|"
      else
        current_key=""
      fi
    fi
  done < "$file"

  CASE_COUNT=$((idx + 1))
}

# =============================================================================
# HTTP helpers
# =============================================================================
do_request() {
  local url="$1" method="$2" path="$3" auth="$4"
  local curl_args=(-s -S -D- -o- -X "$method" --max-time 10 -L0 --max-redirs 0)
  [[ -n "$auth" ]] && curl_args+=(-u "$auth")
  curl "${curl_args[@]}" "${url}${path}" 2>/dev/null || true
}

get_status() {
  echo "$1" | head -1 | grep -oP 'HTTP/[0-9.]+ \K[0-9]+' || echo "000"
}

get_header() {
  local raw="$1" name="$2"
  local headers
  headers=$(echo "$raw" | sed -n '1,/^\r*$/p')
  echo "$headers" | grep -i "^${name}:" | head -1 | sed "s/^[^:]*:[[:space:]]*//" | tr -d '\r\n'
}

get_body() {
  echo "$1" | sed '1,/^\r*$/d'
}

get_probe_field() {
  local body="$1" field="$2"
  if [[ "$field" == *"."* ]]; then
    local child="${field#*.}"
    echo "$body" | grep -oP "\"${child}\"[[:space:]]*:[[:space:]]*\"?\K[^\",}\n]+" | head -1
  else
    echo "$body" | grep -oP "\"${field}\"[[:space:]]*:[[:space:]]*\"?\K[^\",}\n]+" | head -1
  fi
}

# =============================================================================
# Check runner
# =============================================================================
run_check() {
  local check_key="$1" check_val="$2" raw="$3" engine="$4"
  local status body hdr_val probe_val

  case "$check_key" in
    status)
      status=$(get_status "$raw")
      [[ "$status" == "$check_val" ]] && return 0
      echo "    ${engine}: status expected=$check_val got=$status"; return 1
      ;;
    status_in)
      status=$(get_status "$raw")
      local cleaned="${check_val//[\[\] ]/}"
      IFS=',' read -ra allowed <<< "$cleaned"
      for a in "${allowed[@]}"; do [[ "$status" == "$a" ]] && return 0; done
      echo "    ${engine}: status expected one of ${check_val} got=$status"; return 1
      ;;
    header:*_contains)
      local hname="${check_key#header:}"; hname="${hname%_contains}"
      hdr_val=$(get_header "$raw" "$hname")
      [[ "$hdr_val" == *"$check_val"* ]] && return 0
      echo "    ${engine}: header $hname expected to contain '$check_val' got='$hdr_val'"; return 1
      ;;
    header:*)
      local hname="${check_key#header:}"
      hdr_val=$(get_header "$raw" "$hname")
      if [[ "$check_val" == "null" ]]; then
        [[ -z "$hdr_val" ]] && return 0
        echo "    ${engine}: header $hname expected absent, got='$hdr_val'"; return 1
      fi
      [[ "$hdr_val" == "$check_val" ]] && return 0
      echo "    ${engine}: header $hname expected='$check_val' got='$hdr_val'"; return 1
      ;;
    body_contains)
      body=$(get_body "$raw")
      echo "$body" | grep -qF "$check_val" && return 0
      echo "    ${engine}: body expected to contain '$check_val'"; return 1
      ;;
    body_not_contains)
      body=$(get_body "$raw")
      echo "$body" | grep -qF "$check_val" && { echo "    ${engine}: body should NOT contain '$check_val'"; return 1; }
      return 0
      ;;
    probe:*)
      local pfield="${check_key#probe:}"
      body=$(get_body "$raw")
      probe_val=$(get_probe_field "$body" "$pfield")
      [[ "$probe_val" == "$check_val" ]] && return 0
      echo "    ${engine}: probe $pfield expected='$check_val' got='$probe_val'"; return 1
      ;;
    *)
      echo "    ${engine}: unknown check type '$check_key'"; return 1
      ;;
  esac
}

# =============================================================================
# Engine list for compare_mode
# =============================================================================
get_engines_for_mode() {
  case "$1" in
    all4)      echo "apache ols_native ols_module lsws" ;;
    htaccess)  echo "apache lsws" ;;
    mod)       echo "apache ols_module" ;;
    mod_lsws)  echo "apache ols_module lsws" ;;
    known)     echo "apache" ;;
    *)         echo "apache ols_module lsws" ;;
  esac
}

# =============================================================================
# Main execution loop
# =============================================================================
declare -a CASE_ID CASE_GROUP CASE_DESC CASE_METHOD CASE_PATH CASE_MODE CASE_AUTH CASE_CHECKS CASE_KNOWN
CASE_COUNT=0

echo -e "${CYAN}=== Four-Engine Comparison Test Runner ===${NC}"
echo -e "  Apache 2.4  -> ${APACHE_URL}"
echo -e "  OLS Native  -> ${OLS_NATIVE_URL}"
echo -e "  OLS+Module  -> ${OLS_MODULE_URL}"
echo -e "  LSWS Ent.   -> ${LSWS_URL}"
echo ""

parse_yaml_cases "$CASES_FILE"
echo "Loaded $CASE_COUNT test cases from $CASES_FILE"
echo ""

echo "id,group,desc,compare_mode,apache,ols_native,ols_module,lsws,result" > "$OUT_DIR/summary.csv"
echo "[" > "$OUT_DIR/diff.json"
FIRST_DIFF=1

for ((i=0; i<CASE_COUNT; i++)); do
  id="${CASE_ID[$i]}"; group="${CASE_GROUP[$i]}"; desc="${CASE_DESC[$i]}"
  method="${CASE_METHOD[$i]}"; path="${CASE_PATH[$i]}"; mode="${CASE_MODE[$i]}"
  auth="${CASE_AUTH[$i]}"; checks="${CASE_CHECKS[$i]}"; known="${CASE_KNOWN[$i]}"

  [[ -n "$FILTER_GROUP" && "$group" != "$FILTER_GROUP" ]] && continue
  [[ -n "$FILTER_CASE" && "$id" != "$FILTER_CASE" ]] && continue

  TOTAL=$((TOTAL + 1))
  echo -e "${CYAN}[$id]${NC} $desc"

  raw_apache=$(do_request "$APACHE_URL" "$method" "$path" "$auth")
  raw_native=$(do_request "$OLS_NATIVE_URL" "$method" "$path" "$auth")
  raw_module=$(do_request "$OLS_MODULE_URL" "$method" "$path" "$auth")
  raw_lsws=$(do_request "$LSWS_URL" "$method" "$path" "$auth")

  st_apache=$(get_status "$raw_apache")
  st_native=$(get_status "$raw_native")
  st_module=$(get_status "$raw_module")
  st_lsws=$(get_status "$raw_lsws")

  case_pass=1; case_errors=""
  engines=$(get_engines_for_mode "$mode")

  IFS='|' read -ra check_pairs <<< "$checks"
  for pair in "${check_pairs[@]}"; do
    [[ -z "$pair" ]] && continue
    ck="${pair%%=*}"; cv="${pair#*=}"

    if [[ "$mode" == "known" ]]; then
      err=$(run_check "$ck" "$cv" "$raw_apache" "apache" 2>&1) || {
        case_pass=0; case_errors+="$err"$'\n'
      }
      for eng_name in ols_module lsws; do
        case "$eng_name" in
          ols_module) eng_raw="$raw_module" ;;
          lsws)       eng_raw="$raw_lsws" ;;
        esac
        run_check "$ck" "$cv" "$eng_raw" "$eng_name" >/dev/null 2>&1 || KNOWN_DIFF=$((KNOWN_DIFF + 1))
      done
    else
      for eng_name in $engines; do
        case "$eng_name" in
          apache)     eng_raw="$raw_apache" ;;
          ols_native) eng_raw="$raw_native" ;;
          ols_module) eng_raw="$raw_module" ;;
          lsws)       eng_raw="$raw_lsws" ;;
        esac
        err=$(run_check "$ck" "$cv" "$eng_raw" "$eng_name" 2>&1) || {
          case_pass=0; case_errors+="$err"$'\n'
        }
      done
    fi
  done

  if [[ $case_pass -eq 1 ]]; then
    PASS=$((PASS + 1)); result="PASS"
    echo -e "  ${GREEN}PASS${NC} (apache=$st_apache native=$st_native module=$st_module lsws=$st_lsws)"
  else
    FAIL=$((FAIL + 1)); result="FAIL"
    echo -e "  ${RED}FAIL${NC} (apache=$st_apache native=$st_native module=$st_module lsws=$st_lsws)"
    [[ -n "$case_errors" ]] && echo "$case_errors"
    [[ $FIRST_DIFF -eq 0 ]] && echo "," >> "$OUT_DIR/diff.json"
    FIRST_DIFF=0
    cat >> "$OUT_DIR/diff.json" <<DIFFJSON
  {
    "id": "$id", "group": "$group",
    "apache": $st_apache, "ols_native": $st_native, "ols_module": $st_module, "lsws": $st_lsws,
    "errors": $(echo "$case_errors" | sed 's/"/\\"/g' | sed ':a;N;$!ba;s/\n/\\n/g' | sed 's/^/"/;s/$/"/')
  }
DIFFJSON
    [[ $FAIL_FAST -eq 1 ]] && { echo -e "\n${RED}--fail-fast: stopping${NC}"; break; }
  fi

  echo "$id,$group,\"$desc\",$mode,$st_apache,$st_native,$st_module,$st_lsws,$result" >> "$OUT_DIR/summary.csv"
done

echo "]" >> "$OUT_DIR/diff.json"

echo ""
echo -e "${CYAN}=== Summary ===${NC}"
echo -e "Total: $TOTAL  ${GREEN}Pass: $PASS${NC}  ${RED}Fail: $FAIL${NC}  ${YELLOW}Known Diff: $KNOWN_DIFF${NC}"
echo "Reports: $OUT_DIR/summary.csv  $OUT_DIR/diff.json"

[[ $FAIL -gt 0 ]] && exit 1 || exit 0
