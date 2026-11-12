# 🗂️ Version history

Every release of the port, newest first. The details of each are on the
[Releases page](https://github.com/MormonFTP-hub/gt5-recomp/releases) and in [CHANGELOG.md](CHANGELOG.md);
the launcher's Updates page shows the newest one's notes.

| Version | Date | What changed |
|---|---|---|
| [v0.1.23](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.23) | 12 Nov 2026 | Speed: 30 fps no longer picked by itself, launcher bug that kept it on fixed, title shows game frames; 1 ms timer kept when hidden; installer: Asian release and 2.01; Discord server |
| [v0.1.22](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.22) | 12 Nov 2026 | Game updates (2.xx) run: SPURS workload flag, 2.01's SPU jobs; Hong Kong/Asian release BCAS20108 tested (01.00 and 2.01) (#18) |
| [v0.1.21](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.21) | 12 Nov 2026 | Skip the opening movie at startup (PS3_SKIP_INTRO, on by default): under heavy CPU load it could freeze the first start (#10) |
| [v0.1.20](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.20) | 12 Nov 2026 | Laptops draw on their dedicated GPU ([gpu] line in play.err, PS3_GPU); launcher Speed setting (30 fps) picked for your PC |
| [v0.1.19](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.19) | 12 Nov 2026 | Fix the black screen from a GT.VOL split into pieces by a FAT32 dump, or missing (#12): joined automatically, and the launcher warns |
| [v0.1.18](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.18) | 12 Nov 2026 | Installer: recognise GT5 by its Chinese, Japanese and Korean titles (#18); say when a copy has a 2.xx update built in |
| [v0.1.17](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.17) | 12 Nov 2026 | Alt+Enter no longer freezes the picture (#10) |
| [v0.1.16](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.16) | 12 Nov 2026 | Fix wrong track textures (#14): GT5's graphics-memory copies (NV0039) now happen |
| [v0.1.15](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.15) | 12 Nov 2026 | The launcher: settings, borderless full screen, Play, Updates and Help in one window (English and Chinese); double-click `Update.bat`; the installer lets other GT5 releases through |
| [v0.1.14](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.14) | 12 Nov 2026 | Fix updates stopping with "Access is denied" on `GT5\spu`; the version in the window title |
| [v0.1.13](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.13) | 11 Nov 2026 | Hotfix: installs and updates work again (v0.1.12's patch didn't apply) |
| [v0.1.12](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.12) | 11 Nov 2026 | 1080p fixed and the default again: the window shows the whole picture |
| [v0.1.11](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.11) | 11 Nov 2026 | Hotfix: back to 720p while v0.1.10's 1080p window was cropped |
| [v0.1.10](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.10) | 11 Nov 2026 | 1080p: the game's own 1080p mode; `play.bat` keeps the previous log |
| [v0.1.9](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.9) | 11 Nov 2026 | Installer: fits high-DPI screens, unpacks RPCS3 on Windows 10, keeps a log; US release confirmed |
| [v0.1.8](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.8) | 10 Nov 2026 | Builds find and redo damaged compiles |
| [v0.1.7](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.7) | 10 Nov 2026 | Builds retry with fewer jobs instead of stopping; Install section for the one-click installer |
| [v0.1.6](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.6) | 10 Nov 2026 | Ultrawide: the game's half that v0.1.5 left out |
| [v0.1.5](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.5) | 10 Nov 2026 | Ultrawide monitors: a wider view, HUD kept 16:9 in the middle |
| [v0.1.4](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.4) | 10 Nov 2026 | Black screen on start fixed (player builds compile the SPU code) |
| [v0.1.3](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.3) | 10 Nov 2026 | First try at the black-screen fix (didn't build; v0.1.4 did) |
| [v0.1.2](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.2) | 10 Nov 2026 | Steering wheels and force feedback (untested on real wheels) |
| [v0.1.1](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.1) | 10 Nov 2026 | One-command setup that checks your files and builds the game |
| [v0.1.0](https://github.com/MormonFTP-hub/gt5-recomp/releases/tag/v0.1.0) | 10 Nov 2026 | Numbered versions and one-command updates (`update.ps1`) |
| (before) | 1-9 Nov 2026 | From first boot to playable: see [CHANGELOG.md](CHANGELOG.md) |

To update: the launcher's **Updates** page, or double-click `Update.bat` in your install folder (see [Updating](README.md#updating)). To go back to one of these: `update.ps1 -Version <version>`.
