/* SteamAPI init / tick / shutdown, friends lobby for seeing names.
 * Area: steam. No movement sync, no overlay (overlay + OpenGL 0xC0000005 on some PCs).
 */

#include "steam/steam_client.hpp"

#include "core/game_constants.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>

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

// #region agent log
void AgentLog(const char *hypothesisId, const char *location, const char *message, const char *dataJson)
{
#ifdef _WIN32
    const unsigned long long ts = GetTickCount64();
#else
    const unsigned long long ts = 0;
#endif
    char line[1024];
    std::snprintf(line, sizeof(line),
        "{\"sessionId\":\"0efb11\",\"runId\":\"run1\",\"hypothesisId\":\"%s\","
        "\"location\":\"%s\",\"message\":\"%s\",\"data\":%s,\"timestamp\":%llu}\n",
        hypothesisId, location, message, dataJson, ts);
    const char *paths[] = {
        "c:\\Users\\Mert\\OneDrive\\Desktop\\Projects\\hh\\debug-0efb11.log",
        "debug-0efb11.log",
    };
    for (const char *path : paths) {
        FILE *file = nullptr;
#ifdef _MSC_VER
        if (fopen_s(&file, path, "a") != 0 || file == nullptr) {
            continue;
        }
#else
        file = std::fopen(path, "a");
        if (file == nullptr) {
            continue;
        }
#endif
        std::fputs(line, file);
        std::fclose(file);
    }
}
// #endregion

class SteamSession {
public:
    void CreateFriendsLobby()
    {
        ISteamMatchmaking *matchmaking = SteamMatchmaking();
        // #region agent log
        {
            char data[192];
            std::snprintf(data, sizeof(data),
                "{\"matchmaking\":%llu,\"inLobby\":%s}",
                static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(matchmaking)),
                inLobby ? "true" : "false");
            AgentLog("D", "steam_client.cpp:CreateFriendsLobby", "create lobby enter", data);
        }
        // #endregion
        if (matchmaking == nullptr) {
            return;
        }
        if (inLobby && lobbyId.IsValid()) {
            matchmaking->LeaveLobby(lobbyId);
            inLobby = false;
            memberCount = 0;
            lobbyId.Clear();
        }
        const SteamAPICall_t call =
            matchmaking->CreateLobby(k_ELobbyTypeFriendsOnly, hh::MAX_PLAYERS);
        lobbyCreated.Set(call, this, &SteamSession::OnLobbyCreated);
        // #region agent log
        {
            char data[128];
            std::snprintf(data, sizeof(data), "{\"call\":%llu}",
                static_cast<unsigned long long>(call));
            AgentLog("D", "steam_client.cpp:CreateFriendsLobby", "create lobby issued", data);
        }
        // #endregion
        SetHint(hintHost);
    }

    void JoinFriendLobby()
    {
        ISteamFriends *friends = SteamFriends();
        if (friends == nullptr) {
            SetHint(hintNoLobby);
            return;
        }

        uint32 appId = 480;
        ISteamUtils *utils = SteamUtils();
        // #region agent log
        {
            char data[192];
            std::snprintf(data, sizeof(data), "{\"friends\":%llu,\"utils\":%llu}",
                static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(friends)),
                static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(utils)));
            AgentLog("A", "steam_client.cpp:JoinFriendLobby", "before GetAppID", data);
        }
        // #endregion
        if (utils != nullptr) {
            appId = utils->GetAppID();
        }
        // #region agent log
        {
            char data[96];
            std::snprintf(data, sizeof(data), "{\"appId\":%u}", static_cast<unsigned>(appId));
            AgentLog("A", "steam_client.cpp:JoinFriendLobby", "after GetAppID", data);
        }
        // #endregion

        int friendCount = friends->GetFriendCount(k_EFriendFlagImmediate);
        if (friendCount < 0) {
            friendCount = 0;
        }
        // #region agent log
        {
            char data[96];
            std::snprintf(data, sizeof(data), "{\"friendCount\":%d}", friendCount);
            AgentLog("C", "steam_client.cpp:JoinFriendLobby", "friend count", data);
        }
        // #endregion
        for (int i = 0; i < friendCount; ++i) {
            const CSteamID friendId = friends->GetFriendByIndex(i, k_EFriendFlagImmediate);
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
            // #region agent log
            {
                char data[96];
                std::snprintf(data, sizeof(data), "{\"i\":%d,\"lobbyValid\":true}", i);
                AgentLog("B", "steam_client.cpp:JoinFriendLobby", "before CGameID::AppID", data);
            }
            // #endregion
            const uint32 friendApp = gameInfo.m_gameID.AppID();
            // #region agent log
            {
                char data[192];
                std::snprintf(data, sizeof(data),
                    "{\"i\":%d,\"friendApp\":%u,\"wantApp\":%u,\"lobbyValid\":%s}", i,
                    static_cast<unsigned>(friendApp), static_cast<unsigned>(appId),
                    gameInfo.m_steamIDLobby.IsValid() ? "true" : "false");
                AgentLog("B", "steam_client.cpp:JoinFriendLobby", "friend game info", data);
            }
            // #endregion
            if (friendApp != appId) {
                continue;
            }
            QueueJoin(gameInfo.m_steamIDLobby);
            SetHint("Joining friend's lobby...");
            return;
        }
        SetHint(hintNoLobby);
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
        // #region agent log
        {
            char data[192];
            std::snprintf(data, sizeof(data), "{\"ioFailure\":%s,\"resultPtr\":%s,\"eResult\":%d}",
                ioFailure ? "true" : "false", result != nullptr ? "true" : "false",
                result != nullptr ? static_cast<int>(result->m_eResult) : -1);
            AgentLog("D", "steam_client.cpp:OnLobbyCreated", "lobby created callback", data);
        }
        // #endregion
        if (ioFailure || result == nullptr || result->m_eResult != k_EResultOK) {
            SetHint("Lobby create failed. Is Steam running?");
            return;
        }
        lobbyId = CSteamID(result->m_ulSteamIDLobby);
        inLobby = lobbyId.IsValid();
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
    // #region agent log
    {
        char data[128];
        std::snprintf(data, sizeof(data), "{\"callback\":%s,\"response\":%d}",
            callback != nullptr ? "true" : "false",
            callback != nullptr ? static_cast<int>(callback->m_EChatRoomEnterResponse) : -1);
        AgentLog("D", "steam_client.cpp:OnLobbyEnter", "lobby enter callback", data);
    }
    // #endregion
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

bool SteamInit()
{
#ifdef _WIN32
    SetEnvironmentVariableA("DISABLESTEAMOVERLAY", "1");
#endif

    g_ok = SteamAPI_Init();
    ISteamUtils *utilsAtInit = g_ok ? SteamUtils() : nullptr;
    // #region agent log
    {
        char data[192];
        std::snprintf(data, sizeof(data), "{\"g_ok\":%s,\"utils\":%llu}", g_ok ? "true" : "false",
            static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(utilsAtInit)));
        AgentLog("E", "steam_client.cpp:SteamInit", "steam init", data);
    }
    // #endregion
    if (!g_ok) {
        SetStatus(offlineStatus);
        return false;
    }

    ISteamFriends *friends = SteamFriends();
    if (friends == nullptr) {
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
    g_session->CreateFriendsLobby();
}

void SteamJoinFriendLobby()
{
    if (g_session == nullptr) {
        return;
    }
    g_session->JoinFriendLobby();
}

bool SteamInLobby()
{
    return g_session != nullptr && g_session->InLobby();
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
