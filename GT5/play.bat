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
REM Mouse steering, off unless you turn it on: in races, move the mouse left and right to steer;
REM left button = throttle, right = brake, middle = straighten the wheel. The keyboard keeps working,
REM at the menus the mouse does nothing, and Alt+Tab frees it. (It plays the wheel, so a real wheel
REM isn't used while it's on.) Remove a REM to use a line:
REM set "PS3_MOUSE_STEER=1"
REM set "PS3_MOUSE_STEER_SENS=150"    steering speed in percent (100 = a 1080p screen's width to full lock)
REM set "PS3_MOUSE_STEER_CENTER=100"  the wheel straightens by itself when the mouse stops (0 = off)
REM Cockpit view, as the game has it unless you change it (or the launcher's Display page does): a wider
REM view from the driver's seat, and the eye moved. Only the cockpit view changes. Remove a REM to use a line:
REM set "PS3_COCKPIT_FOV=20"       degrees wider, top to bottom of the screen (game: 52.4; negative = narrower)
REM set "PS3_COCKPIT_SEAT=10"      cm forward, toward the windscreen (negative = back)
REM set "PS3_COCKPIT_HEIGHT=5"     cm up (negative = down)
REM Screen shape. auto = your monitor's: on an ultrawide (21:9, 32:9) the game draws a wider view,
REM more of the track at the sides, with the HUD and menus kept 16:9 in the middle. The window opens
REM at that shape; maximize it. On a 16:9 monitor nothing changes. Or a fixed shape: 21:9, 32:9,
REM 3440x1440. 16:9 = the PS3's own picture.
set "PS3_ASPECT=auto"
REM set "PS3_WINDOW_SIZE=2560x1080"   open the window at exactly this size
REM Resolution: the TV mode the game is told it's on, as on a PS3. 1080 = its 1080p mode (races drawn
REM at 1728x972, sharp HUD and menus at 1920x1080). 720 = its 720p mode (races at 1152x648), for a slow
REM graphics chip. The window title shows the mode.
set "PS3_VIDEO_MODE=1080"
REM Skip the opening movie: a quicker start, and on a busy or slower PC that movie could stop on one
REM frame at the first start (#10). GT TV still plays it. Remove this line to watch it.
set "PS3_SKIP_INTRO=1"
REM Downloadable content (DLC): GT5 reads its DLC "vouchers" (hdd0\game\<title>\USRDIR\vou) the way the
REM PS3 kernel serves them, as stored for a plain open and decrypted for the licensed one. Each needs its
REM licence key in hdd0\home\00000001\exdata (README, Game updates and DLC). Without DLC nothing changes.
set "PS3_EDAT_STRICT=1"
REM The Downloadable Content screen wants a PSN sign-in. Only when DLC is installed, the port reports
REM signed in, with a local profile and no connection to anything; without DLC the game stays offline
REM and doesn't try for tickets. set "PS3_NP_SIGNED_IN=0" in settings.bat to never sign in.
for /d %%G in ("%PS3_HDD0_ROOT%\game\*") do if exist "%%G\USRDIR\vou\*.edat" set "PS3_NP_SIGNED_IN=1"
REM The launcher (GT5Launcher.exe in the install folder) saves its settings in settings.bat; they win
REM over the lines above. Without the launcher there's no settings.bat and the lines above count.
if exist "%HERE%settings.bat" call "%HERE%settings.bat"
REM Renderer: DirectX 12 unless GT5_RENDERER=vulkan (Display > Renderer in the launcher or the F1 menu).
REM Vulkan is built in at install and by updates (then build\SDL2.dll is there).
if /i not "%GT5_RENDERER%"=="vulkan" goto renderer_set
if not exist "%HERE%build\SDL2.dll" goto renderer_set
set "RSX_LIVE_DRAW="
set "PS3RECOMP_VK_WINDOW=1"
set "PS3RECOMP_VK_GUEST_PROGRAMS=1"
set "VK_DRAW_BATCH=1"
set "PS3RECOMP_VK_ASYNC_SHADERS=1"
set "PS3RECOMP_VK_NO_READBACK=1"
set "PS3RECOMP_SPIRV_CACHE=%HERE%shadercache"
REM Present every flip: the DirectX 12 engine's "nothing new to show" test doesn't apply to Vulkan.
set "LD_SKIP_EMPTY_PRESENT=0"
if not exist "%HERE%shadercache" mkdir "%HERE%shadercache"
:renderer_set
if not exist "%HERE%hdd0" mkdir "%HERE%hdd0"
if not exist "%HERE%hdd1" mkdir "%HERE%hdd1"
if not exist "%HERE%logs" mkdir "%HERE%logs"
cd /d "%HERE%"
REM Keep the last run's log as play.prev.err, so a problem report can still include it after a restart.
if exist "%HERE%logs\play.err" copy /y "%HERE%logs\play.err" "%HERE%logs\play.prev.err" >nul
"%HERE%build\gt5.exe" "%HERE%input\EBOOT.elf" > "%HERE%logs\play.out" 2> "%HERE%logs\play.err"
