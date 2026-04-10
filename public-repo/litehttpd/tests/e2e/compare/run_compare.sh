#!/usr/bin/env bash
# =============================================================================
# Four-Engine Comparison — Build, Start, Test, Cleanup
# Usage:
#   ./run_compare.sh              # Full run: build + start + test + stop
#   ./run_compare.sh up           # Only start services
#   ./run_compare.sh test         # Only run tests (services must be up)
#   ./run_compare.sh down         # Only stop services
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
COMPOSE_FILE="$SCRIPT_DIR/docker-compose.compare.yml"

RED='\033[0;31m'; GREEN='\033[0;32m'; CYAN='\033[0;36m'; NC='\033[0m'

cmd_up() {
  echo -e "${CYAN}[1/4] Building litehttpd_htaccess.so ...${NC}"
  (cd "$PROJECT_ROOT" && cmake -B build -S . -DCMAKE_BUILD_TYPE=Release 2>&1 | tail -3)
  (cd "$PROJECT_ROOT" && cmake --build build -j"$(nproc)" 2>&1 | tail -5)

  echo -e "${CYAN}[2/4] Copying module and patches to build context ...${NC}"
  cp "$PROJECT_ROOT/build/litehttpd_htaccess.so" "$SCRIPT_DIR/litehttpd_htaccess.so"
  cp "$PROJECT_ROOT/patches/0001-lsiapi-phpconfig.patch" "$SCRIPT_DIR/"
  cp "$PROJECT_ROOT/patches/0002-lsiapi-rewrite.patch" "$SCRIPT_DIR/"
  cp "$PROJECT_ROOT/patches/0004-autoindex-403.patch" "$SCRIPT_DIR/"

  echo -e "${CYAN}[3/4] Building and starting four engines ...${NC}"
  docker compose -f "$COMPOSE_FILE" build --quiet
  docker compose -f "$COMPOSE_FILE" up -d

  echo -e "${CYAN}[4/4] Waiting for services to be healthy ...${NC}"
  local max_wait=180
  local waited=0
  while [[ $waited -lt $max_wait ]]; do
    local ready=0
    curl -sf "http://localhost:18080/_probe/probe.php" >/dev/null 2>&1 && ready=$((ready+1))
    curl -sf "http://localhost:28080/_probe/probe.php" >/dev/null 2>&1 && ready=$((ready+1))
    curl -sf "http://localhost:38080/_probe/probe.php" >/dev/null 2>&1 && ready=$((ready+1))
    curl -sf "http://localhost:48080/_probe/probe.php" >/dev/null 2>&1 && ready=$((ready+1))
    if [[ $ready -eq 4 ]]; then
      echo -e "${GREEN}All four engines are ready.${NC}"
      return 0
    fi
    sleep 3
    waited=$((waited+3))
    echo "  Waiting... ($waited/${max_wait}s, $ready/4 ready)"
  done
  echo -e "${RED}Timeout waiting for services.${NC}"
  docker compose -f "$COMPOSE_FILE" ps
  docker compose -f "$COMPOSE_FILE" logs --tail=20
  return 1
}

cmd_test() {
  local priority="${PRIORITY:-p0}"
  echo -e "${CYAN}Running comparison tests (priority: $priority) ...${NC}"
  PRIORITY="$priority" "$SCRIPT_DIR/compare_runner.sh" "$@"
}

cmd_down() {
  echo -e "${CYAN}Stopping services ...${NC}"
  docker compose -f "$COMPOSE_FILE" down -v --remove-orphans 2>/dev/null || true
  rm -f "$SCRIPT_DIR/litehttpd_htaccess.so" "$SCRIPT_DIR"/0001-*.patch "$SCRIPT_DIR"/0002-*.patch "$SCRIPT_DIR"/0004-*.patch
}

ACTION="${1:-full}"
shift 2>/dev/null || true

case "$ACTION" in
  up)   cmd_up ;;
  test) cmd_test "$@" ;;
  down) cmd_down ;;
  full|"") trap cmd_down EXIT; cmd_up; cmd_test "$@" ;;
  *)    echo "Usage: $0 [up|test|down|full] [test options...]"; exit 1 ;;
esac
