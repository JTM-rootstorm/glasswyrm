#!/usr/bin/env bash
set -euo pipefail

repo_root=$1
tool=${repo_root}/tools/gw-m14-dev
temporary=$(mktemp -d)
trap 'rm -rf -- "${temporary}"' EXIT

if "${tool}" >"${temporary}/missing.log" 2>&1; then
  printf 'gw-m14-dev accepted a missing command\n' >&2
  exit 1
fi
grep -F 'configure-probe BUILD' "${temporary}/missing.log" >/dev/null

if "${tool}" timing /var/tmp/glasswyrm-build-m14 \
    >"${temporary}/physical.log" 2>&1; then
  printf 'gw-m14-dev accepted the frozen physical build\n' >&2
  exit 1
fi
grep -F 'refusing to touch' "${temporary}/physical.log" >/dev/null

mkdir -p "${temporary}/bin" "${temporary}/build/meson-private"
: > "${temporary}/build/meson-private/coredata.dat"
cat >"${temporary}/bin/meson" <<'EOF'
#!/usr/bin/env bash
printf '%s\n' "$*" >>"${M14_DEV_TEST_LOG}"
EOF
chmod +x "${temporary}/bin/meson"
export M14_DEV_TEST_LOG=${temporary}/meson.log
PATH=${temporary}/bin:${PATH} "${tool}" timing "${temporary}/build" \
  >"${temporary}/timing.log"
grep -F 'compile -C' "${temporary}/meson.log" | grep -F 'drm_vrr_timing drm_fake_events' >/dev/null
grep -F 'test -C' "${temporary}/meson.log" | grep -F -- '--no-rebuild' |
  grep -F 'drm-vrr-timing drm-fake-events' >/dev/null
if grep -F 'm14-vrr-180-frame-integrated' "${temporary}/meson.log" >/dev/null; then
  printf 'narrow timing gate selected broad integration work\n' >&2
  exit 1
fi

: >"${temporary}/meson.log"
PATH=${temporary}/bin:${PATH} "${tool}" integration "${temporary}/build" \
  >"${temporary}/integration.log"
grep -F 'm14-vrr-180-frame-integrated' "${temporary}/meson.log" >/dev/null
grep -F 'source-layout' "${temporary}/meson.log" >/dev/null

printf 'M14 developer gate CLI contract: ok\n'
