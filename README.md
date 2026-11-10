# SNES9x PS5 Edition

A PS3 homebrew SNES front end with a PlayStation 5–style interface: a game row
with cover art, a full-screen backdrop for the selected game, preview videos
(planned) and PS5-style settings and in-game menus.

> Status: **0.1 — interface preview.** The home screen, game library, artwork
> and settings work. Running games (the Snes9x 2010 core), the in-game menu and
> preview videos come in the next updates.

## Install

1. Download `snes9x-ps5.pkg` from the latest run under **Actions**.
2. Install it on a PS3 with HEN or CFW (Package Manager → Install Package Files).
3. Copy ROMs to `/dev_hdd0/game/SNES95000/USRDIR/roms/`
   (USB: `/dev_usb000/SNES/`).

## Artwork

Named exactly like the ROM file (without extension):

| What | Folder |
| --- | --- |
| Cover on the game tile | `USRDIR/art/boxart/<rom name>.png` |
| Picture behind the selected game | `USRDIR/art/snap/<rom name>.png` |
| Preview video (planned) | `USRDIR/art/video/<rom name>.ogv` |

PNG and JPG work. [libretro-thumbnails](https://github.com/libretro-thumbnails/Nintendo_-_Super_Nintendo_Entertainment_System)
uses the same naming as No-Intro ROM sets.

## Controls

| Button | Home | Settings |
| --- | --- | --- |
| ← → | Move between games | Change a value |
| L1 / R1 | Jump 6 games | |
| ↓ / ↑ | Play button row / top bar | Move |
| ✕ | Play | Select / change |
| ○ | Back | Back |
| START | Settings | Home |

(✕ and ○ swap on consoles set to Japanese button layout.)

## Building

```sh
tools/ci/setup-toolchain.sh ~/ps3          # PSL1GHT toolchain
export PS3DEV=~/ps3/ps3dev PSL1GHT=~/ps3/ps3dev PATH=~/ps3/ps3dev/bin:~/ps3/ps3dev/ppu/bin:$PATH
make pkg
```

`tools/host/build.sh` builds a PC preview of the same UI code (software
renderer, writes PNG screenshots) for checking layouts without a console.

## Credits

- Interface font: [Inter](https://github.com/rsms/inter) (SIL OFL 1.1, see `assets/fonts/OFL.txt`)
- Image decoding: [stb_image](https://github.com/nothings/stb) (public domain)
- PS3 SDK: [PSL1GHT](https://github.com/ps3dev/PSL1GHT), [Tiny3D](https://github.com/wargio/tiny3d)
