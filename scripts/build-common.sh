#!/usr/bin/env bash

project_dir() {
  cd "$(dirname "${BASH_SOURCE[1]}")/.." && pwd
}

require_command() {
  local command_name="$1"
  local install_hint="${2:-}"

  if command -v "${command_name}" >/dev/null 2>&1; then
    return 0
  fi

  if [[ -n "${install_hint}" ]]; then
    echo "${command_name} not found. Install with: ${install_hint}" >&2
  else
    echo "${command_name} not found" >&2
  fi
  exit 1
}

require_option_value() {
  local option_name="$1"
  local remaining_count="$2"

  if [[ "${remaining_count}" -lt 2 ]]; then
    echo "${option_name} requires a value" >&2
    return 1
  fi
}

prepare_build_dir() {
  local build_dir="$1"
  local reconfigure="$2"
  local required_generator="${3:-}"

  if [[ -f "${build_dir}/CMakeCache.txt" && -n "${required_generator}" ]]; then
    if ! grep -Fxq "CMAKE_GENERATOR:INTERNAL=${required_generator}" "${build_dir}/CMakeCache.txt"; then
      if [[ "${reconfigure}" == "1" ]]; then
        rm -rf "${build_dir}"
      else
        echo "${build_dir} was configured with a non-${required_generator} generator." >&2
        echo "Run with --reconfigure to recreate it as a ${required_generator} build directory." >&2
        exit 1
      fi
    fi
  fi

  if [[ "${reconfigure}" == "1" && -d "${build_dir}" ]]; then
    rm -rf "${build_dir}"
  fi
}

print_existing_build_dir_message() {
  local build_dir="$1"

  echo "Using existing CMake build directory: ${build_dir}"
  echo "Pass --reconfigure if you need to regenerate CMake files."
}
