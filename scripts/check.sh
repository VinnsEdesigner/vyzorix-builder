#!/usr/bin/env bash
#
# check.sh — every check this repository runs, in one command.
#
#   ./scripts/check.sh            configure, build, test, lint
#   ./scripts/check.sh --no-lint  skip clang-tidy and cppcheck
#
# It is the single source of truth for "is the tree clean": CI runs exactly this,
# so a green local run and a green CI run mean the same thing. Any failure stops
# the script with a non-zero exit.

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
RUN_LINT=1

for arg in "$@"; do
  case "$arg" in
    --no-lint) RUN_LINT=0 ;;
    -h|--help) sed -n '2,9p' "$0"; exit 0 ;;
    *) echo "unknown argument: $arg" >&2; exit 2 ;;
  esac
done

CPPCHECK_INCLUDES=(
  -I okfw/include -I domain/include
  -I modules/plan/include -I modules/adapter/include -I modules/containerbuild/include
  -I modules/package/include -I modules/publish/include -I modules/schema/include
)

fail=0
step() { printf '\n=== %s\n' "$*"; }
ok()   { printf 'ok    %s\n' "$*"; }
bad()  { printf 'FAIL  %s\n' "$*"; fail=1; }

cd "$ROOT"

step "Configure"
if cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON; then
  ok "configure"
else
  bad "configure"
  exit 1
fi

step "Build"
if cmake --build "$BUILD_DIR" --parallel; then ok "build"; else bad "build"; fi

step "Test"
if ctest --test-dir "$BUILD_DIR" --output-on-failure; then ok "test"; else bad "test"; fi

if [ "$RUN_LINT" -eq 1 ]; then
  # The linters are part of the check, so install them when they are missing
  # rather than silently skipping the step (a skip that hides findings is worse
  # than no lint step at all). `apt-get` is used because this runs on Debian.
  ensure_tool() {
    local tool="$1"
    command -v "$tool" >/dev/null 2>&1 && return 0
    if command -v apt-get >/dev/null 2>&1; then
      printf 'installing %s ...\n' "$tool"
      sudo apt-get install -y "$tool" >/dev/null 2>&1 || apt-get install -y "$tool" >/dev/null 2>&1 || true
    fi
    command -v "$tool" >/dev/null 2>&1
  }

  step "clang-tidy"
  if ensure_tool clang-tidy; then
    # The curated .clang-tidy set with WarningsAsErrors: any finding fails.
    if clang-tidy -p "$BUILD_DIR" --quiet $(find okfw domain modules cmd -name '*.cpp'); then
      ok "clang-tidy"
    else
      bad "clang-tidy"
    fi
  else
    bad "clang-tidy (could not install)"
  fi

  step "cppcheck"
  if ensure_tool cppcheck; then
    if cppcheck "${CPPCHECK_INCLUDES[@]}" okfw domain modules cmd; then
      ok "cppcheck"
    else
      bad "cppcheck"
    fi
  else
    bad "cppcheck (could not install)"
  fi
fi

printf '\n'
if [ "$fail" -eq 0 ]; then
  echo "all checks passed"
else
  echo "one or more checks failed"
fi
exit "$fail"
