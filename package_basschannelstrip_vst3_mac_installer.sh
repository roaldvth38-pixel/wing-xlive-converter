#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}" && pwd)"

VERSION="0.1.0"
BUILD_CONFIG="Release"
BUILD_PRESET=""
BUILD_DIR="${REPO_ROOT}/build"
SKIP_BUILD=0
SKIP_INSTALLER=0
OUTPUT_DIR="${REPO_ROOT}/release/installer-macos"
PLUGIN_TARGET="BassChannelStrip_VST3"
PLUGIN_BUNDLE_NAME="Bass Channel Strip.vst3"
INSTALL_BUNDLE_NAME=""
PACKAGE_NAME="BassChannelStrip_VST3"
PKG_ID="com.audioplugincreator.basschannelstrip.vst3"
INSTALL_LOCATION="/Library/Audio/Plug-Ins/VST3"
SIGN_APP_IDENTITY=""
SIGN_INSTALLER_IDENTITY=""

usage() {
  cat <<'EOF'
Package Bass Channel Strip VST3 as a macOS installer (.pkg).

Usage:
  ./package_basschannelstrip_vst3_mac_installer.sh [options]

Options:
  --version <semver>                 Version for generated package (default: 0.1.0)
  --config <Release|Debug>           Build configuration (default: Release)
  --build-preset <name>              CMake build preset to use (optional)
  --build-dir <path>                 CMake build directory (default: ./build)
  --plugin-target <target>           CMake VST3 target (default: BassChannelStrip_VST3)
  --plugin-bundle-name <name.vst3>   Source bundle folder name from build output (default: Bass Channel Strip.vst3)
  --install-bundle-name <name.vst3>  Bundle folder name written into installer (default: source bundle name)
  --package-name <name>              Package file prefix (default: BassChannelStrip_VST3)
  --pkg-id <identifier>              pkgbuild identifier (default: com.audioplugincreator.basschannelstrip.vst3)
  --output-dir <path>                Installer output directory
  --skip-build                       Skip CMake build step
  --skip-installer                   Validate build artifact only
  --sign-app-identity <identity>     codesign identity for VST3 bundle (optional)
  --sign-installer-identity <id>     productbuild signing identity (optional)
  -h, --help                         Show this help

Notes:
  - Run this script on macOS.
  - Requires: cmake, pkgbuild, productbuild
  - The plugin bundle is expected at:
    build/<target_without_VST3>_artefacts/<config>/VST3/<bundle_name>.vst3
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --version)
      VERSION="$2"
      shift 2
      ;;
    --config)
      BUILD_CONFIG="$2"
      shift 2
      ;;
    --build-preset)
      BUILD_PRESET="$2"
      shift 2
      ;;
    --build-dir)
      BUILD_DIR="$2"
      shift 2
      ;;
    --plugin-target)
      PLUGIN_TARGET="$2"
      shift 2
      ;;
    --plugin-bundle-name)
      PLUGIN_BUNDLE_NAME="$2"
      shift 2
      ;;
    --install-bundle-name)
      INSTALL_BUNDLE_NAME="$2"
      shift 2
      ;;
    --package-name)
      PACKAGE_NAME="$2"
      shift 2
      ;;
    --pkg-id)
      PKG_ID="$2"
      shift 2
      ;;
    --output-dir)
      OUTPUT_DIR="$2"
      shift 2
      ;;
    --skip-build)
      SKIP_BUILD=1
      shift
      ;;
    --skip-installer)
      SKIP_INSTALLER=1
      shift
      ;;
    --sign-app-identity)
      SIGN_APP_IDENTITY="$2"
      shift 2
      ;;
    --sign-installer-identity)
      SIGN_INSTALLER_IDENTITY="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage
      exit 1
      ;;
  esac
done

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "This script must be run on macOS." >&2
  exit 1
fi

if [[ -z "${INSTALL_BUNDLE_NAME}" ]]; then
  INSTALL_BUNDLE_NAME="${PLUGIN_BUNDLE_NAME}"
