#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "$0")/.." && pwd)"
build_dir="$(mktemp -d)"
trap 'rm -rf "$build_dir"' EXIT

${CXX:-c++} -std=c++17 -Wall -Wextra -Werror \
  -I"$repo_dir/main/boards/common" \
  "$repo_dir/main/boards/common/wifi_priority.cc" \
  "$repo_dir/tests/wifi_priority_test.cc" \
  -o "$build_dir/wifi_priority_test"

"$build_dir/wifi_priority_test"
