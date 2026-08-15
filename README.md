# Heart House (Raylib)

Two-person C++ + Raylib party game. Design and agent rules: [AGENTS.md](AGENTS.md).

## Needs

- Git
- CMake 3.16+
- A C++ compiler (Visual Studio Build Tools on Windows, or MinGW)
- Steam client (running when you playtest)
- Steamworks SDK (local, not in git)

## Steamworks SDK (one-time, both people)

1. Log into [Steamworks partner downloads](https://partner.steamgames.com/downloads/list) and download the Steamworks SDK.
2. Extract so `public/steam/steam_api.h` exists. Either:
   - put the SDK in `third_party/steamworks/` (gitignored), or
   - point CMake at it: `cmake -B build -DSTEAMWORKS_SDK=C:/path/to/sdk`
3. You need the 64-bit redistributable: `redistributable_bin/win64/steam_api64.lib` and `steam_api64.dll`.

CMake copies `steam_api64.dll` and `steam_appid.txt` (App ID **480**, Spacewar) next to the exe. Do not commit the SDK. Do not call `SteamAPI_RestartAppIfNecessary` with 480.

## Clone, build, run

```
git clone <this-repo-url>
cd hh
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

Windows (MSVC):

```
.\build\Debug\heart_house.exe
```

First configure downloads Raylib (needs network once). Steam must be **open and logged in**.

## Test seeing your friend’s name

Both of you: Steam open, **Steam friends**, same source pulled, both windows already running.

1. You press **C** (host). Your name appears under `In lobby`.
2. Friend presses **J** (join). Do not use Steam overlay invites.
3. Both windows should list **two names**.

If his IDE prints `0xC0000005`, he should pull this, **delete his build folder**, reconfigure CMake, rebuild, then run `heart_house.exe` directly (not the debugger). Steam overlay + Debug CRT crashes on some PCs.

No movement sync yet — names only.

## Two-person workflow

1. `git pull` (or rebase) before you start.
2. Work on a feature branch. Open a PR into `main`.
3. Stay in your ownership folder when you can (see AGENTS.md). Do not both edit the same `.cpp` / `.hpp`.
4. Push when it compiles and runs.
5. Playtest, then report what felt wrong.

Do not commit `build/`, binaries, or the Steamworks SDK. CMake is the only supported build. Movement sync is not in this slice.
