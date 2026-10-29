#!/usr/bin/env bash
# Collect the built plugin formats into Pentagrit-v<version>-<platform>.zip.
# usage: package.sh <platform: windows-x64|macos-universal|linux-x64> <build_dir> <out_dir> [version]
# version defaults to the one in CMakeLists.txt; a leading "v" is accepted.
set -euo pipefail

platform="${1:?usage: package.sh <platform> <build_dir> <out_dir> [version]}"
build_dir="${2:?missing build_dir}"
out_dir="${3:?missing out_dir}"
version="${4:-}"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

if [[ -z "$version" ]]; then
    version="$(sed -nE 's/^[[:space:]]*project\([[:space:]]*Pentagrit[[:space:]]+VERSION[[:space:]]+([0-9]+(\.[0-9]+)*).*/\1/p' \
        "$root/CMakeLists.txt" | head -n1)"
fi
version="${version#v}"
name="Pentagrit-v${version}-${platform}"

mkdir -p "$out_dir"
out_dir="$(cd "$out_dir" && pwd)"
stage="$out_dir/stage/$name"
rm -rf "$out_dir/stage"
mkdir -p "$stage"

# Locate artefacts without assuming the exact JUCE layout (<target>_artefacts[/<config>]/<format>/...).
find_one() { # $1 = name, $2 = type (f|d|any)
    if [[ "$2" == any ]]; then
        find "$build_dir" -path '*_artefacts*' -name "$1" -print -quit
    else
        find "$build_dir" -path '*_artefacts*' -type "$2" -name "$1" -print -quit
    fi
}
copy_item() { # $1 = name, $2 = type, $3 = required (1|0)
    local src
    src="$(find_one "$1" "$2")"
    if [[ -z "$src" ]]; then
        if [[ "$3" == 1 ]]; then echo "error: $1 not found under $build_dir" >&2; exit 1; fi
        return 0
    fi
    cp -R "$src" "$stage/"
    echo "packaged $src"
}

copy_item Pentagrit.vst3 d 1
copy_item Pentagrit.clap any 1  # a file on Windows/Linux, a bundle on macOS
case "$platform" in
windows-*)
    copy_item Pentagrit.exe f 1
    ;;
macos-*)
    copy_item Pentagrit.component d 1
    copy_item Pentagrit.app d 1
    ;;
linux-*)
    copy_item Pentagrit f 1
    ;;
esac
[[ -e "$stage/Pentagrit.vst3" ]] || { echo "error: VST3 missing from package" >&2; exit 1; }

# Ad-hoc signing on macOS (Apple signing/notarisation is a later phase).
if [[ "$platform" == macos-* ]]; then
    for b in "$stage"/*.vst3 "$stage"/*.component "$stage"/*.clap "$stage"/*.app; do
        [[ -e "$b" ]] && codesign --force --deep --sign - "$b"
    done
fi

cp "$root/LICENSE" "$stage/LICENSE"

case "$platform" in
windows-*)
    install_text='Copy Pentagrit.vst3 to C:\Program Files\Common Files\VST3\
Copy Pentagrit.clap to C:\Program Files\Common Files\CLAP\
Run Pentagrit.exe for the standalone application.'
    ;;
macos-*)
    install_text='Copy Pentagrit.vst3    to ~/Library/Audio/Plug-Ins/VST3/
Copy Pentagrit.component to ~/Library/Audio/Plug-Ins/Components/
Copy Pentagrit.clap    to ~/Library/Audio/Plug-Ins/CLAP/
Copy Pentagrit.app     to /Applications/

The binaries are ad-hoc signed, not notarised. If macOS refuses to load them, run:
  xattr -dr com.apple.quarantine <path to the copied item>'
    ;;
*)
    install_text='Copy Pentagrit.vst3 to ~/.vst3/
Copy Pentagrit.clap to ~/.clap/
Run ./Pentagrit for the standalone application.'
    ;;
esac
cat > "$stage/README.txt" <<TXT
Pentagrit ${version} (${platform})
Multiband transient shaper and saturator. License: AGPL-3.0-or-later (see LICENSE).

Installation
------------
${install_text}

Rescan plugins in your host after copying.
TXT

(cd "$out_dir/stage" && cmake -E tar cf "$out_dir/$name.zip" --format=zip -- "$name")
rm -rf "$out_dir/stage"
echo "created $out_dir/$name.zip"
