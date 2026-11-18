#!/usr/bin/env bash
# Profile-guided build of the Linux game (clang): compile once with counters, play, compile again using
# the counts, so hot code is laid out together and branches go the way they usually go. Covers the
# lifted PPU and SPU code and the runtime alike.
#
#   bash scripts/pgo_linux.sh gen     # build-pgo-gen/gt5, instrumented (an hour or more on a Steam Deck)
#   bash scripts/pgo_linux.sh record  # swaps it into build-linux/gt5 and sets LLVM_PROFILE_FILE in settings.sh:
#                                     # play menus and a race normally, quit with Esc (Game Mode: quit from
#                                     # the game, not Steam), then:
#   bash scripts/pgo_linux.sh use     # merges the counts, puts the normal build back, builds build-pgo-use/gt5
#   bash scripts/pgo_linux.sh install # copies build-pgo-use/gt5 to build-linux/gt5 (normal build kept as gt5.release)
#
# The profile is made by running the game, so it is derived data like the lift: it lives in
# analysis/pgo/ and is never committed. After a re-lift, record again (out-of-date counts are only
# partly used). Steam Deck, US 2.17, race paused on the starting grid: 107.4 ms a frame with the normal
# build, 92.5 ms with this one (two normal runs around one PGO run).
set -euo pipefail
GT5="$(cd "$(dirname "$0")/.." && pwd)"
PROF="$GT5/analysis/pgo"
JOBS="${JOBS:-5}"
cfg() {   # cfg <dir> <flags>
    cmake -S "$GT5/linux" -B "$GT5/$1" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
        -DPS3RECOMP_RSX_VULKAN=ON -DPS3RECOMP_FFMPEG_RUNTIME=ON \
        "-DCMAKE_C_FLAGS=$2" "-DCMAKE_CXX_FLAGS=$2" "${@:3}"
}
put() {   # put <binary>: becomes build-linux/gt5, also while the game runs (a copy over a running
          # executable fails with "Text file busy"; a rename does not)
    cp -p "$1" "$GT5/build-linux/gt5.new" && mv -f "$GT5/build-linux/gt5.new" "$GT5/build-linux/gt5"
}
case "${1:-}" in
gen)
    cfg build-pgo-gen "-fprofile-generate" -DCMAKE_EXE_LINKER_FLAGS=-fprofile-generate
    ninja -C "$GT5/build-pgo-gen" -j "$JOBS" gt5 ;;
record)
    mkdir -p "$PROF"; rm -f "$PROF"/*.profraw
    cp -p "$GT5/build-linux/gt5" "$GT5/build-linux/gt5.release"
    put "$GT5/build-pgo-gen/gt5"
    sed -i "/pgo_linux.sh record/d; /LLVM_PROFILE_FILE/d" "$GT5/settings.sh" 2>/dev/null || true
    printf '# pgo_linux.sh record (temporary)\nexport LLVM_PROFILE_FILE=%s/gt5-%%p.profraw\n' "$PROF" >> "$GT5/settings.sh"
    echo "Play menus and a race, quit the game itself, then: bash scripts/pgo_linux.sh use" ;;
use)
    llvm-profdata merge -o "$PROF/gt5.profdata" "$PROF"/gt5-*.profraw
    sed -i "/pgo_linux.sh record/d; /LLVM_PROFILE_FILE/d" "$GT5/settings.sh"
    if [ -f "$GT5/build-linux/gt5.release" ]; then put "$GT5/build-linux/gt5.release"; fi
    cfg build-pgo-use "-fprofile-use=$PROF/gt5.profdata -Wno-profile-instr-unprofiled -Wno-profile-instr-out-of-date"
    # ninja does not track the profile: with the same flags and sources a new profile would be
    # ignored ("no work to do"), so every object is rebuilt.
    ninja -C "$GT5/build-pgo-use" -t clean >/dev/null
    ninja -C "$GT5/build-pgo-use" -j "$JOBS" gt5 ;;
install)
    [ -f "$GT5/build-linux/gt5.release" ] || cp -p "$GT5/build-linux/gt5" "$GT5/build-linux/gt5.release"
    put "$GT5/build-pgo-use/gt5"
    echo "build-linux/gt5 is now the profile-guided build (normal build: build-linux/gt5.release)" ;;
*) sed -n '2,19p' "$0"; exit 1 ;;
esac
