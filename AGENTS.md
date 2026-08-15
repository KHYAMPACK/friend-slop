# AGENTS.md — Heart House (working title)

Orientation for AI agents working on this game. This is a **Raylib** multiplayer party game, coded from the console (no visual editor). Same fantasy as the Godot prototype; this repo is C + Raylib only.

## One-line pitch

Four friends get the same house. Each secretly traps it and hides a heart. Then you run your friend’s house while the lobby watches and triggers traps. Steal the heart. Ruin friendships.

## Design pillars

- Fun comes from **map manipulation**, not aim or combat.
- Building must stay **casual-simple** (menu + snap place, not full sandbox).
- Spectating is an active role (buttons), not AFK waiting.
- Chaos and comedy > hardcore optimization.

## Core loop (concrete)

### Setup

- 4 players.
- Shared premade map (start with one: **House**).
- Objective object: a **Heart** somewhere in the house. Touch it = success for that run.

### Phase 1 — Build (~60–90 seconds)

- Each player gets their **own copy** of the same base house.
- Trap/build UI is a **small menu** (about 8 items), not a free toolgun.
- Starter item set:
  - wall
  - locked door
  - jump pad
  - explosive barrel
  - spike floor
  - fake / falling floor
  - launcher
  - alarm / noise
- Hard limit: about **6 placements** per player.
- Controls: aim → place (snap) → optional simple rotate → delete.
- No complex wiring graphs in v1. Traps are self-contained or spectator-triggered.
- Player also places/hides the **Heart**.
- When timer ends, each layout is saved as that player’s house variant.

### Phase 2 — Raid / Rotate

- Runner is dropped into **another player’s** house (not their own).
- Goal: reach the Heart without dying / failing.
- Other players **spectate** that run.
- Spectators get a few **big buttons** tied to placed traps, e.g.:
  - CLOSE DOOR
  - FIRE LAUNCHER
  - DROP FLOOR
- Spectator power should be limited (charges/cooldowns) so runs stay fair and funny, not pure bullying.
- After a run: score (heart stolen / defense success), then rotate so everyone runs everyone else’s house.

### Match end

- Highest score wins the lobby (hearts stolen and/or successful defenses — exact scoring can be tuned).

## What “simple building” means (non-negotiable)

Players should feel like they are **decorating a house for war crimes with a shopping cart**, not engineering systems.

- Prop menu, not sandbox.
- Snap-to-valid-spots preferred (doorframes, hallway tiles, stairs, etc.).
- Short build timer.
- Budget of placements.
- Optional later: one-click preset kits (“Hallway Hell”, etc.).
- v1 trap types = Block / Yeet / Hurt / Confuse only.

## Tech

- Engine: **Raylib 5.5** (fetched by CMake; do not vendor a second copy)
- Language: **C11** (default unless the team explicitly switches)
- Build: **CMake** is the only supported build. Both people use the same commands.
- Workflow: **console / source files only** — no Godot, no visual scene editor, no generated project files checked in
- Multiplayer: required for the real fantasy; prototype **local-first** (same machine / hotseat / split or sequential local) before full online
- Repo: this Raylib project only (not the Godot `heart-house` repo, not Tilky Engine)

## Project layout

Folders map to ownership so diffs stay reviewable:

```
src/
  main.c           # entry + window + main loop; phase switching lives here later
  core/            # shared constants + tiny types (no gameplay systems)
  maps/house/      # premade House graybox / map
  player/          # movement + camera
  heart/           # Heart objective
  build/           # hotbar, snap place, budget, timer, save layout
  traps/           # one .c + .h per trap type
  raid/            # run mode, win/fail, scoring hooks
  spectate/        # spectator camera + trigger buttons
  ui/              # menus / HUD
assets/            # textures, models, sfx (committed; not build output)
```

- One folder ≈ one job.
- Colocate `thing.c` + `thing.h`.
- Prefer editing inside the owning folder over inventing parallel systems.
- Named constants live in `src/core/game_constants.h` — no magic numbers scattered.

## Code style (human-first)

Written so a teammate can review AI diffs without guessing:

- Short file header: what it does + which area owns it.
- One responsibility per `.c` file. If it needs “and also…”, split it.
- Prefer plain Raylib + small C files over engines, ECS frameworks, or plugin stacks.
- Name files for what the player sees (`spike_floor`, `locked_door`), not abstractions.
- Keep PRs/commits small enough to read in one sitting.
- Match `.clang-format` (4-space indent). Do not fight the formatter.
- CMakeLists.txt is the source of truth for which files compile. When you add a `.c`, add it there in the same change.

