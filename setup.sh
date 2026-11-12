#!/usr/bin/env bash
# Set up and build GT5 Recomp on Linux with one command. Run it from the gt5-recomp folder:
#   bash setup.sh
# Run it as often as you like; it does whatever is still to do. It checks the tools (and with
# --install-deps installs the missing ones through your package manager, asking for your password),
# fetches and patches ps3recomp, makes the Python environment, then checks your game files and says
# exactly what's missing or wrong. When everything is in place it generates the game code from your
# EBOOT.elf and builds GT5/build-linux/gt5 (the first time takes 20 to 60 minutes). Then play with
# bash GT5/play.sh. --no-build stops after the checks.
#
# Updating later: git pull, then bash setup.sh again (it re-patches, re-lifts and rebuilds only what
# the new version changed).
set -u
BASE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GT5="$BASE/GT5"
RC="$BASE/ps3recomp"
VENV="$BASE/venv"
PY="$VENV/bin/python"
STATE="$BASE/.gt5"
TODO=0
NOBUILD=0
INSTALL=0
for a in "$@"; do
    case "$a" in
        --no-build) NOBUILD=1 ;;
        --install-deps) INSTALL=1 ;;
        -h|--help) sed -n '2,12p' "$0"; exit 0 ;;
        *) echo "setup.sh: unknown option $a (try --help)"; exit 1 ;;
    esac
done

if [ -t 1 ]; then G=$'\e[32m'; Y=$'\e[33m'; R=$'\e[31m'; N=$'\e[0m'; else G=; Y=; R=; N=; fi
ok()   { echo "  ${G}[ok]${N}      $1"; }
todo() { echo "  ${Y}[missing]${N} $1"; [ -n "${2:-}" ] && echo "            $2"; TODO=$((TODO + 1)); }
stop() { echo; echo "${R}setup stopped: $1${N}"; exit 1; }
have() { command -v "$1" > /dev/null 2>&1; }

[ -f "$GT5/scripts/relift.sh" ] && [ -f "$GT5/linux/CMakeLists.txt" ] ||
    stop "run this from the gt5-recomp folder (the one with setup.sh, README.md and the GT5 folder)."
[ "$(uname -s)" = Linux ] || stop "this is the Linux setup. On Windows use setup.ps1 (or GT5-Installer.bat)."
case "$(uname -m)" in x86_64|amd64) ;; *) stop "only x86-64 PCs are supported so far (this is $(uname -m))." ;; esac
grep -qw avx2 /proc/cpuinfo 2> /dev/null ||
    stop "this CPU has no AVX2. The game code is built for AVX2 CPUs (Intel 2013 Haswell / AMD 2015 and later)."
echo "== GT5 Recomp setup (Linux) in $BASE"
echo
echo "Tools:"

# ---- The packages the build needs, by package manager ----------------------------------------------------
PM=""; PKGS=""
if have apt-get; then
    PM="sudo apt-get install -y"
    PKGS="git python3 python3-venv python3-pip clang lld cmake ninja-build pkg-config libsdl2-dev libvulkan-dev glslang-dev glslang-tools libavcodec-dev libvulkan1 mesa-vulkan-drivers"
elif have dnf; then
    PM="sudo dnf install -y"
    PKGS="git python3 clang lld cmake ninja-build pkgconf-pkg-config SDL2-devel vulkan-headers vulkan-loader glslang-devel glslang ffmpeg-free-devel mesa-vulkan-drivers"
elif have pacman; then
    PM="sudo pacman -S --needed --noconfirm"
    PKGS="git python clang lld cmake ninja pkgconf sdl2 vulkan-headers vulkan-icd-loader glslang ffmpeg"
elif have zypper; then
    PM="sudo zypper install -y"
    PKGS="git python3 clang lld cmake ninja pkg-config libSDL2-devel vulkan-headers libvulkan1 glslang-devel ffmpeg-7-libavcodec-devel"
fi
if [ "$INSTALL" = 1 ]; then
    [ -n "$PM" ] || stop "no apt, dnf, pacman or zypper here: install the tools listed below by hand."
    echo "  installing: $PKGS"
    $PM $PKGS || stop "installing the packages failed (see above)."
fi

