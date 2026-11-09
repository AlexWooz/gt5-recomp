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
if not exist "%HERE%hdd0" mkdir "%HERE%hdd0"
if not exist "%HERE%hdd1" mkdir "%HERE%hdd1"
if not exist "%HERE%logs" mkdir "%HERE%logs"
cd /d "%HERE%"
"%HERE%build\gt5.exe" "%HERE%input\EBOOT.elf" > "%HERE%logs\play.out" 2> "%HERE%logs\play.err"
