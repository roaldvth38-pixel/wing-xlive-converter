#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="${SCRIPT_DIR}"

PYTHON_EXE=""
APP_NAME="WingXLIVEConverter"

print_help() {
  cat <<'EOF'
Build Wing X-LIVE Converter macOS app bundle

Usage:
  ./build_wing_xlive_converter_mac_app.sh [--python-exe /path/to/python]

Options:
  --python-exe PATH   Python interpreter to use
  --help              Show this help message

Output:
  dist/WingXLIVEConverter.app
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --python-exe)
      PYTHON_EXE="${2:-}"
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

resolve_python() {
  if [[ -n "${PYTHON_EXE}" && -x "${PYTHON_EXE}" ]]; then
    echo "${PYTHON_EXE}"
    return
  fi

  local candidates=(
    "${REPO_ROOT}/.venv-1/bin/python"
    "${REPO_ROOT}/.venv/bin/python"
  )

  for p in "${candidates[@]}"; do
    if [[ -x "${p}" ]]; then
      echo "${p}"
      return
    fi
  done

  if command -v python3 >/dev/null 2>&1; then
    command -v python3
    return
  fi

  if command -v python >/dev/null 2>&1; then
    command -v python
    return
  fi

  echo "No Python interpreter found. Pass --python-exe." >&2
  exit 1
}

PYTHON_BIN="$(resolve_python)"
echo "Using Python: ${PYTHON_BIN}"

"${PYTHON_BIN}" - <<'PY'
import importlib.util
import subprocess
import sys

required = ["pyinstaller", "numpy", "soundfile", "lameenc", "pillow"]
missing = [name for name in required if importlib.util.find_spec(name) is None]
if missing:
    print("Installing missing packages:", ", ".join(missing))
    subprocess.check_call([sys.executable, "-m", "pip", "install", *missing])
PY

pushd "${REPO_ROOT}" >/dev/null

"${PYTHON_BIN}" -m PyInstaller \
  --noconfirm \
  --clean \
  --windowed \
  --name "${APP_NAME}" \
  --add-data "assets:assets" \
  wing_xlive_converter_gui.py

APP_BUNDLE="${REPO_ROOT}/dist/${APP_NAME}.app"
if [[ ! -d "${APP_BUNDLE}" ]]; then
  echo "Expected app bundle was not created: ${APP_BUNDLE}" >&2
  exit 1
fi

FFMPEG_SRC=""
if [[ -f "${REPO_ROOT}/dist/ffmpeg" ]]; then
  FFMPEG_SRC="${REPO_ROOT}/dist/ffmpeg"
elif [[ -f "${REPO_ROOT}/dist/ffmpeg.exe" ]]; then
  FFMPEG_SRC="${REPO_ROOT}/dist/ffmpeg.exe"
elif [[ -f "${REPO_ROOT}/ffmpeg" ]]; then
  FFMPEG_SRC="${REPO_ROOT}/ffmpeg"
elif command -v ffmpeg >/dev/null 2>&1; then
  FFMPEG_SRC="$(command -v ffmpeg)"
fi

if [[ -n "${FFMPEG_SRC}" ]]; then
  cp "${FFMPEG_SRC}" "${APP_BUNDLE}/Contents/MacOS/ffmpeg"
  chmod +x "${APP_BUNDLE}/Contents/MacOS/ffmpeg"
  echo "Bundled ffmpeg from: ${FFMPEG_SRC}"
else
  echo "Warning: ffmpeg not found. FLAC/MP3 will rely on in-process fallback."
fi

if [[ -f "${REPO_ROOT}/FFMPEG_LICENSE.txt" ]]; then
  cp "${REPO_ROOT}/FFMPEG_LICENSE.txt" "${APP_BUNDLE}/Contents/Resources/FFMPEG_LICENSE.txt"
elif [[ -f "${REPO_ROOT}/dist/FFMPEG_LICENSE.txt" ]]; then
  cp "${REPO_ROOT}/dist/FFMPEG_LICENSE.txt" "${APP_BUNDLE}/Contents/Resources/FFMPEG_LICENSE.txt"
fi

echo "Build complete: ${APP_BUNDLE}"

popd >/dev/null
