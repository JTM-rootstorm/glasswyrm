#!/usr/bin/env bash
set -euo pipefail

readonly BASE_COMMIT='9c1cbfb72858b8307f9d9d0a6dc53ac1235ecba0'
readonly DEFAULT_LIMIT=1000
readonly MATERIAL_LIMIT=600
readonly COORDINATOR_LIMIT=500
readonly MAIN_LIMIT=250
readonly FUNCTION_TARGET=100
readonly FUNCTION_REVIEW_THRESHOLD=150

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(cd -- "${script_dir}/../.." && pwd)
allowlist_file="${repo_root}/docs/maintenance/source_size_allowlist.txt"
errors=0
reviews=0

fail() {
  printf 'source-layout: error: %s\n' "$*" >&2
  errors=$((errors + 1))
}

trim() {
  local value=$1
  value=${value#"${value%%[![:space:]]*}"}
  value=${value%"${value##*[![:space:]]}"}
  printf '%s' "${value}"
}

is_source_file() {
  case "$1" in
    *.c|*.cc|*.cpp|*.cxx|*.h|*.hh|*.hpp|*.hxx) return 0 ;;
    *) return 1 ;;
  esac
}

physical_lines() {
  awk 'END { print NR }' "$1"
}

declare -A allowed_lines=()
declare -A seen_allowlist=()

if [[ ! -f ${allowlist_file} ]]; then
  printf 'source-layout: missing allowlist: %s\n' "${allowlist_file}" >&2
  exit 1
fi

