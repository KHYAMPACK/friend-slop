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

constexpr std::size_t STATUS_MAX = 256;
constexpr std::size_t MEMBER_NAME_MAX = 128;
constexpr char OFFLINE_STATUS[] = "Open Steam and relaunch";
constexpr char UNKNOWN_NAME[] = "(unknown)";
constexpr char HINT_CREATE[] = "You: C to host. Friend: J to join. You must be Steam friends.";
constexpr char HINT_HOST[] = "Lobby up. Friend presses J.";
constexpr char HINT_OFFLINE[] = "Open Steam and relaunch before creating a lobby.";
constexpr char HINT_NO_LOBBY[] = "No friend lobby yet. They press C first. You must be Steam friends.";
constexpr std::uint32_t SPACEWAR_APP_ID = 480;

bool steamOk = false;
std::array<char, STATUS_MAX> statusText{};

void CopyAscii(char* dst, const std::size_t dstSize, const char* src)
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

void SetStatus(const char* text)
{
    CopyAscii(statusText.data(), statusText.size(), text);
}

class SteamSession {
public:
    void CreateFriendsLobby()
    {
        ISteamMatchmaking* matchmaking = SteamMatchmaking();
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
        SetHint(HINT_HOST);
        std::cout << "post set hing \n";
    }

    bool JoinFriendLobby()
    {
        ISteamFriends* friends = SteamFriends();
        if (friends == nullptr) {
            SetHint(HINT_NO_LOBBY);
            return false;
        }

        std::uint32_t appId = SPACEWAR_APP_ID;
        ISteamUtils* utils = SteamUtils();
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
        SetHint(HINT_NO_LOBBY);
        return false;
    }

    void FlushPending()
    {
        ISteamMatchmaking* matchmaking = SteamMatchmaking();
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

    [[nodiscard]] const char* MemberName(const int index) const
    {
        if (index < 0 || index >= memberCount) {
            return UNKNOWN_NAME;
        }
        return memberNames[static_cast<std::size_t>(index)].data();
    }

    [[nodiscard]] const char* Hint() const
    {
        return hint.data();
    }

    [[nodiscard]] std::uint64_t OwnerSteamID64() const
    {
        ISteamMatchmaking* matchmaking = SteamMatchmaking();
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
        ISteamUser* user = SteamUser();
        if (user == nullptr) {
            return false;
        }
        const std::uint64_t owner = OwnerSteamID64();
        return owner != 0 && owner == user->GetSteamID().ConvertToUint64();
    }

    void Leave()
    {
        ISteamMatchmaking* matchmaking = SteamMatchmaking();
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
    void SetHint(const char* text)
    {
        CopyAscii(hint.data(), hint.size(), text);
    }

    void QueueJoin(const CSteamID& id)
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
        ISteamMatchmaking* matchmaking = SteamMatchmaking();
        ISteamFriends* friends = SteamFriends();
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

        std::array<std::array<char, MEMBER_NAME_MAX>, hh::MAX_PLAYERS> nextNames{};
        for (int i = 0; i < count; ++i) {
            const CSteamID id = matchmaking->GetLobbyMemberByIndex(lobbyId, i);
            const char* name = UNKNOWN_NAME;
            if (id.IsValid()) {
                const char* persona = friends->GetFriendPersonaName(id);
                if (persona != nullptr && persona[0] != '\0') {
                    name = persona;
                }
            }
            CopyAscii(nextNames[static_cast<std::size_t>(i)].data(), MEMBER_NAME_MAX, name);
        }
        memberNames = nextNames;
        memberCount = count;
    }

    void OnLobbyCreated(LobbyCreated_t* result, bool ioFailure)
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
        SetHint(HINT_HOST);
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
    std::array<std::array<char, MEMBER_NAME_MAX>, hh::MAX_PLAYERS> memberNames{};
    std::array<char, STATUS_MAX> hint{};
};

void SteamSession::OnLobbyEnter(LobbyEnter_t* callback)
{
    if (callback == nullptr || callback->m_EChatRoomEnterResponse != k_EChatRoomEnterResponseSuccess) {
        SetHint("Could not enter lobby.");
        return;
    }
    lobbyId = CSteamID(callback->m_ulSteamIDLobby);
    inLobby = lobbyId.IsValid();
    pendingRefresh = true;
    SetHint(HINT_HOST);
}

void SteamSession::OnLobbyChatUpdate(LobbyChatUpdate_t* callback)
{
    if (callback == nullptr) {
        return;
    }
    pendingRefresh = true;
}

void SteamSession::OnJoinRequested(GameLobbyJoinRequested_t* callback)
{
    if (callback == nullptr) {
        return;
    }
    QueueJoin(callback->m_steamIDLobby);
}

std::unique_ptr<SteamSession> session;

} // namespace

