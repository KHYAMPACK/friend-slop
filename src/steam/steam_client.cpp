/* SteamAPI init / tick / shutdown, friends lobby for seeing names.
 * Area: steam. No movement sync, no overlay (overlay + OpenGL 0xC0000005 on some PCs).
 */

#include "steam/steam_client.hpp"

#include "core/game_constants.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>
#include <ostream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
#include "steam/steam_api.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace {

constexpr std::size_t statusMax = 256;
constexpr std::size_t nameMax = 128;
constexpr char offlineStatus[] = "Open Steam and relaunch";
constexpr char unknownName[] = "(unknown)";
constexpr char hintCreate[] = "You: C to host. Friend: J to join. You must be Steam friends.";
constexpr char hintHost[] = "Lobby up. Friend presses J.";
constexpr char hintOffline[] = "Open Steam and relaunch before creating a lobby.";
constexpr char hintNoLobby[] = "No friend lobby yet. They press C first. You must be Steam friends.";

bool g_ok = false;
std::array<char, statusMax> g_status{};

void CopyAscii(char *dst, std::size_t dstSize, const char *src)
{
    if (dst == nullptr || dstSize == 0) {
        return;
    }
    std::size_t out = 0;
    if (src != nullptr) {
        for (std::size_t i = 0; src[i] != '\0' && out + 1 < dstSize; ++i) {
            const unsigned char ch = static_cast<unsigned char>(src[i]);
            dst[out++] = (ch >= 32 && ch < 127) ? static_cast<char>(ch) : '?';
        }
    }
    dst[out] = '\0';
}

void SetStatus(const char *text)
{
    CopyAscii(g_status.data(), g_status.size(), text);
}

class SteamSession {
public:
    void CreateFriendsLobby()
    {
        ISteamMatchmaking *matchmaking = SteamMatchmaking();
        if (matchmaking == nullptr) {
            std::cout << "matchmaking is null \n";
            return;
        }
        if (inLobby && lobbyId.IsValid()) {
            matchmaking->LeaveLobby(lobbyId);
            inLobby = false;
            memberCount = 0;
            lobbyId.Clear();
        }
        std::cout << "pre create lobby\n";
        const SteamAPICall_t call = matchmaking->CreateLobby(k_ELobbyTypeFriendsOnly, hh::MAX_PLAYERS);
        std::cout << "post creat lobby\n";
        lobbyCreated.Set(call, this, &SteamSession::OnLobbyCreated);
        std::cout << "lobby created.set \n";
        SetHint(hintHost);
        std::cout << "post set hing \n";
    }

    bool JoinFriendLobby()
    {
        ISteamFriends *friends = SteamFriends();
        if (friends == nullptr) {
            SetHint(hintNoLobby);
            return false;
        }

        uint32 appId = 480;
        ISteamUtils *utils = SteamUtils();
        if (utils != nullptr) {
            appId = utils->GetAppID();
        }

        int friendCount = friends->GetFriendCount(k_EFriendFlagImmediate);
        if (friendCount < 0) {
            friendCount = 0;
        }
        for (int i = 0; i < friendCount; ++i) {
            std::cout << "pre\n";
            const CSteamID friendId = friends->GetFriendByIndex(i, k_EFriendFlagImmediate);
            std::cout << "post\n";
            if (!friendId.IsValid()) {
                continue;
            }
            FriendGameInfo_t gameInfo{};
            if (!friends->GetFriendGamePlayed(friendId, &gameInfo)) {
                continue;
            }
            if (!gameInfo.m_steamIDLobby.IsValid()) {
                continue;
            }
            if (gameInfo.m_gameID.AppID() != appId) {
                continue;
            }
            QueueJoin(gameInfo.m_steamIDLobby);
            SetHint("Joining friend's lobby...");
            return true;
        }
        SetHint(hintNoLobby);
        return false;
    }

    void FlushPending()
    {
        ISteamMatchmaking *matchmaking = SteamMatchmaking();
        if (pendingLeave) {
            pendingLeave = false;
            if (matchmaking != nullptr && inLobby && lobbyId.IsValid()) {
                matchmaking->LeaveLobby(lobbyId);
            }
            inLobby = false;
            memberCount = 0;
            lobbyId.Clear();
            if (pendingJoin) {
                return;
            }
        }
        if (pendingJoin) {
            pendingJoin = false;
            if (matchmaking != nullptr && pendingJoinId.IsValid()) {
                matchmaking->JoinLobby(pendingJoinId);
            }
            pendingJoinId.Clear();
            return;
        }
        if (pendingRefresh) {
            pendingRefresh = false;
            RefreshMembers();
        }
    }

