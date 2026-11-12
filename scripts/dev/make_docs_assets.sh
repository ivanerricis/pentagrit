#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Regenerates the README screenshots and audio examples by running the hidden Catch2 cases tagged [.docs].
#
# Usage: scripts/dev/make_docs_assets.sh [output-dir]
#   output-dir   default: docs (images go to <output-dir>/images, audio to <output-dir>/audio)
# Environment:
#   PRESET             CMake preset (default: win-vs on Windows, mac-universal on macOS, linux elsewhere)
#   CONFIG             build configuration for multi-config generators (default: Release)
#   PENTAGRIT_DOCS_SRC optional folder with extra *.wav loops to render next to the synthetic drum loop
#   SKIP_BUILD=1       reuse an existing PentagritTests binary
# Works from Git Bash on Windows.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$root"

case "$(uname -s)" in
  MINGW* | MSYS* | CYGWIN*) default_preset="win-vs" ;;
  Darwin) default_preset="mac-universal" ;;
  *) default_preset="linux" ;;
esac
preset="${PRESET:-$default_preset}"
config="${CONFIG:-Release}"

out="${1:-docs}"
mkdir -p "$out"
out="$(cd "$out" && pwd)"

if [[ "${SKIP_BUILD:-0}" != "1" ]]; then
  [[ -d "build/$preset" ]] || cmake --preset "$preset"
  cmake --build "build/$preset" --config "$config" --target PentagritTests --parallel
fi

exe="$(find "build/$preset" -type f \( -name PentagritTests -o -name PentagritTests.exe \) -print | head -n 1)"
if [[ -z "$exe" ]]; then
  echo "PentagritTests binary not found under build/$preset" >&2
  exit 1
fi

export PENTAGRIT_DOCS_DIR="$out"
[[ -n "${PENTAGRIT_DOCS_SRC:-}" ]] && export PENTAGRIT_DOCS_SRC
"$exe" "[.docs]"

echo "Generated files in $out:"
find "$out/images" "$out/audio" -type f -exec ls -l {} + 2>/dev/null | awk '{print $5 "\t" $9}'
