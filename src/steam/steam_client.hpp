#pragma once

/* SteamAPI init / tick / shutdown, friends lobby for seeing names.
 * Area: steam. No movement sync, no RestartAppIfNecessary, no overlay.
 */

bool SteamInit();
void SteamUseExeDirectory();
void SteamTick();
void SteamShutdown();
[[nodiscard]] bool SteamLoggedIn();
[[nodiscard]] const char *SteamStatusLine();

void SteamCreateLobby();
void SteamJoinFriendLobby();
[[nodiscard]] bool SteamInLobby();
[[nodiscard]] int SteamLobbyMemberCount();
[[nodiscard]] const char *SteamLobbyMemberName(int index);
[[nodiscard]] const char *SteamLobbyHint();
