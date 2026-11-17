#!/usr/bin/env bash
set -euo pipefail

# Convenient build script for Test Drive III Enhanced (SDL3).
# Usage:
#   ./build.sh [--debug|--release] [--launcher|--no-launcher] [--clean] [--jobs N] [--run]
#
# Examples:
#   ./build.sh                 # Release build, launcher auto-detected
#   ./build.sh --no-launcher   # Build game only
#   ./build.sh --debug --clean # Clean Debug rebuild
#   ./build.sh --run           # Build (if needed) and start launcher with default game dir
#
# Environment:
#   GAME_DIR   Game data directory passed to --game-dir when using --run (default: ../../../Game)

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"
BUILD_TYPE="Release"
JOBS="${JOBS:-$(command -v nproc >/dev/null 2>&1 && nproc || echo 4)}"
CLEAN=0
RUN_AFTER=0
LAUNCHER_MODE="auto" # auto|on|off

usage() {
  sed -n '1,20p' "$0" | sed 's/^# \{0,1\}//'
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --debug) BUILD_TYPE="Debug"; shift ;;
    --release) BUILD_TYPE="Release"; shift ;;
    --launcher) LAUNCHER_MODE="on"; shift ;;
    --no-launcher) LAUNCHER_MODE="off"; shift ;;
    --clean) CLEAN=1; shift ;;
    --jobs|-j) JOBS="$2"; shift 2 ;;
    --run) RUN_AFTER=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage; exit 1 ;;
  esac
done

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || { echo "Missing required command: $1" >&2; exit 1; }
}

need_cmd cmake
need_cmd "${CMAKE_C_COMPILER:-gcc}" 2>/dev/null || need_cmd gcc
need_cmd "${CMAKE_CXX_COMPILER:-g++}" 2>/dev/null || need_cmd g++

GENERATOR_ARGS=()
if command -v ninja >/dev/null 2>&1; then
  GENERATOR_ARGS=(-G Ninja)
fi

have_wx=0
if command -v wx-config >/dev/null 2>&1; then
  have_wx=1
elif pkg-config --exists wxwidgets >/dev/null 2>&1 || pkg-config --exists wx >/dev/null 2>&1; then
  have_wx=1
fi

case "$LAUNCHER_MODE" in
  on)  TD3E_LAUNCHER=ON ;;
  off) TD3E_LAUNCHER=OFF ;;
  auto)
    if [[ $have_wx -eq 1 ]]; then
      TD3E_LAUNCHER=ON
    else
      TD3E_LAUNCHER=OFF
      echo "Note: wxWidgets 3.2 not detected; building without launcher (game only)." >&2
      echo "      Install wxWidgets dev packages or run with --launcher to force." >&2
    fi
    ;;
esac

if [[ $CLEAN -eq 1 ]]; then
  echo "Cleaning $BUILD_DIR"
  rm -rf "$BUILD_DIR"
fi

mkdir -p "$BUILD_DIR"

CMAKE_ARGS=(
  -S "$ROOT_DIR"
  -B "$BUILD_DIR"
  "${GENERATOR_ARGS[@]}"
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
  -DTD3E_LAUNCHER="$TD3E_LAUNCHER"
)

# Use compilers from environment if provided, otherwise let CMake/defaults decide.
[[ -n "${CMAKE_C_COMPILER:-}" ]] && CMAKE_ARGS+=(-DCMAKE_C_COMPILER="$CMAKE_C_COMPILER")
[[ -n "${CMAKE_CXX_COMPILER:-}" ]] && CMAKE_ARGS+=(-DCMAKE_CXX_COMPILER="$CMAKE_CXX_COMPILER")

echo "Configuring ($BUILD_TYPE, launcher=$TD3E_LAUNCHER)..."
cmake "${CMAKE_ARGS[@]}"

echo "Building with $JOBS job(s)..."
cmake --build "$BUILD_DIR" -j "$JOBS"

echo
echo "Build complete."
if [[ -f "$BUILD_DIR/testdrive3-enhanced" || -f "$BUILD_DIR/testdrive3-enhanced.exe" ]]; then
  echo "Game binary: $BUILD_DIR/testdrive3-enhanced"
fi
if [[ "$TD3E_LAUNCHER" == "ON" ]]; then
  echo "Launcher:    $BUILD_DIR/Test Drive III Enhanced (if built)"
fi
# Default game data location (relative to repo root). Override with GAME_DIR if needed.
GAME_DIR="${GAME_DIR:-../../../Game}"
# Resolve to an absolute path for launching (avoids issues when programs change working directory).
if command -v realpath >/dev/null 2>&1; then
  GAME_DIR_LAUNCH="$(realpath -m "$ROOT_DIR/$GAME_DIR")"
else
  GAME_DIR_LAUNCH="$GAME_DIR"
fi
echo "Run example: $BUILD_DIR/testdrive3-enhanced --game-dir \"$GAME_DIR\""

if [[ $RUN_AFTER -eq 1 ]]; then
  LAUNCHER_BIN="$BUILD_DIR/Test Drive III Enhanced"
  GAME_BIN="$BUILD_DIR/testdrive3-enhanced"
  [[ -x "$GAME_BIN" ]] || GAME_BIN="$BUILD_DIR/testdrive3-enhanced.exe"

  if [[ -x "$LAUNCHER_BIN" ]]; then
    echo "Running launcher: $LAUNCHER_BIN --game-dir \"$GAME_DIR_LAUNCH\""
    exec "$LAUNCHER_BIN" --game-dir "$GAME_DIR_LAUNCH"
  elif [[ -x "$GAME_BIN" ]]; then
    echo "Launcher not found; running game: $GAME_BIN --game-dir \"$GAME_DIR_LAUNCH\""
    exec "$GAME_BIN" --game-dir "$GAME_DIR_LAUNCH"
  else
    echo "Cannot --run: no launcher or game binary found in $BUILD_DIR." >&2
    exit 1
  fi
fi
