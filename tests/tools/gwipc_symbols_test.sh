#!/usr/bin/env bash
set -euo pipefail

library=${1:?usage: gwipc_symbols_test.sh LIBGWIPC}
symbols="$(nm -D --defined-only "$library" | awk '{ print $3 }' | sed '/^$/d')"
unexpected="$(printf '%s\n' "$symbols" | grep -v -E '^gwipc_|^GWIPC_' || true)"

if [[ -n $unexpected ]]; then
  printf 'unexpected exported symbols:\n%s\n' "$unexpected" >&2
  exit 1
fi

for symbol in gwipc_get_api_version gwipc_listener_create \
  gwipc_connection_connect gwipc_connection_enqueue gwipc_message_destroy; do
  printf '%s\n' "$symbols" | grep -Eq "^${symbol}(@@GWIPC_0\\.1)?$" || {
    printf 'missing public symbol: %s\n' "$symbol" >&2
    exit 1
  }
done

for symbol in gwipc_contract_encode_policy_lifecycle_window_upsert \
  gwipc_contract_encode_surface_policy_upsert \
  gwipc_decoded_policy_lifecycle_window_upsert \
  gwipc_decoded_surface_policy_upsert \
  gwipc_connection_enqueue_with_sequence; do
  printf '%s\n' "$symbols" | grep -Eq "^${symbol}@@GWIPC_0\\.4$" || {
    printf 'missing API 0.4 symbol version: %s\n' "$symbol" >&2
    exit 1
  }
done
