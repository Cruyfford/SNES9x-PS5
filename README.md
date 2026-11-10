# SNES9x PS5 Edition

A PS3 homebrew SNES emulator with a PlayStation 5–style interface: a game row
with cover art, a full-screen backdrop and preview video for the selected game,
PS5-style settings, and a control-centre style in-game menu.

Emulation is the [snes9x2010](https://github.com/libretro/snes9x2010) core
(the successor of SNES9x Next), linked into the app.

> Status: **0.2 — first playable build.** Not yet tested on real hardware.

## Install

1. Download `snes9x-ps5.pkg` from the latest run under **Actions**.
2. Install it on a PS3 with HEN or CFW (Package Manager → Install Package Files).
3. Copy ROMs to `/dev_hdd0/game/SNES95000/USRDIR/roms/`
   (USB: `/dev_usb000/SNES/`).

## Artwork and preview videos

Named exactly like the ROM file (without extension):

| What | Folder |
| --- | --- |
| Cover on the game tile | `USRDIR/art/boxart/<rom name>.png` |
| Picture behind the selected game | `USRDIR/art/snap/<rom name>.png` |
| Preview video | `USRDIR/art/video/<rom name>.ogv` |

PNG and JPG work for pictures. [libretro-thumbnails](https://github.com/libretro-thumbnails/Nintendo_-_Super_Nintendo_Entertainment_System)
uses the same naming as No-Intro ROM sets.

Preview videos are Ogg Theora. Convert MP4 clips with
`tools/preview-maker/make_previews.py` (PySide6 window, or command line; needs
ffmpeg with libtheora). The clip plays about a second after you stop on a game.

## Controls

| Button | Home | In game |
| --- | --- | --- |
| ← → / L1 R1 | Move between games / jump 6 | |
| ✕ | Play | SNES B (A with "Cross = A" layout) |
| ○ | Back | SNES A |
| □ △ | | SNES Y / X |
| L1 R1 (L2 R2) | | SNES L / R |
| START | Settings | SNES Start |
| R3, or SELECT + START | | Game menu (save, load, screenshot, display, reset, exit) |

The second connected controller is player 2. Save states (3 slots with
thumbnails), battery saves (`USRDIR/saves`) and screenshots (`USRDIR/screenshots`)
are stored on the hard drive.

## Building

```sh
tools/ci/setup-toolchain.sh ~/ps3          # PSL1GHT toolchain
export PS3DEV=~/ps3/ps3dev PSL1GHT=~/ps3/ps3dev PATH=~/ps3/ps3dev/bin:~/ps3/ps3dev/ppu/bin:$PATH
tools/ci/build-core.sh ps3                 # snes9x2010 core
make pkg
```

`tools/ci/build-core.sh host && tools/host/build.sh` builds a PC version of the
same code with a software renderer that writes PNG screenshots, used for checking
the interface without a console.

## Credits and licences

- Emulator core: Snes9x / [snes9x2010](https://github.com/libretro/snes9x2010) — Snes9x licence (non-commercial)
- Interface font: [Inter](https://github.com/rsms/inter) (SIL OFL 1.1, `assets/fonts/OFL.txt`)
- Image decoding/encoding: [stb](https://github.com/nothings/stb) (public domain)
- Video: libogg / libtheora (BSD)
- PS3 SDK: [PSL1GHT](https://github.com/ps3dev/PSL1GHT), [Tiny3D](https://github.com/wargio/tiny3d)