    [[nodiscard]] bool InLobby() const
    {
        return inLobby;
    }

    [[nodiscard]] int MemberCount() const
    {
        return memberCount;
    }

    [[nodiscard]] const char *MemberName(int index) const
    {
        if (index < 0 || index >= memberCount) {
            return unknownName;
        }
        return memberNames[static_cast<std::size_t>(index)].data();
    }

    [[nodiscard]] const char *Hint() const
    {
        return hint.data();
    }

    [[nodiscard]] std::uint64_t OwnerSteamID64() const
    {
        ISteamMatchmaking *matchmaking = SteamMatchmaking();
        if (!inLobby || !lobbyId.IsValid() || matchmaking == nullptr) {
            return 0;
        }

        const CSteamID owner = matchmaking->GetLobbyOwner(lobbyId);
        if (!owner.IsValid() || !owner.BIndividualAccount()) {
            return 0;
        }
        return owner.ConvertToUint64();
    }

    [[nodiscard]] bool IsOwner() const
    {
        ISteamUser *user = SteamUser();
        if (user == nullptr) {
            return false;
        }
        const std::uint64_t owner = OwnerSteamID64();
        return owner != 0 && owner == user->GetSteamID().ConvertToUint64();
    }

    void Leave()
    {
        ISteamMatchmaking *matchmaking = SteamMatchmaking();
        if (matchmaking != nullptr && inLobby && lobbyId.IsValid()) {
            matchmaking->LeaveLobby(lobbyId);
        }
        inLobby = false;
        lobbyId.Clear();
        memberCount = 0;
        pendingJoin = false;
        pendingLeave = false;
        pendingRefresh = false;
    }

private:
    void SetHint(const char *text)
    {
        CopyAscii(hint.data(), hint.size(), text);
    }

    void QueueJoin(CSteamID id)
    {
        if (!id.IsValid()) {
            return;
        }
        if (inLobby && lobbyId.IsValid() && lobbyId != id) {
            pendingLeave = true;
        }
        pendingJoinId = id;
        pendingJoin = true;
    }

    void RefreshMembers()
    {
        ISteamMatchmaking *matchmaking = SteamMatchmaking();
        ISteamFriends *friends = SteamFriends();
        if (matchmaking == nullptr || friends == nullptr || !inLobby || !lobbyId.IsValid()) {
            memberCount = 0;
            return;
        }

        int count = matchmaking->GetNumLobbyMembers(lobbyId);
        if (count < 0) {
            count = 0;
        }
        if (count > hh::MAX_PLAYERS) {
            count = hh::MAX_PLAYERS;
        }

        std::array<std::array<char, nameMax>, hh::MAX_PLAYERS> nextNames{};
        for (int i = 0; i < count; ++i) {
            const CSteamID id = matchmaking->GetLobbyMemberByIndex(lobbyId, i);
            const char *name = unknownName;
            if (id.IsValid()) {
                const char *persona = friends->GetFriendPersonaName(id);
                if (persona != nullptr && persona[0] != '\0') {
                    name = persona;
                }
            }
            CopyAscii(nextNames[static_cast<std::size_t>(i)].data(), nameMax, name);
        }
        memberNames = nextNames;
        memberCount = count;
    }

    void OnLobbyCreated(LobbyCreated_t *result, bool ioFailure)
    {
        if (ioFailure || result == nullptr || result->m_eResult != k_EResultOK) {
            SetHint("Lobby create failed. Is Steam running?");
            return;
        }
        lobbyId = CSteamID(result->m_ulSteamIDLobby);
        inLobby = lobbyId.IsValid();
        if (inLobby && SteamMatchmaking() != nullptr) {
            SteamMatchmaking()->SetLobbyJoinable(lobbyId, true);
        }
        pendingRefresh = true;
        SetHint(hintHost);
    }

    STEAM_CALLBACK(SteamSession, OnLobbyEnter, LobbyEnter_t);
    STEAM_CALLBACK(SteamSession, OnLobbyChatUpdate, LobbyChatUpdate_t);
    STEAM_CALLBACK(SteamSession, OnJoinRequested, GameLobbyJoinRequested_t);

    CCallResult<SteamSession, LobbyCreated_t> lobbyCreated;
    CSteamID lobbyId;
    CSteamID pendingJoinId;
    bool inLobby = false;
    bool pendingJoin = false;
    bool pendingLeave = false;
    bool pendingRefresh = false;
    int memberCount = 0;
    std::array<std::array<char, nameMax>, hh::MAX_PLAYERS> memberNames{};
    std::array<char, statusMax> hint{};
};

