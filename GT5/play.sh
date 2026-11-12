#!/usr/bin/env bash
# Gran Turismo 5 recomp on Linux: the game with your files in this folder (what play.bat does on
# Windows). Build it first with setup.sh in the folder above.
#   bash GT5/play.sh
# Keyboard while no controller is connected:
#   Z = Cross (OK)   X = Circle   A = Square   S = Triangle   Q/W = L1/R1   1/2 = L2/R2
#   arrows = D-pad   Enter = Start   Tab = Select   Esc = quit
# Controllers (DualSense, DualShock 4, Xbox, Steam Deck through Steam Input) go through SDL2.
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GAME="$HERE/build-linux/gt5"
if [ ! -x "$GAME" ]; then
    echo "No $GAME yet: run setup.sh in $(dirname "$HERE") first." >&2
    exit 1
fi

export PS3_VFS_ROOT="$HERE/vfs"
export PS3_HDD0_ROOT="$HERE/hdd0"
export PS3_HDD1_ROOT="$HERE/hdd1"
export PS3_DEV_FLASH="$HERE/dev_flash"
export PS3_TITLE="Gran Turismo 5"
# The port's version in the window title (setup.sh notes it in .gt5/version.txt).
if [ -f "$HERE/../.gt5/version.txt" ]; then
    v="$(head -n1 "$HERE/../.gt5/version.txt" | tr -d '\r')"
    v="${v#development build }"
    case "$v" in v*) export PS3_TITLE="Gran Turismo 5 (recomp $v)";; esac
fi
export PS3_VERBOSE=0
# An SPU job manager polls for work while the game loads; it is not stuck.
export SPU_DMA_REPEAT_SECS=100000
# Seven job managers want an SPU each. A PC can spare them; the PS3 had to take turns.
export SPURS_EXTRA_SPUS=2
# GT5's SPU jobs fill holes in the GPU command stream, slowly during loads. Wait for them.
export GCM_PARK_GRACE_MS=600000
# A few timing lines every 5 s in logs/play.err, to see how a real session runs.
export GCM_DRAIN_STAT=1
# What the game received from the controller, also in logs/play.err.
export PAD_TRACE=1
# The controller keeps working if another window takes focus.
export PAD_BACKGROUND=1
# The Vulkan renderer: the game's own shaders (compiled once, then cached in GT5/shadercache), draws
# batched into one submission, no copy of each frame back to the CPU.
export PS3RECOMP_VK_WINDOW=1
export PS3RECOMP_VK_GUEST_PROGRAMS=1
export PS3RECOMP_SPIRV_CACHE="$HERE/shadercache"
export VK_DRAW_BATCH=1
export PS3RECOMP_VK_NO_READBACK=1
# Full screen: remove the # from the next line.
# export PS3RECOMP_VK_FULLSCREEN=1
# Resolution: the TV mode the game is told it's on, as on a PS3. 720 = its 720p mode (races drawn at
# 1152x648). 1080 = its 1080p mode (races at 1728x972, sharper menus), for a strong graphics card.
export PS3_VIDEO_MODE=720
# Skip the opening movie: a quicker start. GT TV still plays it. Comment it out to watch it.
export PS3_SKIP_INTRO=1
# Watch mode, for B-Spec races and replays: remove the # and it comes on by itself after 10 s
# without input (draws fewer frames), and goes off the moment you press something.
# export LD_WATCH=1
# Your own settings, one "export NAME=value" per line, win over the lines above.
[ -f "$HERE/settings.sh" ] && . "$HERE/settings.sh"

mkdir -p "$HERE/hdd0" "$HERE/hdd1" "$HERE/logs" "$HERE/shadercache"
cd "$HERE" || exit 1
# Keep the last run's log as play.prev.err, so a problem report can still include it after a restart.
[ -f "$HERE/logs/play.err" ] && cp -f "$HERE/logs/play.err" "$HERE/logs/play.prev.err"
echo "Gran Turismo 5 is starting (logs in $HERE/logs)."
exec "$GAME" "$HERE/input/EBOOT.elf" > "$HERE/logs/play.out" 2> "$HERE/logs/play.err"
