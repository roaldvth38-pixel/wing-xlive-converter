#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="${SCRIPT_DIR}"

PYTHON_EXE=""
SKIP_BUILD=0
APP_VERSION="1.0.0"
SIGN_APP_IDENTITY=""
SIGN_INSTALLER_IDENTITY=""
NOTARIZE_PROFILE=""
APP_NAME="WingXLIVEConverter"
APP_BUNDLE="${REPO_ROOT}/dist/${APP_NAME}.app"
OUTPUT_DIR="${REPO_ROOT}/release/installer-macos"

print_help() {
  cat <<'EOF'
Package Wing X-LIVE Converter into a macOS installer package (.pkg)

Usage:
  ./package_wing_xlive_converter_mac_installer.sh [options]

Options:
  --python-exe PATH              Python interpreter for app build
  --skip-build                   Use existing dist/WingXLIVEConverter.app
  --app-version VERSION          Version string for package metadata (default: 1.0.0)
  --sign-app-identity NAME       Developer ID Application identity for codesigning the app
  --sign-installer-identity NAME Developer ID Installer identity for signing the pkg
  --notarize-profile PROFILE     Keychain profile for xcrun notarytool submit --keychain-profile
  --help                         Show this help message

Examples:
  ./package_wing_xlive_converter_mac_installer.sh
  ./package_wing_xlive_converter_mac_installer.sh \
    --sign-app-identity "Developer ID Application: Your Company (TEAMID)" \
    --sign-installer-identity "Developer ID Installer: Your Company (TEAMID)" \
    --notarize-profile AC_NOTARY

Output:
  release/installer-macos/WingXLIVEConverter-macOS-<version>.pkg
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --python-exe)
      PYTHON_EXE="${2:-}"
      shift 2
      ;;
    --skip-build)
      SKIP_BUILD=1
      shift
      ;;
    --app-version)
      APP_VERSION="${2:-}"
      shift 2
      ;;
    --sign-app-identity)
      SIGN_APP_IDENTITY="${2:-}"
      shift 2
      ;;
    --sign-installer-identity)
      SIGN_INSTALLER_IDENTITY="${2:-}"
      shift 2
      ;;
    --notarize-profile)
      NOTARIZE_PROFILE="${2:-}"
      shift 2
      ;;
    --help|-h)
      print_help
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      print_help
      exit 1
      ;;
  esac
done

if [[ "${SKIP_BUILD}" -eq 0 ]]; then
  BUILD_CMD=("${REPO_ROOT}/build_wing_xlive_converter_mac_app.sh")
  if [[ -n "${PYTHON_EXE}" ]]; then
    BUILD_CMD+=("--python-exe" "${PYTHON_EXE}")
  fi
  "${BUILD_CMD[@]}"
fi

if [[ ! -d "${APP_BUNDLE}" ]]; then
  echo "Missing app bundle: ${APP_BUNDLE}" >&2
  echo "Run ./build_wing_xlive_converter_mac_app.sh first or remove --skip-build." >&2
  exit 1
fi

if [[ -n "${SIGN_APP_IDENTITY}" ]]; then
  echo "Codesigning app bundle..."
  codesign \
    --force \
    --deep \
    --options runtime \
    --timestamp \
    --sign "${SIGN_APP_IDENTITY}" \
    "${APP_BUNDLE}"

  echo "Verifying app signature..."
  codesign --verify --deep --strict --verbose=2 "${APP_BUNDLE}"
fi

mkdir -p "${OUTPUT_DIR}"
PKG_PATH="${OUTPUT_DIR}/${APP_NAME}-macOS-${APP_VERSION}.pkg"

PRODUCTBUILD_CMD=(
  productbuild
  --component "${APP_BUNDLE}" /Applications
  "${PKG_PATH}"
)

if [[ -n "${SIGN_INSTALLER_IDENTITY}" ]]; then
  PRODUCTBUILD_CMD=(
    productbuild
    --sign "${SIGN_INSTALLER_IDENTITY}"
    --component "${APP_BUNDLE}" /Applications
    "${PKG_PATH}"
  )
fi

echo "Building pkg installer..."
"${PRODUCTBUILD_CMD[@]}"

if [[ -n "${NOTARIZE_PROFILE}" ]]; then
  echo "Submitting pkg for notarization..."
  xcrun notarytool submit "${PKG_PATH}" --keychain-profile "${NOTARIZE_PROFILE}" --wait

  echo "Stapling notarization ticket..."
  xcrun stapler staple "${PKG_PATH}"
fi

echo "macOS installer created: ${PKG_PATH}"
