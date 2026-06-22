#!/usr/bin/env bash
set -euo pipefail

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/build-common.sh"

PROJECT_DIR="$(project_dir)"
BUILD_DIR="${PROJECT_DIR}/build/debug"
CONFIG=Debug
GENERATOR=""
BUILD_TESTING=ON
CODE_SIGN_APP=""
RECONFIGURE=0
VERIFY=0

usage() {
  cat >&2 <<EOF
Usage: $0 [options]

Options:
  --verify          Build all targets and run tests.
  --reconfigure     Regenerate the build directory.
  --build-dir DIR   Override the build directory.
  --config CONFIG   CMake build type/configuration. Defaults to Debug.
  --generator GEN   CMake generator to use, for example Ninja.
  --no-code-sign    Configure ComputerCpp without the macOS post-build code-sign step.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --verify)
      VERIFY=1
      ;;
    --reconfigure)
      RECONFIGURE=1
      ;;
    --build-dir)
      if ! require_option_value "--build-dir" "$#"; then
        usage
        exit 1
      fi
      BUILD_DIR="$2"
      shift
      ;;
    --config)
      if ! require_option_value "--config" "$#"; then
        usage
        exit 1
      fi
      CONFIG="$2"
      shift
      ;;
    --generator)
      if ! require_option_value "--generator" "$#"; then
        usage
        exit 1
      fi
      GENERATOR="$2"
      shift
      ;;
    --no-code-sign)
      CODE_SIGN_APP=OFF
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage
      exit 1
      ;;
  esac
  shift
done

cd "${PROJECT_DIR}"

require_command cmake

prepare_build_dir "${BUILD_DIR}" "${RECONFIGURE}" "${GENERATOR}"

if [[ ! -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
  CMAKE_ARGS=(
    -S "${PROJECT_DIR}"
    -B "${BUILD_DIR}"
    -DCMAKE_BUILD_TYPE="${CONFIG}"
    -DBUILD_TESTING="${BUILD_TESTING}"
  )

  if [[ -n "${GENERATOR}" ]]; then
    CMAKE_ARGS+=(-G "${GENERATOR}")
  fi

  if [[ -n "${CODE_SIGN_APP}" ]]; then
    CMAKE_ARGS+=(-DCOMPUTER_CPP_CODE_SIGN_APP="${CODE_SIGN_APP}")
  fi

  if command -v ccache >/dev/null 2>&1; then
    CMAKE_ARGS+=(
      -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
      -DCMAKE_OBJCXX_COMPILER_LAUNCHER=ccache
    )
  fi

  cmake "${CMAKE_ARGS[@]}"
else
  print_existing_build_dir_message "${BUILD_DIR}"
fi

cmake --build "${BUILD_DIR}" --target all --config "${CONFIG}"

if [[ "${VERIFY}" == "1" ]]; then
  ctest --test-dir "${BUILD_DIR}" --build-config "${CONFIG}" --output-on-failure
fi
