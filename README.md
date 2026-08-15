# Heart House (Raylib)

Two-person C + Raylib party game. Design and agent rules: [AGENTS.md](AGENTS.md).

## Needs

- Git
- CMake 3.16+
- A C compiler (Visual Studio Build Tools on Windows, or MinGW)

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

First configure downloads Raylib (needs network once).

## Two-person workflow

1. `git pull` (or rebase) before you start.
2. Work on a feature branch. Open a PR into `main`.
3. Stay in your ownership folder when you can (see AGENTS.md). Do not both edit the same `.c` / `.h`.
4. Push when it compiles and runs.
5. Playtest, then report what felt wrong.

Do not commit `build/` or binaries. CMake is the only supported build.