while IFS='|' read -r raw_path raw_lines raw_responsibility raw_reason raw_revisit raw_extra; do
  path=$(trim "${raw_path}")
  [[ -z ${path} || ${path:0:1} == '#' ]] && continue

  lines=$(trim "${raw_lines:-}")
  responsibility=$(trim "${raw_responsibility:-}")
  reason=$(trim "${raw_reason:-}")
  revisit=$(trim "${raw_revisit:-}")
  extra=$(trim "${raw_extra:-}")

  if [[ -n ${extra} || -z ${lines} || -z ${responsibility} || -z ${reason} || -z ${revisit} ]]; then
    fail "malformed allowlist entry for '${path}'; expected exactly five non-empty fields"
    continue
  fi
  if [[ ! ${lines} =~ ^[0-9]+$ ]]; then
    fail "allowlist count for '${path}' is not an integer: '${lines}'"
    continue
  fi
  if [[ ${path} != src/* ]] || ! is_source_file "${path}"; then
    fail "allowlist path is not a src C/C++ file: '${path}'"
    continue
  fi
  if [[ -n ${allowed_lines[${path}]+set} ]]; then
    fail "duplicate allowlist entry: '${path}'"
    continue
  fi
  allowed_lines["${path}"]=${lines}
done < "${allowlist_file}"

baseline_available=1
if ! git -C "${repo_root}" cat-file -e "${BASE_COMMIT}^{commit}" 2>/dev/null; then
  baseline_available=0
  printf 'source-layout: note: baseline commit unavailable; enforcing the 600-line material-change budget on every non-allowlisted file\n' >&2
fi

is_materially_rewritten() {
  local path=$1
  local baseline_lines=$2
  local added=0
  local deleted=0
  local numstat

  numstat=$(git -C "${repo_root}" diff --numstat "${BASE_COMMIT}" -- "${path}" | tail -n 1)
  [[ -z ${numstat} ]] && return 1
  IFS=$'\t' read -r added deleted _ <<< "${numstat}"
  [[ ${added} =~ ^[0-9]+$ && ${deleted} =~ ^[0-9]+$ ]] || return 1

  # Half of the baseline file changing is a deliberately conservative,
  # deterministic definition of "materially rewritten" for this guard.
  (( (added + deleted) * 2 >= baseline_lines ))
}

scan_functions_for_review() {
  local absolute_path=$1
  local relative_path=$2
  local findings

  # This lexical pass is advisory: 100 lines is a design target, while crossing
  # 150 lines requires review. Hard failures are the objectively enforceable file
  # budgets below. The scanner intentionally ignores declarations and control
  # statements and may miss macro-generated functions, which are prohibited by
  # the milestone's review rules in any event.
  findings=$(awk -v target="${FUNCTION_TARGET}" -v review="${FUNCTION_REVIEW_THRESHOLD}" '
    function braces(text, character, copy) {
      copy = text
      return gsub(character, "", copy)
    }
    function control(text) {
      return text ~ /^[[:space:]]*(if|for|while|switch|catch)[[:space:]]*\(/
    }
    {
      text = $0
      sub(/\/\/.*/, "", text)

      if (!in_function && !control(text) && text !~ /;[[:space:]]*$/ &&
          text !~ /#[[:space:]]*define/ && text !~ /\[[^]]*\][[:space:]]*\(/) {
        if (candidate == 0 && text ~ /[[:alnum:]_~>:*&][[:space:]]*\(/)
          candidate = NR
        if (candidate != 0 && text ~ /\{/) {
          in_function = 1
          function_start = candidate
          function_depth = depth
          candidate = 0
        } else if (candidate != 0 && text ~ /;/) {
          candidate = 0
        }
      }

      depth += braces(text, "\\{")
      depth -= braces(text, "\\}")

      if (in_function && depth == function_depth) {
        span = NR - function_start + 1
        if (span > target)
          printf "%d:%d:%s\n", function_start, span,
                 (span > review ? "review" : "target")
        in_function = 0
      }
    }
  ' "${absolute_path}")

  while IFS=: read -r line length kind; do
    [[ -z ${line} ]] && continue
    if [[ ${kind} == review ]]; then
      printf 'source-layout: review: %s:%s spans approximately %s lines (review threshold >%s)\n' \
        "${relative_path}" "${line}" "${length}" "${FUNCTION_REVIEW_THRESHOLD}" >&2
      reviews=$((reviews + 1))
    else
      printf 'source-layout: note: %s:%s spans approximately %s lines (ordinary target <=%s)\n' \
        "${relative_path}" "${line}" "${length}" "${FUNCTION_TARGET}" >&2
    fi
  done <<< "${findings}"
}

while IFS= read -r -d '' absolute_path; do
  path=${absolute_path#"${repo_root}/"}
  lines=$(physical_lines "${absolute_path}")
  introduced=0
  material=0

  if (( ! baseline_available )); then
    # Release and VM source exports intentionally omit .git. Applying the
    # 600-line budget to every source file is a conservative substitute for
    # baseline-aware change classification.
    material=1
  elif ! git -C "${repo_root}" cat-file -e "${BASE_COMMIT}:${path}" 2>/dev/null; then
    introduced=1
  else
    baseline_lines=$(git -C "${repo_root}" show "${BASE_COMMIT}:${path}" | awk 'END { print NR }')
    if is_materially_rewritten "${path}" "${baseline_lines}"; then
      material=1
    fi
  fi

  if (( lines > DEFAULT_LIMIT )); then
    if [[ -z ${allowed_lines[${path}]+set} ]]; then
      fail "${path} has ${lines} lines; hard default is ${DEFAULT_LIMIT} and no exception is recorded"
    elif (( allowed_lines[${path}] != lines )); then
      fail "${path} has ${lines} lines; allowlist records stale count ${allowed_lines[${path}]}"
    else
      seen_allowlist["${path}"]=1
    fi
  elif [[ -n ${allowed_lines[${path}]+set} ]]; then
    fail "${path} is allowlisted at ${allowed_lines[${path}]} lines but is now ${lines}; remove the stale exception"
    seen_allowlist["${path}"]=1
  fi

  if (( (introduced || material) && lines > MATERIAL_LIMIT )); then
    fail "${path} has ${lines} lines; new/materially rewritten files are limited to ${MATERIAL_LIMIT}"
  fi

  case "${path}" in
    */main.cpp)
      (( lines > MAIN_LIMIT )) && fail "${path} has ${lines} lines; top-level main.cpp limit is ${MAIN_LIMIT}"
      ;;
  esac

  case "${path}" in
    src/glasswyrmd/server.cpp|*/runtime.cpp|*_coordinator.cpp)
      (( lines > COORDINATOR_LIMIT )) && fail "${path} has ${lines} lines; coordinator limit is ${COORDINATOR_LIMIT}"
      ;;
  esac

  case "${path}" in
    src/glasswyrmd/request_dispatcher.cpp)
      (( lines > 450 )) && fail "${path} has ${lines} lines; routing-shell limit is 450"
      ;;
    src/glasswyrmd/resource_table.cpp|src/glasswyrmd/server.cpp)
      (( lines > 500 )) && fail "${path} has ${lines} lines; final limit is 500"
      ;;
    src/gwcomp/compositor.cpp)
      (( lines > 600 )) && fail "${path} has ${lines} lines; final limit is 600"
      ;;
    src/ipc/connection.cpp)
      (( lines > 350 )) && fail "${path} has ${lines} lines; M11 connection-shell limit is 350"
      ;;
  esac

  if (( introduced )) && [[ -n ${allowed_lines[${path}]+set} ]]; then
    fail "new source file may not be allowlisted: ${path}"
  fi

  case "${path}" in
    *.c|*.cc|*.cpp|*.cxx) scan_functions_for_review "${absolute_path}" "${path}" ;;
  esac
done < <(find "${repo_root}/src" -type f \
  \( -name '*.c' -o -name '*.cc' -o -name '*.cpp' -o -name '*.cxx' \
     -o -name '*.h' -o -name '*.hh' -o -name '*.hpp' -o -name '*.hxx' \) \
  -print0 | sort -z)

for path in "${!allowed_lines[@]}"; do
  if [[ ! -f ${repo_root}/${path} ]]; then
    fail "allowlisted file does not exist: ${path}"
  elif [[ -z ${seen_allowlist[${path}]+set} ]]; then
    # An entry can only remain unseen after an earlier malformed-state error.
    fail "allowlist entry was not exercised: ${path}"
  fi
done

if (( errors != 0 )); then
  printf 'source-layout: FAIL (%d error(s), %d function review item(s))\n' \
    "${errors}" "${reviews}" >&2
  exit 1
fi

printf 'source-layout: PASS (%d function review item(s))\n' "${reviews}"