## Collaboration rules (2-person team)

This repo is meant for **two people + Cursor agents** working from the console.

- Use Git. **Pull (or rebase) before you start.** Push when a chunk actually runs.
- Prefer **feature branches + PRs into `main`**. Do not both commit straight to `main` for overlapping work.
- Never commit `build/`, compiler output, `.vs/`, or local CMake caches. `.gitignore` already covers this.
- **Do not edit the same `.c` / `.h` at the same time.** Split by the ownership table. If you must touch a shared file (`main.c`, `game_constants.h`, `CMakeLists.txt`), ping the other person first and keep the diff tiny.
- Keep commits small and descriptive.
- Raylib itself is downloaded by CMake (FetchContent). Do not copy raylib sources into the repo.
- Shared tunables (`MAX_PLACEMENTS`, timers) change only when both people agree — they are match rules, not local prefs.
- Assets that the game needs belong in `assets/` and get committed. Scratch / export junk does not.

### Playtest loop (how we work with the agent)

- Human role: **direction + playtesting**. Give goals, pick options when asked, report bugs.
- Agent role: implement small slices, then end with a short **“What you should see”** test checklist (including the exact build + run commands).
- Human reports: what worked, what broke, what felt wrong — then agent fixes or asks.
- Do not assume a change is good until a playtest report says so.

## Suggested ownership split

| Area | Owns |
|------|------|
| Map / player | House graybox, movement, camera, Heart win/fail |
| Build phase | Trap hotbar, snap place, budget, timer, save layout |
| Raid / spectate | Load layout, spectator camera, trigger buttons, scoring |

`src/main.c`, `src/core/`, and `CMakeLists.txt` are shared. Touch them last, in small diffs, and tell the other person.

## How to build (both people, same commands)

Needs: Git, CMake 3.16+, a C compiler (Visual Studio Build Tools on Windows, or MinGW).

```
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

Windows (MSVC) run:

```
.\build\Debug\heart_house.exe
```

Other generators may put the binary at `build/heart_house`.

First configure downloads Raylib — needs network once. After that, offline builds are fine.

## Vertical slice (build this first)

1. One house (boxes / graybox in code or a simple model).
2. Player can place up to 6 snap props from a tiny menu.
3. Place/hide a Heart.
4. Switch to run mode on that layout.
5. Second local “spectator” can press 2–3 trigger buttons.
6. Touch Heart = win; trap fail = lose.

If that slice already makes people yell on voice chat, the idea is validated.

## Non-goals (for now)

- Full Garry’s Mod-style sandbox.
- Deep combat / gun fantasy as the main skill.
- Complex wire/logic puzzles in the build phase.
- Huge content library before the loop is fun.
- Building this in Godot or Tilky Engine (this repo is Raylib).
- Online netcode before local hotseat/split is fun.

## Tone / product feel

Friend-slop party game: fast, nostalgic, absurd, readable, scream-on-Discord energy. Architecture and betrayal matter more than mechanical skill.

### Visual direction (decided)

**PS2 / early-3D jank** — chunky readable shapes, slightly ugly on purpose, nostalgic low-fi. Prefer simple materials and bold silhouettes over realism or modern PBR polish. Draw with Raylib primitives / simple meshes first; art pack later if the loop is fun.

## Agent instructions

- Prefer the smallest change that makes the slice more playable.
- Keep build UX dead simple and readable.
- When adding traps, make them readable and spectator-triggerable when relevant.
- Don’t expand scope into combat systems unless asked.
- Match existing naming and file style once files exist.
- Do not add extra libraries, engines, or build systems unless asked. Raylib + CMake only.
- When adding a source file, update `CMakeLists.txt` in the same change.
- Ask before changing core loop rules (phases, heart objective, 6-placement budget, spectator buttons).

### Ask — don’t invent (non-negotiable)

If you are stuck or a real product choice is needed, **stop and ask**. Do not silently pick a direction.

Ask instead of inventing for things like:
- Art style, palette, camera feel, UI look, audio mood
- New mechanics, trap behavior, scoring, control schemes
- Folder/architecture forks that aren’t already in this file
- Language/engine switches (C++ / other libs)
- Anything ambiguous that would be hard to undo

Allowed without asking:
- Following decisions already written in this file
- Tiny implementation details that don’t change the fantasy (variable names, local refactors inside one owned file)
- Bugfixes that restore intended behavior

When asking: one short question, 2–3 concrete options max, then wait.

After every playable change, end the reply with **What you should see** (build/run commands + controls + expected result) so the human can playtest and report bugs.
