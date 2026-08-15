#pragma once

/* SteamAPI init / tick / shutdown, friends lobby for seeing names.
 * Area: steam. No overlay invite dialog (overlay + OpenGL 0xC0000005).
 * Namespace is HhSteam (Steamworks already owns SteamClient).
 */

#include <cstdint>

namespace HhSteam {

void PrepareLaunch();
bool Init();
void UseExeDirectory();
void Tick();
void Shutdown();
[[nodiscard]] bool LoggedIn();
[[nodiscard]] const char* StatusLine();

void CreateLobby();
[[nodiscard]] bool JoinFriendLobby();
[[nodiscard]] bool InLobby();
[[nodiscard]] bool IsLobbyOwner();
[[nodiscard]] std::uint64_t LobbyOwnerID64();
[[nodiscard]] int LobbyMemberCount();
[[nodiscard]] const char* LobbyMemberName(const int index);
[[nodiscard]] const char* LobbyHint();

} // namespace HhSteam