namespace HhSteam {

void UseExeDirectory()
{
#ifdef _WIN32
    char path[MAX_PATH];
    if (GetModuleFileNameA(nullptr, path, MAX_PATH) == 0) {
        return;
    }
    char* slash = std::strrchr(path, '\\');
    if (slash != nullptr) {
        *slash = '\0';
        SetCurrentDirectoryA(path);
    }
#endif
}

void PrepareLaunch()
{
#ifdef _WIN32
    SetEnvironmentVariableA("DISABLESTEAMOVERLAY", "1");
#endif
    UseExeDirectory();
}

bool Init()
{
#ifdef _WIN32
    SetEnvironmentVariableA("DISABLESTEAMOVERLAY", "1");
#endif

    steamOk = SteamAPI_Init();
    if (!steamOk) {
        SetStatus(OFFLINE_STATUS);
        return false;
    }

    ISteamUser* user = SteamUser();
    ISteamFriends* friends = SteamFriends();
    if (user == nullptr || friends == nullptr ||
        !user->GetSteamID().BIndividualAccount()) {
        SetStatus(OFFLINE_STATUS);
        SteamAPI_Shutdown();
        steamOk = false;
        return false;
    }

    const char* name = friends->GetPersonaName();
    if (name == nullptr || name[0] == '\0') {
        name = UNKNOWN_NAME;
    }
    char line[STATUS_MAX];
    std::snprintf(line, sizeof(line), "Steam: %s", name);
    SetStatus(line);
    session = std::make_unique<SteamSession>();
    return true;
}

void Tick()
{
    if (!steamOk) {
        return;
    }
    SteamAPI_RunCallbacks();
    if (session != nullptr) {
        session->FlushPending();
    }
}

void Shutdown()
{
    if (session != nullptr) {
        session->Leave();
        session.reset();
    }
    if (!steamOk) {
        return;
    }
    SteamAPI_Shutdown();
    steamOk = false;
}

bool LoggedIn()
{
    return steamOk;
}

const char* StatusLine()
{
    if (statusText[0] == '\0') {
        return OFFLINE_STATUS;
    }
    return statusText.data();
}

void CreateLobby()
{
    if (session == nullptr) {
        return;
    }
    std::cout << "pre create firends looby \n";
    session->CreateFriendsLobby();
    std::cout << "session created";
}

bool JoinFriendLobby()
{
    if (session == nullptr) {
        return false;
    }
    return session->JoinFriendLobby();
}

bool InLobby()
{
    return session != nullptr && session->InLobby();
}

bool IsLobbyOwner()
{
    return session != nullptr && session->IsOwner();
}

std::uint64_t LobbyOwnerID64()
{
    if (session == nullptr) {
        return 0;
    }
    return session->OwnerSteamID64();
}

int LobbyMemberCount()
{
    if (session == nullptr) {
        return 0;
    }
    return session->MemberCount();
}

const char* LobbyMemberName(const int index)
{
    if (session == nullptr) {
        return UNKNOWN_NAME;
    }
    return session->MemberName(index);
}

const char* LobbyHint()
{
    if (!steamOk) {
        return HINT_OFFLINE;
    }
    if (session == nullptr) {
        return HINT_CREATE;
    }
    const char* hint = session->Hint();
    if (hint[0] == '\0') {
        return HINT_CREATE;
    }
    return hint;
}

} // namespace HhSteam