# Each check: its name, then how to see it is there.
missing_tools=0
check_tool() { if eval "$2" > /dev/null 2>&1; then ok "$1"; else todo "$1"; missing_tools=1; fi; }
check_tool "Git" "have git"
check_tool "Python 3.11 or newer, with venv" "python3 -c 'import sys, venv; sys.exit(sys.version_info < (3, 11))'"
check_tool "Clang (C/C++ compiler)" "have clang && have clang++"
check_tool "CMake 3.20 or newer" "printf '3.20\n%s\n' \"\$(cmake --version | awk 'NR==1 {print \$3}')\" | sort -V -C"
check_tool "Ninja" "have ninja || have ninja-build"
check_tool "SDL2 (development files)" "have sdl2-config || pkg-config --exists sdl2"
check_tool "Vulkan headers" "test -f /usr/include/vulkan/vulkan.h"
check_tool "glslang (development files)" "ls /usr/include/glslang/Public/ShaderLang.h"
check_tool "FFmpeg 6.1 or newer (libavcodec development files)" \
    "grep -qs AV_FRAME_FLAG_KEY /usr/include/libavutil/frame.h /usr/include/*/libavutil/frame.h /usr/include/ffmpeg/libavutil/frame.h"
check_tool "Vulkan loader (libvulkan.so.1)" "ldconfig -p | grep -q 'libvulkan.so.1 '"
if [ "$missing_tools" = 1 ]; then
    echo
    if [ -n "$PM" ]; then
        echo "Install them with:"
        echo "  $PM $PKGS"
        echo "or run: bash setup.sh --install-deps"
    else
        echo "Install them with your distribution's package manager (development packages of SDL2, Vulkan,"
        echo "glslang and FFmpeg's libavcodec, plus git, python3, clang, cmake and ninja)."
    fi
    stop "tools missing (see above)."
fi
if have vulkaninfo; then
    vulkaninfo --summary 2> /dev/null | grep -q "deviceName" && ok "a Vulkan graphics driver" ||
        echo "            ${Y}vulkaninfo found no Vulkan device: install your graphics card's Vulkan driver (Mesa for AMD/Intel, the NVIDIA driver).${N}"
fi

# ---- ps3recomp at this version's base commit, plus this version's patch (what update.ps1 does) ----------
mkdir -p "$STATE"
PATCH="$GT5/patches/ps3recomp.patch"
NEWBASE="$(tr -d '\r\n ' < "$GT5/patches/ps3recomp-base.txt")"
if [ ! -d "$RC/.git" ]; then
    # No line-ending conversion: the patch matches the files as stored upstream.
    git clone --quiet --config core.autocrlf=false https://github.com/sp00nznet/ps3recomp.git "$RC" ||
        stop "couldn't clone ps3recomp (is the internet reachable?)"
fi
OLDPATCH="$STATE/applied.patch"
OLDBASE="$(cat "$STATE/applied-base.txt" 2> /dev/null | tr -d '\r\n ')"
if [ -f "$OLDPATCH" ] && cmp -s "$OLDPATCH" "$PATCH" && [ "$OLDBASE" = "$NEWBASE" ] &&
   [ "$(git -C "$RC" rev-parse HEAD)" = "$NEWBASE" ]; then
    ok "ps3recomp, patched (already in place)"
else
    # 1. Take the old patch out, keeping anything else changed in ps3recomp (saved to backups/).
    if [ -n "$(git -C "$RC" status --porcelain)" ] && [ -f "$OLDPATCH" ] &&
       git -C "$RC" apply --whitespace=nowarn --reverse --check "$OLDPATCH" 2> /dev/null; then
        git -C "$RC" apply --whitespace=nowarn --reverse "$OLDPATCH"
    fi
    if [ -n "$(git -C "$RC" status --porcelain)" ]; then
        mkdir -p "$BASE/backups"
        own="$BASE/backups/ps3recomp-changes-$(date +%Y-%m-%d_%H%M).patch"
        git -C "$RC" add -A && git -C "$RC" diff --cached --binary > "$own" && git -C "$RC" reset --quiet
        echo "            ${Y}ps3recomp had changes besides the patch (your own edits?). Saved in backups/$(basename "$own").${N}"
    fi
    # 2. The ps3recomp commit this version's patch was made against.
    git -C "$RC" cat-file -e "$NEWBASE^{commit}" 2> /dev/null || git -C "$RC" fetch --quiet origin "$NEWBASE"
    git -C "$RC" checkout --quiet --force "$NEWBASE" || stop "couldn't check out ps3recomp $NEWBASE"
    git -C "$RC" clean -fdq
    # 3. This version's patch.
    git -C "$RC" apply --whitespace=nowarn --check "$PATCH" ||
        stop "this version's patch doesn't apply to ps3recomp $NEWBASE. Please report it."
    git -C "$RC" apply --whitespace=nowarn "$PATCH" || stop "applying the patch failed"
    cp -f "$PATCH" "$OLDPATCH"
    echo "$NEWBASE" > "$STATE/applied-base.txt"
    ok "ps3recomp, patched"
