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

unset PKG_CONFIG_PATH
export PKG_CONFIG_LIBDIR=${pc_file%/*}
export PKG_CONFIG_SYSROOT_DIR=$stage
[[ $(pkg-config --modversion gwipc) == 0.6.0 ]]
read -r -a flags <<<"$(pkg-config --cflags --libs gwipc)"
[[ " ${flags[*]} " != *"$source_root"* &&
   " ${flags[*]} " != *"$build_root"* ]]
library_dir=${library%/*}

consumers=(
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
