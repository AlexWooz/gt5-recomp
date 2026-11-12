@echo off
REM Gran Turismo 5 recomp. Keyboard if no XInput pad is connected:
REM   Z = Cross (OK)   X = Circle   A = Square   S = Triangle
REM   arrows = D-pad   Enter = Start   Tab = Select
set "HERE=%~dp0"
set "PS3_VFS_ROOT=%HERE%vfs"
set "PS3_HDD0_ROOT=%HERE%hdd0"
set "PS3_HDD1_ROOT=%HERE%hdd1"
set "RSX_LIVE_DRAW=1"
set "PS3_TITLE=Gran Turismo 5"
set "PS3_VERBOSE=0"
set "PS3_DEV_FLASH=%HERE%dev_flash"
REM An SPU job manager polls for work while the game loads; it is not stuck.
set "SPU_DMA_REPEAT_SECS=100000"
REM Seven job managers want an SPU each. A PC can spare them; the PS3 had to take turns.
set "SPURS_EXTRA_SPUS=2"
REM GT5's SPU jobs fill holes in the GPU command stream, slowly during loads. Wait for them.
set "GCM_PARK_GRACE_MS=600000"
REM A few timing lines every 5 s in logs\play.err, to see how a real session runs.
set "GCM_DRAIN_STAT=1"
set "LD_GPU_WORK=1"
REM What the game received from the controller, also in logs\play.err.
set "PAD_TRACE=1"
REM The controller keeps working if another window takes focus (an untitled one did mid-race).
set "PAD_BACKGROUND=1"
REM Watch mode, for B-Spec races, replays and anything you only watch. OFF unless you turn it on.
REM It draws every other frame (the race itself keeps full speed), draws nothing while the window
REM is minimized, and lowers the game's priority so your other programs come first.
REM   F9 in the game window: watch mode on / off, any time.
REM   Or remove the REM from the next line and it comes on by itself after 10 s without touching
REM   the controller or the game keys, and goes off again the moment you do.
REM set "LD_WATCH=1"
REM The mouse is no longer held inside the window. While you use the controller with the game in
REM front, mouse movement and clicks that Steam makes from the controller are ignored; your real
REM mouse always works. (PAD_CURSOR_CLIP=1 brings back the old clipping.)
REM Steering wheel: plug it in before starting; the game sees it as a Logitech G27, with force
REM feedback. New and not yet tried on a real wheel. logs\play.err says what was found ([wheel]).
REM Remove a REM to use a line (more in the README's FAQ):
REM set "PS3_WHEEL_FFB_INVERT=1"   forces push the wrong way
REM set "PS3_WHEEL_FFB=60"         force strength in percent
REM set "PS3_WHEEL_RANGE=900"      the rotation your wheel is set to, in degrees
REM set "PS3_WHEEL_TRACE=1"        log every pedal, button and force, to fix a mapping
REM set "PS3_WHEEL=off"            don't look for a wheel
REM Screen shape. auto = your monitor's: on an ultrawide (21:9, 32:9) the game draws a wider view,
REM more of the track at the sides, with the HUD and menus kept 16:9 in the middle. The window opens
REM at that shape; maximize it. On a 16:9 monitor nothing changes. Or a fixed shape: 21:9, 32:9,
REM 3440x1440. 16:9 = the PS3's own picture.
set "PS3_ASPECT=auto"
REM set "PS3_WINDOW_SIZE=2560x1080"   open the window at exactly this size
REM Resolution: the TV mode the game is told it's on, as on a PS3. 720 = its 720p mode (races drawn at
REM 1152x648). 1080 = its 1080p mode (races at 1728x972), but in v0.1.10 the window showed only part of
REM that picture, so 720 is back as the default until the fix is out.
set "PS3_VIDEO_MODE=720"
if not exist "%HERE%hdd0" mkdir "%HERE%hdd0"
if not exist "%HERE%hdd1" mkdir "%HERE%hdd1"
if not exist "%HERE%logs" mkdir "%HERE%logs"
cd /d "%HERE%"
REM Keep the last run's log as play.prev.err, so a problem report can still include it after a restart.
if exist "%HERE%logs\play.err" copy /y "%HERE%logs\play.err" "%HERE%logs\play.prev.err" >nul
"%HERE%build\gt5.exe" "%HERE%input\EBOOT.elf" > "%HERE%logs\play.out" 2> "%HERE%logs\play.err"
