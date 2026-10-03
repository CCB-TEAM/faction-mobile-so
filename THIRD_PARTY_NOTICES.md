# Third-party notices

## KardsFactionToolkit (vendored baseline)

`upstream/KardsFactionToolkit/` is a copy of
[mdzz195/KardsFactionToolkit](https://github.com/mdzz195/KardsFactionToolkit)
at commit `b602ad9` ("Initial release: configurable KARDS faction DLL toolkit").

- License: MIT
- Copyright (c) 2026 KardsFactionToolkit contributors
- Full text: `upstream/KardsFactionToolkit/LICENSE`

It is vendored so that the Android port can be developed against the exact
baseline it derives from. Its own `THIRD_PARTY_NOTICES.md` and the MinHook
license under `upstream/KardsFactionToolkit/vendor/minhook/LICENSE.txt` apply to
that subtree unchanged.

Note: the upstream project's generated artifacts (address profiles, machine-code
byte headers) are specific to Windows x64 builds and are not reused here. The
Android address table in `profiles/` was derived independently.

## Hook backends (not vendored)

The skeleton references Dobby and ShadowHook but does not include either.
Whichever is added must ship with its own license file.

- Dobby — <https://github.com/jmpews/Dobby> (Apache-2.0)
- ShadowHook — <https://github.com/bytedance/android-inline-hook> (MIT)

## Game binaries

No KARDS binaries, APK files, extracted assets, or game data are included in
this repository. KARDS and all related assets are the property of 1939 Games.
