#pragma once

/* SteamAPI init / tick / shutdown, friends lobby for seeing names.
 * Area: steam. No overlay invite dialog (overlay + OpenGL 0xC0000005).
 */

#include <cstdint>

void SteamPrepareLaunch();
bool SteamInit();
void SteamUseExeDirectory();
void SteamTick();
void SteamShutdown();
[[nodiscard]] bool SteamLoggedIn();
[[nodiscard]] const char *SteamStatusLine();

void SteamCreateLobby();
[[nodiscard]] bool SteamJoinFriendLobby();
[[nodiscard]] bool SteamInLobby();
[[nodiscard]] bool SteamIsLobbyOwner();
[[nodiscard]] std::uint64_t SteamLobbyOwnerID64();
[[nodiscard]] int SteamLobbyMemberCount();
[[nodiscard]] const char *SteamLobbyMemberName(int index);
[[nodiscard]] const char *SteamLobbyHint();