fi
if [ -d "$BASE/.git" ]; then
    { git -C "$BASE" describe --tags --exact-match HEAD 2> /dev/null ||
      echo "development build $(git -C "$BASE" describe --tags 2> /dev/null || git -C "$BASE" rev-parse --short HEAD)"; } > "$STATE/version.txt"
fi

# ---- Python environment and the tools' packages --------------------------------------------------------
if [ ! -x "$PY" ]; then
    python3 -m venv "$VENV" || stop "making the Python environment failed (Debian/Ubuntu: sudo apt install python3-venv)."
fi
"$PY" -m pip install --quiet --disable-pip-version-check -r "$RC/requirements.txt" ||
    stop "installing the Python packages failed (see above)."
ok "Python environment and packages"

# ---- Your game files -----------------------------------------------------------------------------------
echo
echo "Your files (see Linux in README.md for where each comes from):"
mkdir -p "$GT5/input" "$GT5/vfs"
ELF="$GT5/input/EBOOT.elf"
if [ ! -f "$ELF" ]; then
    if [ -f "$GT5/input/EBOOT.BIN" ]; then
        todo "GT5/input/EBOOT.elf" "You have EBOOT.BIN there, but the tools need the DECRYPTED file: in RPCS3, Utilities > Decrypt PS3 Binaries, pick EBOOT.BIN, and put the result in GT5/input named EBOOT.elf."
    else
        todo "GT5/input/EBOOT.elf (your decrypted game executable)" "In RPCS3, Utilities > Decrypt PS3 Binaries, pick your disc's PS3_GAME/USRDIR/EBOOT.BIN, and put the result in GT5/input named EBOOT.elf."
    fi
else
    magic="$(head -c 4 "$ELF" | od -An -tx1 | tr -d ' \n')"
    sha1="$(sha1sum "$ELF" | cut -d' ' -f1 | tr a-f A-F)"
    if [ "$magic" = "53434500" ]; then
        todo "a decrypted GT5/input/EBOOT.elf" "This file is still encrypted (it's EBOOT.BIN renamed). Decrypt it in RPCS3: Utilities > Decrypt PS3 Binaries."
    elif [ "$magic" != "7f454c46" ]; then
        todo "a valid GT5/input/EBOOT.elf" "This file isn't a PS3 executable. Decrypt your disc's PS3_GAME/USRDIR/EBOOT.BIN in RPCS3 and use the result."
    else
        case "$sha1" in
            1535B865EC7DD58E7704B378B8621532ECF844E1) ok "GT5/input/EBOOT.elf: Europe BCES00569, version 01.00 (the tested one)" ;;
            40126D2D14B0A95B9601BB11E3A6E778186067AC) ok "GT5/input/EBOOT.elf: North America BCUS98114 (players have raced on it)" ;;
            49A3662D4B541BB4E0C2E72D2602F89FEB710E71) ok "GT5/input/EBOOT.elf: Hong Kong / Asia BCAS20108, version 01.00 (tested)" ;;
            96DEA35B194AA80E993CAD9EBE42E96072F3A3D5) ok "GT5/input/EBOOT.elf: Hong Kong / Asia BCAS20108, update 2.01 (tested; needs the update's data in GT5/hdd0, README: Game updates)" ;;
            *) ok "GT5/input/EBOOT.elf (SHA-1 $sha1)"
               echo "            ${Y}Not one tried yet. Another disc version may well work: please report how it goes.${N}" ;;
        esac
    fi
fi

GAME="$GT5/vfs/PS3_GAME"
if [ -d "$GAME/USRDIR" ]; then
    if [ -f "$GAME/PARAM.SFO" ]; then
        # A dump made to a FAT32 drive splits files over 4 GB into <file>.66600, .66601, ...: join them.
        while IFS= read -r first; do
            whole="${first%.66600}"
            parts=(); i=0
            while [ -f "$(printf '%s.666%02d' "$whole" "$i")" ]; do parts+=("$(printf '%s.666%02d' "$whole" "$i")"); i=$((i + 1)); done
            echo "            joining ${whole#$GAME/} from ${#parts[@]} pieces (a few minutes)..."
            if cat "${parts[@]}" > "$whole.joining" && mv -f "$whole.joining" "$whole"; then
                rm -f "${parts[@]}"
            else
                rm -f "$whole.joining"; echo "            ${Y}couldn't join them (disk full?). Copy the whole file from your dump instead.${N}"
            fi
        done < <(find "$GAME" -type f -name '*.66600' 2> /dev/null)
        if [ -f "$GAME/USRDIR/GT.VOL" ]; then ok "GT5/vfs/PS3_GAME (your disc's game data)"
        else todo "GT5/vfs/PS3_GAME/USRDIR/GT.VOL (the game's data, about 12 GB)" "Copy it from PS3_GAME/USRDIR in your disc dump (or its pieces GT.VOL.66600, .66601, ...: they're joined automatically)."
        fi
    else
        todo "GT5/vfs/PS3_GAME/PARAM.SFO" "Copy the whole PS3_GAME folder from your disc, including PARAM.SFO."
    fi
