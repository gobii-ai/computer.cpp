#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${PROJECT_DIR}/build/debug-ninja"
CONFIG=Debug
BUILD_TESTING=ON
CODE_SIGN_APP=ON
CODE_SIGN_IDENTITY=auto
RECONFIGURE=0
LAUNCH=0
VERIFY=0
RELEASE=0

usage() {
  cat >&2 <<EOF
Usage: $0 [options]

Options:
  --launch          Fast inner loop: build ComputerCpp only and relaunch the tray app.
  --verify          Full verification: build all targets and run tests.
  --release         Release build: build computer.cpp and ComputerCpp with tests/signing off.
  --reconfigure     Regenerate the Ninja build directory.
  --build-dir DIR   Override the build directory.
  --config CONFIG   CMake build type/configuration. Defaults to Debug.
  --no-code-sign    Configure ComputerCpp without the local post-build code-sign step.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --reconfigure)
      RECONFIGURE=1
      ;;
    --launch)
      LAUNCH=1
      ;;
    --verify)
      VERIFY=1
      ;;
    --release)
      RELEASE=1
      CONFIG=Release
      BUILD_TESTING=OFF
      CODE_SIGN_APP=OFF
      ;;
    --build-dir)
      if [[ $# -lt 2 ]]; then
        echo "--build-dir requires a path" >&2
        usage
        exit 1
      fi
      BUILD_DIR="$2"
      shift
      ;;
    --config)
      if [[ $# -lt 2 ]]; then
        echo "--config requires a value" >&2
        usage
        exit 1
      fi
      CONFIG="$2"
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

if ! command -v cmake >/dev/null 2>&1; then
  echo "cmake not found. Install with: brew install cmake" >&2
  exit 1
fi

if ! command -v brew >/dev/null 2>&1; then
  echo "Homebrew not found. Install Homebrew first, then run: brew install cmake ninja wxwidgets lua" >&2
  exit 1
fi

if ! command -v ninja >/dev/null 2>&1; then
  echo "ninja not found. Install with: brew install ninja" >&2
  exit 1
fi

if [[ "${BUILD_TESTING}" == "ON" ]] && ! command -v lua >/dev/null 2>&1; then
  echo "lua not found. Install with: brew install lua" >&2
  exit 1
fi

if ! brew list wxwidgets >/dev/null 2>&1; then
  echo "wxwidgets not found. Install with: brew install wxwidgets" >&2
  exit 1
fi

if [[ "${CODE_SIGN_APP}" == "ON" ]]; then
  "${PROJECT_DIR}/scripts/create-local-codesign-identity.sh"
fi

if [[ -f "${BUILD_DIR}/CMakeCache.txt" ]] && ! grep -q '^CMAKE_GENERATOR:INTERNAL=Ninja$' "${BUILD_DIR}/CMakeCache.txt"; then
  if [[ "${RECONFIGURE}" == "1" ]]; then
    rm -rf "${BUILD_DIR}"
  else
    echo "${BUILD_DIR} was configured with a non-Ninja generator." >&2
    echo "Run with --reconfigure to recreate it as a Ninja build directory." >&2
    exit 1
  fi
elif [[ "${RECONFIGURE}" == "1" ]]; then
  rm -rf "${BUILD_DIR}"
fi

if [[ ! -f "${BUILD_DIR}/CMakeCache.txt" || "${RECONFIGURE}" == "1" ]]; then
  BREW_PREFIX="$(brew --prefix)"

  CMAKE_ARGS=(
    -S "${PROJECT_DIR}"
    -B "${BUILD_DIR}"
    -G Ninja
    -DCMAKE_BUILD_TYPE="${CONFIG}"
    -DBUILD_TESTING="${BUILD_TESTING}"
    -DCMAKE_PREFIX_PATH="${BREW_PREFIX}"
    -DCOMPUTER_CPP_CODE_SIGN_APP="${CODE_SIGN_APP}"
    -DCOMPUTER_CPP_CODE_SIGN_IDENTITY="${CODE_SIGN_IDENTITY}"
  )

  if command -v ccache >/dev/null 2>&1; then
    CMAKE_ARGS+=(
      -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
      -DCMAKE_OBJCXX_COMPILER_LAUNCHER=ccache
    )
  fi

  cmake "${CMAKE_ARGS[@]}"
else
  echo "Using existing CMake build directory: ${BUILD_DIR}"
  echo "Pass --reconfigure if you need to regenerate CMake files."
fi

if [[ "${VERIFY}" == "1" ]]; then
  cmake --build "${BUILD_DIR}" --target all --config "${CONFIG}"
  ctest --test-dir "${BUILD_DIR}" --output-on-failure
elif [[ "${RELEASE}" == "1" ]]; then
  cmake --build "${BUILD_DIR}" --target computer.cpp ComputerCpp --config "${CONFIG}"
else
  cmake --build "${BUILD_DIR}" --target ComputerCpp --config "${CONFIG}"
fi

if [[ "${LAUNCH}" == "1" ]]; then
  pkill -x ComputerCpp 2>/dev/null || true
  while pgrep -x ComputerCpp >/dev/null; do
    sleep 0.2
  done
  open -n "${BUILD_DIR}/ComputerCpp.app"
fi

echo
echo "Built:"
echo "  ${BUILD_DIR}/computer.cpp"
echo "  ${BUILD_DIR}/ComputerCpp.app"
echo
echo "Relaunch tray app with:"
echo "  $0 --launch"
echo "Run full verification with:"
echo "  $0 --verify"
