#pragma once

/* Shared tunables for the match loop.
 * Area: core (both owners may read; change only when the team agrees). */

namespace hh {

inline constexpr int MAX_PLAYERS = 4;
inline constexpr int MAX_PLACEMENTS = 6;
inline constexpr float BUILD_TIME_SEC = 75.0f;
inline constexpr float BUILD_SNAP_M = 1.0f;
inline constexpr float BUILD_RAY_M = 10.0f;

/* Trap catalog size target for v1 (menu items, not free sandbox). */
inline constexpr int STARTER_TRAP_COUNT = 8;

inline constexpr int WINDOW_WIDTH = 1280;
inline constexpr int WINDOW_HEIGHT = 720;
inline constexpr int TARGET_FPS = 60;

} // namespace hh
