# WWE SmackDown vs. Raw 2009 — native PC & Steam Deck

The Xbox 360 version of **WWE SmackDown vs. Raw 2009**, running natively on Windows and on the
Steam Deck: no emulator. The game's own program is recompiled to run directly on your PC, and its
graphics go through a native Vulkan renderer, at 60 fps and up to 4K.

> **You need your own copy of the game.** This project contains none of the game's files and never
> downloads any. It only runs with a disc image (`.iso`) made from your own SvR 2009 disc. It does
> not condone piracy: please buy the game.

## Showcase

*Gameplay clips are on the way.*

## Features

- **Native, not emulated.** The game's PowerPC code is statically recompiled to x86-64
  ([ReXGlue](https://github.com/rexglue/rexglue-sdk)), and the game's own Direct3D calls are drawn
  by a native Vulkan renderer (adapted from [re:Blue](https://github.com/zolaware/reblue)) with
  the game's shaders recompiled ahead of time. No GPU emulation.
- **60 fps** in menus and matches, using the game's own 60 fps mode so game speed stays right
  (the original 30 fps is one setting away).
- **Up to 4K.** Internal resolution follows your screen: 1440p on 1080p and 1440p monitors, 4K on
  4K monitors, 720p on the Steam Deck. Or pick 720p / 1440p / 4K yourself. 16x anisotropic
  filtering.
- **Steam Deck** through Proton: 720p at 60 fps, the correct 16:9 shape (or stretched to fill).
- **Plays straight from your disc image**: nothing is extracted or installed.
- **Controllers** (Xbox, PlayStation, Switch Pro, the Deck's controls) through SDL; the keyboard
  works as a controller too.

## What you need

- **Your own SvR 2009 disc image**, USA / Europe release (one release for both regions: title ID
  `54510826`, media ID `7AFA4596`, about 7.3 GB). Other releases aren't supported yet.
- **Windows 10 or 11, 64-bit**, and a graphics card with **Vulkan** support (AMD, NVIDIA or Intel,
  with a recent driver). Or a **Steam Deck**.
- About 120 MB for the program, plus your disc image.

## Windows: install and play

1. Download `WWE-SVR2009-Native-v<version>-Windows-and-SteamDeck.zip` from the
   [Releases](https://github.com/nefariousjosiah/WWE-SVR2009-Native/releases) page (under
   *Assets*; not *Source code*, which is the developer source).
2. Extract it to a folder of its own, for example `C:\Games\WWE-SVR2009-Native`
   (not inside `Program Files`).
3. Put your disc image (`.iso`) in that folder, next to `svr2009.exe`.
4. Run `svr2009.exe`.

Good to know:

- **Disc image somewhere else?** Start `svr2009.exe` without an `.iso` next to it and it asks you
  to pick one; it remembers your choice.
- **Settings** are in `svr2009.toml` next to the program (open it with Notepad):

  | Setting | Values |
  |---|---|
  | `svr_render_scale` | `0` auto, `1` 720p (original), `2` 1440p, `3` 4K |
  | `fullscreen` | `true` / `false` |
  | `svr_60fps` | `true` 60 fps / `false` the original 30 |
  | `bd_aspect_ratio` | `5` the game's 16:9 (bars on 16:10 screens), `6` stretch to fill |

- **"Windows protected your PC"**: the program isn't code-signed. Click *More info* and then
  *Run anyway*.
- **Quitting:** close the game window (Alt+F4 or the close button).
- **Saves** live in the `userdata` folder next to `svr2009.exe`. Back it up to keep your career
  and created superstars.

### Controls

A controller is recommended; plug it in before starting. On the keyboard (rebind with **F4** in
game):

| Controller | Keyboard | | Controller | Keyboard |
|---|---|---|---|---|
| A | Space / ; | | Left stick | W A S D |
| B | Backspace / ' | | Right stick | Arrow keys |
| X | L | | D-pad | Shift + arrow keys |
| Y | P | | LB / RB | 1 / 3 |
| Start | Enter / X | | LT / RT | Q / E |
| Back | Tab / Z | | Stick presses | F / K |

**F2** shows or hides the FPS counter.

## Steam Deck: install and play

The Windows release runs on the Deck through Proton, Steam's compatibility layer. It holds 60 fps.

**In Desktop Mode** (press the Steam button, *Power*, *Switch to Desktop*):

1. **Get the release.** Download `WWE-SVR2009-Native-v<version>-Windows-and-SteamDeck.zip`
   (under *Assets*, not *Source code*) with a browser, or copy it from your PC together with your
   disc image. A USB stick for the copy must be **exFAT or NTFS**: the disc image is bigger than
   FAT32's 4 GB file limit.
2. **Extract it.** In the *Dolphin* file manager, make a folder such as
   `/home/deck/Games/WWE-SVR2009-Native`, right-click the zip, *Extract*, *Extract archive to...*,
   and choose that folder. (A microSD card works too.)
3. **Add your disc image.** Copy your `.iso` into that folder, next to `svr2009.exe`.
4. **Add it to Steam.** Open Steam (desktop), then *Games* > *Add a Non-Steam Game to My
   Library...* > *Browse...*. Set the file type filter to *All files*, pick `svr2009.exe` in your
   folder, then *Add Selected Programs*.
5. **Turn on Proton.** In your library, right-click *svr2009* > *Properties...* >
   *Compatibility*, tick *Force the use of a specific Steam Play compatibility tool* and choose
   **Proton Experimental** (or the newest Proton). While you are there, rename the shortcut to
   *WWE SmackDown vs. Raw 2009*.

**Back in Game Mode** (the *Return to Gaming Mode* icon on the desktop):

6. Start it from *Library* > *Non-Steam*.

Notes for the Deck:

- **The first start** takes a little longer while Proton sets itself up.
- **Black bars:** the game is 16:9 and the Deck's screen is 16:10, so there are thin bars above
  and below. To fill the screen instead, open `svr2009.toml` in Desktop Mode (right-click,
  *Open with* a text editor such as *KWrite*) and set `bd_aspect_ratio = 6`; the picture gets a
  little taller.
- **Controls:** the Deck's built-in controls work as an Xbox controller with Steam's default
  layout.
- **Artwork:** Steam's library artwork for the shortcut can be set from its properties.

## Troubleshooting

| What you see | What to do |
|---|---|
| The game asks you to pick a disc image | There is no `.iso` next to `svr2009.exe`. Pick yours, or copy it into the folder. |
| The game closes right away | Check the disc image is SvR 2009, USA / Europe release (about 7.3 GB). |
| Black screen, or it closes during play | Update your graphics driver (the renderer needs Vulkan), then try again. If it keeps happening, open an issue with `game.log` from the game's folder. |
| It runs slowly | Set `svr_render_scale = 1` (720p) or `2` (1440p) in `svr2009.toml`: lower resolutions need less from the graphics card. |
| Windows blocks it | *More info* > *Run anyway* (the program isn't code-signed). |

## How it works

- **Recompilation.** [ReXGlue](https://github.com/rexglue/rexglue-sdk) translates the game's Xbox
  360 executable into C++ that is compiled for x86-64. The Xbox 360 system calls it makes are
  provided by the ReXGlue runtime, which also reads the game's data straight from your disc image.
- **Rendering.** Instead of emulating the Xbox 360 GPU, the renderer watches the game's own
  Direct3D library (statically linked into the game) and draws the same scenes through
  [plume](https://github.com/zolaware/plume) on Vulkan. The game's shaders are converted ahead of
  time with [XenosRecomp](https://github.com/zolaware/reblue-XenosRecomp). The emulated GPU only
  keeps the game's command stream moving.
- **60 fps.** The game has its own 60 fps mode, which it normally drops to 30 for matches; a hook
  keeps it at 60, so the game logic and the animations stay in step.

The details, every pitfall included, are in [docs/porting-playbook.md](docs/porting-playbook.md)
and [docs/native-renderer.md](docs/native-renderer.md).

## Building from source

For developers; players use the releases. Windows, with about 30 GB free.

```
git clone https://github.com/nefariousjosiah/WWE-SVR2009-Native.git
cd WWE-SVR2009-Native
powershell -ExecutionPolicy Bypass -File tools\windows\setup.ps1 -Iso "D:\path\to\your disc image.iso"
```

`setup.ps1` installs the tools it needs (Git, CMake, Ninja, LLVM clang, Python, 7-Zip, Visual Studio
2022 Build Tools), fetches the third-party sources at the pinned commits and applies this project's
patches (`patches/`), builds the ReXGlue SDK, extracts your disc image to `assets/`, recompiles
`default.xex` and builds the game.

The native renderer also needs your game's shaders, which can't be in this repository. The first
time, `setup.ps1` builds a shader-dump version and tells you how to collect them: play the menus
and a match or two with shader dumping on, run `tools\native\build_shader_cache.ps1`, then run
`setup.ps1` again.

Afterwards: `tools\windows\build.ps1` rebuilds, `Play WWE 2009.bat` runs it, and
`tools\windows\package_release.ps1 -Version 1.0` makes the release zip (no game files).

Repository layout: `src/` the game-specific code (`native/` renderer glue, `reblue/` the renderer),
`config/` recompiler configuration, `patches/` changes to the third-party sources, `tools/` scripts,
`docs/` notes.

## Credits

- [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) by Tom Clay, derived from
  [Xenia](https://xenia.jp): the recompiler and runtime.
- [re:Blue](https://github.com/zolaware/reblue) (Blue Dragon), the renderer this one is adapted
  from, with [plume](https://github.com/zolaware/plume) and
  [XenosRecomp](https://github.com/zolaware/reblue-XenosRecomp).
- [zstd](https://github.com/facebook/zstd); the Press Start 2P font.

Licences of all of these: [THIRD_PARTY.md](THIRD_PARTY.md).

## Licence and legal

This project's own code is MIT licensed ([LICENSE](LICENSE)). The repository contains no game code
or data; the releases contain the recompiled program, and every bit of game data (models,
textures, sound, video) comes from your own disc image.

WWE, SmackDown vs. Raw and all related names, characters and content belong to their respective
owners (WWE, THQ, Yuke's). This is an unofficial fan project, not affiliated with or endorsed by
them or by Microsoft.
