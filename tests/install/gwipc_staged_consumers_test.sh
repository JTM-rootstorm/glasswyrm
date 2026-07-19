#!/usr/bin/env bash
set -euo pipefail

if (($# != 2)); then
  printf '%s\n' 'usage: gwipc_staged_consumers_test.sh SOURCE_ROOT BUILD_ROOT' >&2
  exit 2
fi

source_root=$1
build_root=$2
stage=$(mktemp -d "${TMPDIR:-/tmp}/gwipc-staged-consumers.XXXXXX")
trap 'rm -rf "$stage"' EXIT

DESTDIR=$stage meson install -C "$build_root" --no-rebuild >/dev/null
pc_file=$(find "$stage" -type f -name gwipc.pc -print -quit)
library=$(find -L "$stage" -type f -name 'libgwipc.so.*' -print -quit)
[[ -n $pc_file && -n $library ]]

tools_enabled=$(meson introspect --buildoptions "$build_root" | python3 -c '
import json
import sys

matches = [item for item in json.load(sys.stdin) if item.get("name") == "tools"]
if len(matches) != 1 or not isinstance(matches[0].get("value"), bool):
    raise SystemExit("unable to read the Meson tools option")
print("true" if matches[0]["value"] else "false")
')

unset PKG_CONFIG_PATH
export PKG_CONFIG_LIBDIR=${pc_file%/*}
export PKG_CONFIG_SYSROOT_DIR=$stage
[[ $(pkg-config --modversion gwipc) == 0.9.0 ]]
read -r -a flags <<<"$(pkg-config --cflags --libs gwipc)"
[[ " ${flags[*]} " != *"$source_root"* &&
   " ${flags[*]} " != *"$build_root"* ]]
library_dir=${library%/*}

gwinfo=$(find "$stage" -type f -path '*/bin/gwinfo' -perm -0100 -print -quit)
gwout=$(find "$stage" -type f -path '*/bin/gwout' -perm -0100 -print -quit)
if [[ $tools_enabled == true ]]; then
  [[ $gwinfo == "$stage"/* && $gwout == "$stage"/* ]]
  for tool in "$gwinfo" "$gwout"; do
    LD_LIBRARY_PATH=$library_dir "$tool" --help >/dev/null
    version=$(LD_LIBRARY_PATH=$library_dir "$tool" --version)
    [[ $version == "${tool##*/} "* ]]
  done
  printf 'gwipc staged installed output tools: passed\n'
else
  [[ -z $gwinfo && -z $gwout ]]
  printf 'gwipc staged tools-disabled boundary: passed\n'
fi

consumers=(
  '0.9|gwipc_vrr_c_consumer.c|c'
  '0.9|gwipc_vrr_cpp_consumer.cpp|c++'
  '0.1|gwipc_transport_c_consumer.c|c'
  '0.1|gwipc_transport_cpp_consumer.cpp|c++'
  '0.2|gwipc_c_consumer.c|c'
  '0.2|gwipc_cpp_consumer.cpp|c++'
  '0.3|gwipc_policy_c_consumer.c|c'
  '0.3|gwipc_policy_cpp_consumer.cpp|c++'
  '0.4|gwipc_lifecycle_c_consumer.c|c'
  '0.4|gwipc_lifecycle_cpp_consumer.cpp|c++'
  '0.5|gwipc_input_c_consumer.c|c'
  '0.5|gwipc_input_cpp_consumer.cpp|c++'
  '0.6|gwipc_session_c_consumer.c|c'
  '0.6|gwipc_session_cpp_consumer.cpp|c++'
  '0.7|gwipc_sync_c_consumer.c|c'
  '0.7|gwipc_sync_cpp_consumer.cpp|c++'
  '0.8|gwipc_output_c_consumer.c|c'
  '0.8|gwipc_output_cpp_consumer.cpp|c++'
)

for entry in "${consumers[@]}"; do
  IFS='|' read -r version source language <<<"$entry"
  output=$stage/consumer-${version}-${language//+/p}
  if [[ $language == c ]]; then
    cc -std=c17 -Wall -Wextra -Werror "$source_root/tests/install/$source" \
      -o "$output" "${flags[@]}"
  else
    c++ -std=c++20 -Wall -Wextra -Werror \
      "$source_root/tests/install/$source" -o "$output" "${flags[@]}"
  fi
  LD_LIBRARY_PATH=$library_dir "$output"
  printf 'gwipc staged API %s %s consumer: passed\n' "$version" "$language"
done
