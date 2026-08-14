#!/usr/bin/env bash
set -euo pipefail

if (($# != 3)); then
  echo "usage: $0 SOURCE_ROOT TARGET_DIR WORK_DIR" >&2
  exit 2
fi

source_root=$1
target_dir=$2
work_dir=$3
crate_dir="$source_root/crates/gw-ipc-capi"

mkdir -p "$work_dir"
cargo build --offline --manifest-path "$crate_dir/Cargo.toml" --target-dir "$target_dir"

cc -std=c17 -Wall -Wextra -Werror -fPIC -I"$source_root/include" \
  -c "$crate_dir/native/gwipc_capi_shim.c" \
  -o "$work_dir/gwipc_capi_shim.o"
cc -std=c17 -Wall -Wextra -Werror -fPIC -I"$source_root/include" \
  -c "$crate_dir/native/gwipc_capi_test_message.c" \
  -o "$work_dir/gwipc_capi_test_message.o"

cc -shared \
  -I"$source_root/include" \
  "$work_dir/gwipc_capi_shim.o" \
  "$work_dir/gwipc_capi_test_message.o" \
  "$target_dir/debug/libgw_ipc_capi.a" \
  -ldl -lpthread -lm \
  -Wl,--no-undefined \
  -Wl,--version-script="$crate_dir/native/gwipc_capi.map" \
  -o "$work_dir/libgwipc-capi-smoke.so"

cc -std=c17 -Wall -Wextra -Werror -I"$source_root/include" \
  "$crate_dir/tests/smoke_c.c" -L"$work_dir" -lgwipc-capi-smoke \
  -Wl,-rpath,"$work_dir" \
  -o "$work_dir/gwipc-capi-smoke-c"

c++ -std=c++20 -Wall -Wextra -Werror -I"$source_root/include" \
  "$crate_dir/tests/smoke_cpp.cpp" \
  "$work_dir/gwipc_capi_shim.o" \
  "$work_dir/gwipc_capi_test_message.o" \
  "$target_dir/debug/libgw_ipc_capi.a" \
  -ldl -lpthread -lm \
  -o "$work_dir/gwipc-capi-smoke-cpp"

readelf --wide --dyn-syms "$work_dir/libgwipc-capi-smoke.so" | \
  grep -F 'gwipc_get_api_version@@GWIPC_0.1' >/dev/null
readelf --wide --dyn-syms "$work_dir/libgwipc-capi-smoke.so" | \
  grep -F 'gwipc_contract_encode_surface_remove@@GWIPC_0.2' >/dev/null
if nm -D --defined-only "$work_dir/libgwipc-capi-smoke.so" | \
    grep -F 'gwipc_rust_' >/dev/null; then
  echo "internal Rust bridge symbol escaped the versioned C shim" >&2
  exit 1
fi
nm -D --defined-only "$target_dir/debug/libgw_ipc_capi.so" | \
  grep -F 'gwipc_rust_get_api_version' >/dev/null

"$work_dir/gwipc-capi-smoke-c"
"$work_dir/gwipc-capi-smoke-cpp"
