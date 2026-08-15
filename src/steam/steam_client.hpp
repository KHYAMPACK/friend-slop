#pragma once

/* SteamAPI init / tick / shutdown. No netcode, no RestartAppIfNecessary.
 * Area: steam
 */

bool steam_init();
void steam_tick();
void steam_shutdown();
[[nodiscard]] bool steam_logged_in();
[[nodiscard]] const char *steam_status_line();
