#!/usr/bin/env bash
# Build the highmap-wasm artifact set into dist/ (design D10).
#
# Toolchain: Emscripten 6.0.13 (pinned). The script looks for emcc on PATH,
# then in $EMSDK (e.g. C:\WS\tools\emsdk). To install:
#   git clone --depth 1 https://github.com/emscripten-core/emsdk.git
#   ./emsdk install 6.0.13 && ./emsdk activate 6.0.13
# and point $EMSDK at the checkout (or source emsdk_env.sh yourself).
#
# Usage: scripts/build-wasm.sh [--no-tests]

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build/wasm"
DIST_DIR="${REPO_ROOT}/dist"
EMSDK_VERSION="6.0.13"

# --- locate the Emscripten toolchain -----------------------------------------
# Modern emsdk ships native executables (emcc/emcmake) under
# $EMSDK/upstream/emscripten; add them to PATH directly.
if ! command -v emcc >/dev/null 2>&1; then
  if [ -n "${EMSDK:-}" ] && [ -d "${EMSDK}/upstream/emscripten" ]; then
    export PATH="${EMSDK}/upstream/emscripten:${PATH}"
  else
    echo "error: emcc not found on PATH and EMSDK is not set." >&2
    echo "  Install Emscripten ${EMSDK_VERSION} and export EMSDK=<emsdk-dir>." >&2
    exit 1
  fi
fi

EMCC_VERSION="$(emcc --version | head -n1 | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -n1)"
echo "==> Emscripten ${EMCC_VERSION} (pinned: ${EMSDK_VERSION})"

# --- configure + build --------------------------------------------------------
echo "==> Configuring ${BUILD_DIR}"
emcmake cmake -S "${REPO_ROOT}/HighMap/wasm" -B "${BUILD_DIR}"

echo "==> Building"
cmake --build "${BUILD_DIR}" --parallel

# --- collect artifacts --------------------------------------------------------
mkdir -p "${DIST_DIR}"
cp "${BUILD_DIR}/highmap.js" "${DIST_DIR}/highmap.js"
cp "${BUILD_DIR}/highmap.wasm" "${DIST_DIR}/highmap.wasm"

echo "==> Artifacts in ${DIST_DIR}:"
ls -l "${DIST_DIR}"

if command -v sha256sum >/dev/null 2>&1; then
  sha256sum "${DIST_DIR}/highmap.wasm"
else
  shasum -a 256 "${DIST_DIR}/highmap.wasm" || true
fi

# --- node smoke test ----------------------------------------------------------
if [ "${1:-}" != "--no-tests" ] && command -v node >/dev/null 2>&1; then
  echo "==> Node smoke test"
  DIST_JS="${DIST_DIR}/highmap.js" node --input-type=module -e "
    import { pathToFileURL } from 'node:url';
    const { default: HighmapFactory } = await import(
      pathToFileURL(process.env.DIST_JS).href
    );
    const mod = await HighmapFactory();
    const a = mod.makeArray(4, 4, 0.5);
    if (a.ni() !== 4 || a.nj() !== 4) throw new Error('bad shape');
    const n = mod.noise(mod.NoiseType.PERLIN, 16, 16, 4, 2, 0);
    if (n.ni() !== 16) throw new Error('bad noise shape');
    console.log('smoke test OK: array + noise');
  "
  echo "==> Smoke test passed"
fi
