#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
PICO_HOME="${PICO_HOME:-${HOME}/.pico-sdk}"

die() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }

newest_match() {
    find $1 -type f -print 2>/dev/null | sort -V | tail -n 1
}

find_sdk() {
    if [[ -n "${PICO_SDK_PATH:-}" && -f "${PICO_SDK_PATH}/pico_sdk_init.cmake" ]]; then
        printf '%s\n' "${PICO_SDK_PATH}"
    else
        local path
        path="$(newest_match "${PICO_HOME}/sdk/*/pico_sdk_init.cmake")"
        [[ -n "${path}" ]] && dirname "${path}"
    fi
}

find_cmake() { command -v cmake 2>/dev/null || newest_match "${PICO_HOME}/cmake/*/bin/cmake"; }
find_ninja() { command -v ninja 2>/dev/null || newest_match "${PICO_HOME}/ninja/*/ninja"; }
find_picotool() {
    command -v picotool 2>/dev/null || newest_match "${PICO_HOME}/picotool/*/picotool/picotool"
}
find_toolchain() {
    local triple="$1" path
    path="$(newest_match "${PICO_HOME}/toolchain/*/bin/${triple}-gcc")"
    [[ -n "${path}" ]] && dirname "$(dirname "${path}")"
}

source_identity() {
    local sha dirty=""
    sha="$(git -C "${PROJECT_ROOT}" rev-parse --short=12 HEAD 2>/dev/null || printf 'uncommitted')"
    [[ -z "$(git -C "${PROJECT_ROOT}" status --porcelain -- src tools 2>/dev/null)" ]] || dirty=-dirty
    printf '%s%s\n' "${sha}" "${dirty}"
}

target_name() {
    case "${1:-shootout}" in
        shootout) printf 'crypto_shootout\n' ;;
        sweep) printf 'crypto_sweep\n' ;;
        *) die "target must be shootout or sweep" ;;
    esac
}

