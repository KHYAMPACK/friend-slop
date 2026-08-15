#pragma once

/* SteamAPI init / tick / shutdown, friends lobby for seeing names.
 * Area: steam. No movement sync, no RestartAppIfNecessary.
 */

bool SteamInit();
void SteamTick();
void SteamShutdown();
[[nodiscard]] bool SteamLoggedIn();
[[nodiscard]] const char *SteamStatusLine();

void SteamCreateLobby();
void SteamOpenInvite();
[[nodiscard]] bool SteamInLobby();
[[nodiscard]] int SteamLobbyMemberCount();
[[nodiscard]] const char *SteamLobbyMemberName(int index);
[[nodiscard]] const char *SteamLobbyHint();