elif [ -d "$GAME/PS3_GAME/USRDIR" ]; then
    todo "GT5/vfs/PS3_GAME at the right level" "It's one folder too deep: move the contents of GT5/vfs/PS3_GAME/PS3_GAME up into GT5/vfs/PS3_GAME."
else
    todo "GT5/vfs/PS3_GAME (your disc's game data, about 17 GB)" "Copy the PS3_GAME folder from your disc dump to GT5/vfs, so that GT5/vfs/PS3_GAME/USRDIR exists."
fi

FLASH="$GT5/dev_flash"
if [ -d "$FLASH/sys" ]; then ok "GT5/dev_flash (PS3 system software)"
elif [ -d "$FLASH/dev_flash/sys" ]; then todo "GT5/dev_flash at the right level" "It's one folder too deep: move the contents of GT5/dev_flash/dev_flash up into GT5/dev_flash."
else todo "GT5/dev_flash (PS3 system software)" "In RPCS3, File > Install Firmware with Sony's PS3UPDAT.PUP, then copy RPCS3's dev_flash folder (~/.config/rpcs3/dev_flash) to GT5/dev_flash."
fi

echo
if [ "$TODO" -gt 0 ]; then
    echo "${Y}$TODO thing(s) still to do (marked [missing] above). Sort them out, then run setup.sh again.${N}"
    exit 1
fi
if [ "$NOBUILD" = 1 ]; then echo "${G}Everything is in place. Run setup.sh without --no-build to build the game.${N}"; exit 0; fi

# ---- Generate the game code (only when it's missing or out of date), then build ---------------------------
cd "$GT5" || exit 1
if ! "$PY" scripts/lift_stamp.py --check > /dev/null 2>&1 || [ ! -f generated/ppu_recomp.h ]; then
    echo "Generating the game code from your EBOOT.elf (about 10 to 20 minutes)..."
    bash scripts/relift.sh ||
        stop "generating the game code failed (see above). Please report it with that output: https://github.com/MormonFTP-hub/gt5-recomp/issues/new?template=2-problem.yml"
else
    echo "The game code is already generated and current."
fi

# Parallel jobs: one per core, but no more than one per 2 GB of memory (the biggest files need that).
mem_gb=$(( $(awk '/MemTotal/ {print $2}' /proc/meminfo) / 1048576 ))
JOBS=$(nproc); [ $((mem_gb / 2)) -lt "$JOBS" ] && JOBS=$((mem_gb / 2)); [ "$JOBS" -lt 1 ] && JOBS=1
[ -n "${GT5_JOBS:-}" ] && JOBS="$GT5_JOBS"
BUILD="$GT5/build-linux"
NINJA=ninja; have ninja || NINJA=ninja-build
if [ ! -f "$BUILD/build.ninja" ]; then
    cmake -S "$GT5/linux" -B "$BUILD" -G Ninja -DCMAKE_MAKE_PROGRAM="$(command -v $NINJA)" \
          -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ ||
        stop "configuring the build failed (see above)."
fi
echo "Building GT5/build-linux/gt5 with $JOBS parallel jobs (the first build takes 20 to 60 minutes)..."
built=1
for try in 1 2 3 4; do
    if cmake --build "$BUILD" --target gt5 -j "$JOBS" 2>&1 | tee "$BUILD/last_build.log"; [ "${PIPESTATUS[0]}" -eq 0 ]; then
        built=0; break
    fi
    # A crashed compile (heavy parallel builds now and then) is simply redone, with fewer jobs.
    JOBS=$(( JOBS / 2 )); [ "$JOBS" -lt 1 ] && JOBS=1
    echo "${Y}The build stopped. Trying again with $JOBS parallel jobs; it carries on where it stopped...${N}"
done
[ "$built" = 0 ] || stop "the build failed (see above). Please report it with that output: https://github.com/MormonFTP-hub/gt5-recomp/issues/new?template=2-problem.yml"
echo
echo "${G}Done! Play with: bash GT5/play.sh${N}"
