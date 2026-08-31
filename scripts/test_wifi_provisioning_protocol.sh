#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "$0")/.." && pwd)"
build_dir="${TMPDIR:-/tmp}/babymilu-wifi-provisioning-test"
mkdir -p "$build_dir"

${CXX:-c++} -std=c++17 -Wall -Wextra -Werror \
  -I"$repo_dir/main/boards/common" \
  "$repo_dir/main/boards/common/wifi_provisioning_protocol.cc" \
  "$repo_dir/tests/wifi_provisioning_protocol_test.cc" \
  -o "$build_dir/wifi_provisioning_protocol_test"

"$build_dir/wifi_provisioning_protocol_test"