void SteamSession::OnLobbyEnter(LobbyEnter_t *callback)
{
    if (callback == nullptr || callback->m_EChatRoomEnterResponse != k_EChatRoomEnterResponseSuccess) {
        SetHint("Could not enter lobby.");
        return;
    }
    lobbyId = CSteamID(callback->m_ulSteamIDLobby);
    inLobby = lobbyId.IsValid();
    pendingRefresh = true;
    SetHint(hintHost);
}

void SteamSession::OnLobbyChatUpdate(LobbyChatUpdate_t *callback)
{
    if (callback == nullptr) {
        return;
    }
    pendingRefresh = true;
}

void SteamSession::OnJoinRequested(GameLobbyJoinRequested_t *callback)
{
    if (callback == nullptr) {
        return;
    }
    QueueJoin(callback->m_steamIDLobby);
}

std::unique_ptr<SteamSession> g_session;

} // namespace

void SteamUseExeDirectory()
{
#ifdef _WIN32
    char path[MAX_PATH];
    if (GetModuleFileNameA(nullptr, path, MAX_PATH) == 0) {
        return;
    }
    char *slash = std::strrchr(path, '\\');
    if (slash != nullptr) {
        *slash = '\0';
        SetCurrentDirectoryA(path);
    }
#endif
}

void SteamPrepareLaunch()
{
#ifdef _WIN32
    SetEnvironmentVariableA("DISABLESTEAMOVERLAY", "1");
#endif
    SteamUseExeDirectory();
}

bool SteamInit()
{
#ifdef _WIN32
    SetEnvironmentVariableA("DISABLESTEAMOVERLAY", "1");
#endif

    g_ok = SteamAPI_Init();
    if (!g_ok) {
        SetStatus(offlineStatus);
        return false;
    }

    ISteamUser *user = SteamUser();
    ISteamFriends *friends = SteamFriends();
    if (user == nullptr || friends == nullptr ||
        !user->GetSteamID().BIndividualAccount()) {
        SetStatus(offlineStatus);
        SteamAPI_Shutdown();
        g_ok = false;
        return false;
    }

    const char *name = friends->GetPersonaName();
    if (name == nullptr || name[0] == '\0') {
        name = unknownName;
    }
    char line[statusMax];
    std::snprintf(line, sizeof(line), "Steam: %s", name);
    SetStatus(line);
    g_session = std::make_unique<SteamSession>();
    return true;
}

void SteamTick()
{
    if (!g_ok) {
        return;
    }
    SteamAPI_RunCallbacks();
    if (g_session != nullptr) {
        g_session->FlushPending();
    }
}

void SteamShutdown()
{
    if (g_session != nullptr) {
        g_session->Leave();
        g_session.reset();
    }
    if (!g_ok) {
        return;
    }
    SteamAPI_Shutdown();
    g_ok = false;
}

bool SteamLoggedIn()
{
    return g_ok;
}

const char *SteamStatusLine()
{
    if (g_status[0] == '\0') {
        return offlineStatus;
    }
    return g_status.data();
}

void SteamCreateLobby()
{
    if (g_session == nullptr) {
        return;
    }
    std::cout << "pre create firends looby \n";
    g_session->CreateFriendsLobby();
    std::cout << "session created";
}

bool SteamJoinFriendLobby()
{
    if (g_session == nullptr) {
        return false;
    }
    return g_session->JoinFriendLobby();
}

bool SteamInLobby()
{
    return g_session != nullptr && g_session->InLobby();
}

bool SteamIsLobbyOwner()
{
    return g_session != nullptr && g_session->IsOwner();
}

std::uint64_t SteamLobbyOwnerID64()
{
    if (g_session == nullptr) {
        return 0;
    }
    return g_session->OwnerSteamID64();
}

int SteamLobbyMemberCount()
{
    if (g_session == nullptr) {
        return 0;
    }
    return g_session->MemberCount();
}

const char *SteamLobbyMemberName(int index)
{
    if (g_session == nullptr) {
        return unknownName;
    }
    return g_session->MemberName(index);
}

const char *SteamLobbyHint()
{
    if (!g_ok) {
        return hintOffline;
    }
    if (g_session == nullptr) {
        return hintCreate;
    }
    const char *hint = g_session->Hint();
    if (hint[0] == '\0') {
        return hintCreate;
    }
    return hint;
}
