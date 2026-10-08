# Third-party components

| Component | How it is included | Licence |
|---|---|---|
| [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) (recompiler + runtime; portions from [Xenia](https://xenia.jp)) | fetched at `c94f5eb` by `tools/windows/setup.ps1`, plus `patches/rexglue-sdk.patch` | BSD 3-Clause (`third_party/rexglue-sdk/LICENSE`); its own dependencies carry their own licences |
| [re:Blue](https://github.com/zolaware/reblue) renderer code | adapted in `src/reblue` | BSD 3-Clause, `src/reblue/LICENSE.reblue` |
| [plume](https://github.com/zolaware/plume) | fetched at `e0c8871`, plus `patches/plume.patch` | MIT |
| [XenosRecomp](https://github.com/zolaware/reblue-XenosRecomp) (build tool, shader recompiler) | fetched at `339af41`, plus `patches/xenosrecomp.patch` | MIT |
| [zstd](https://github.com/facebook/zstd) (decompression, single-file decoder) | `third_party/reblue_thirdparty/zstd` | BSD, `third_party/reblue_thirdparty/zstd/LICENSE` |
| Press Start 2P font (FPS counter) | `res/fonts` | SIL Open Font License 1.1, `res/fonts/OFL.txt` |