fi

TARGET_STEM="${PLUGIN_TARGET%_VST3}"
ARTEFACTS_DIR="${TARGET_STEM}_artefacts"
PLUGIN_BUNDLE_LEAF="${PLUGIN_BUNDLE_NAME}"
INSTALL_BUNDLE_LEAF="${INSTALL_BUNDLE_NAME}"

if [[ ${SKIP_BUILD} -eq 0 ]]; then
  echo "Building ${PLUGIN_TARGET} (${BUILD_CONFIG}) ..."
  if [[ -n "${BUILD_PRESET}" ]]; then
    cmake --build --preset "${BUILD_PRESET}" --config "${BUILD_CONFIG}" --target "${PLUGIN_TARGET}"
  else
    cmake --build "${BUILD_DIR}" --config "${BUILD_CONFIG}" --target "${PLUGIN_TARGET}"
  fi
fi

PLUGIN_BUNDLE="${BUILD_DIR}/${ARTEFACTS_DIR}/${BUILD_CONFIG}/VST3/${PLUGIN_BUNDLE_LEAF}"
if [[ ! -d "${PLUGIN_BUNDLE}" ]]; then
  PLUGIN_BUNDLE="${BUILD_DIR}/${ARTEFACTS_DIR}/VST3/${PLUGIN_BUNDLE_LEAF}"
fi

if [[ ! -d "${PLUGIN_BUNDLE}" ]]; then
  echo "VST3 bundle not found: ${PLUGIN_BUNDLE}" >&2
  exit 1
fi

echo "Packaging source bundle: ${PLUGIN_BUNDLE_LEAF}"
echo "Installer bundle name: ${INSTALL_BUNDLE_LEAF}"

if [[ ${SKIP_INSTALLER} -eq 1 ]]; then
  echo "Build artifact verified. Skipping installer generation."
  echo "Bundle: ${PLUGIN_BUNDLE}"
  exit 0
fi

if ! command -v pkgbuild >/dev/null 2>&1; then
  echo "pkgbuild not found. Install Xcode command line tools." >&2
  exit 1
fi

if ! command -v productbuild >/dev/null 2>&1; then
  echo "productbuild not found. Install Xcode command line tools." >&2
  exit 1
fi

TMP_ROOT="$(mktemp -d)"
STAGING_ROOT="${TMP_ROOT}/staging"
mkdir -p "${STAGING_ROOT}${INSTALL_LOCATION}"

INSTALL_BUNDLE_PATH="${STAGING_ROOT}${INSTALL_LOCATION}/${INSTALL_BUNDLE_LEAF}"
cp -R "${PLUGIN_BUNDLE}" "${INSTALL_BUNDLE_PATH}"

if [[ -n "${SIGN_APP_IDENTITY}" ]]; then
  echo "Codesigning plugin bundle ..."
  codesign --force --deep --options runtime --sign "${SIGN_APP_IDENTITY}" "${INSTALL_BUNDLE_PATH}"
fi

mkdir -p "${OUTPUT_DIR}"
TIMESTAMP="$(date +%Y%m%d_%H%M%S)"
COMPONENT_PKG="${TMP_ROOT}/${PACKAGE_NAME}_component.pkg"
FINAL_PKG="${OUTPUT_DIR}/${PACKAGE_NAME}_${VERSION}_${TIMESTAMP}.pkg"

pkgbuild \
  --root "${STAGING_ROOT}" \
  --identifier "${PKG_ID}" \
  --version "${VERSION}" \
  "${COMPONENT_PKG}"

if [[ -n "${SIGN_INSTALLER_IDENTITY}" ]]; then
  productbuild \
    --package "${COMPONENT_PKG}" \
    --sign "${SIGN_INSTALLER_IDENTITY}" \
    "${FINAL_PKG}"
else
  productbuild \
    --package "${COMPONENT_PKG}" \
    "${FINAL_PKG}"
fi

echo "Created installer: ${FINAL_PKG}"

rm -rf "${TMP_ROOT}"