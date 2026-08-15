/* SteamAPI init / tick / shutdown, friends lobby for seeing names.
 * Area: steam. No movement sync, no RestartAppIfNecessary.
 */

#include "steam/steam_client.hpp"

#include "core/game_constants.hpp"

#include <array>
#include <cstdio>
#include <memory>

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
constexpr char hintCreate[] = "Press C to create a lobby. Friend must already have this window open.";
constexpr char hintInvite[] = "Press I to invite. Friend accepts in Steam overlay. Both games must be running.";
constexpr char hintOffline[] = "Open Steam and relaunch before creating a lobby.";

bool g_ok = false;
std::array<char, statusMax> g_status{};

void SetStatus(const char *text)
{
    std::snprintf(g_status.data(), g_status.size(), "%s", text);
}

class SteamSession {
public:
    void CreateFriendsLobby()
    {
        ISteamMatchmaking *matchmaking = SteamMatchmaking();
        if (matchmaking == nullptr) {
            return;
        }
        if (inLobby && lobbyId.IsValid()) {
            matchmaking->LeaveLobby(lobbyId);
            inLobby = false;
            lobbyId.Clear();
        }
        const SteamAPICall_t call =
            matchmaking->CreateLobby(k_ELobbyTypeFriendsOnly, hh::MAX_PLAYERS);
        lobbyCreated.Set(call, this, &SteamSession::OnLobbyCreated);
        SetHint(hintInvite);
    }

    void OpenInviteDialog()
    {
        if (!inLobby || !lobbyId.IsValid()) {
            return;
        }
        ISteamFriends *friends = SteamFriends();
        if (friends == nullptr) {
            return;
        }
        friends->ActivateGameOverlayInviteDialog(lobbyId);
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
    }

private:
    void SetHint(const char *text)
    {
        std::snprintf(hint.data(), hint.size(), "%s", text);
    }

    void RefreshMembers()
    {
        memberCount = 0;
        ISteamMatchmaking *matchmaking = SteamMatchmaking();
        ISteamFriends *friends = SteamFriends();
        if (matchmaking == nullptr || friends == nullptr || !lobbyId.IsValid()) {
            return;
        }

        int count = matchmaking->GetNumLobbyMembers(lobbyId);
        if (count > hh::MAX_PLAYERS) {
            count = hh::MAX_PLAYERS;
        }
        for (int i = 0; i < count; ++i) {
            const CSteamID id = matchmaking->GetLobbyMemberByIndex(lobbyId, i);
            friends->RequestUserInformation(id, true);
            const char *name = friends->GetFriendPersonaName(id);
            if (name == nullptr || name[0] == '\0') {
                name = unknownName;
            }
            std::snprintf(memberNames[static_cast<std::size_t>(i)].data(), nameMax, "%s", name);
        }
        memberCount = count;
    }

    void OnLobbyCreated(LobbyCreated_t *result, bool ioFailure)
    {
        if (ioFailure || result == nullptr || result->m_eResult != k_EResultOK) {
            SetHint("Lobby create failed. Is Steam running?");
            return;
        }
        lobbyId = CSteamID(result->m_ulSteamIDLobby);
        inLobby = true;
        RefreshMembers();
        SetHint(hintInvite);
    }

    STEAM_CALLBACK(SteamSession, OnLobbyEnter, LobbyEnter_t);
    STEAM_CALLBACK(SteamSession, OnLobbyChatUpdate, LobbyChatUpdate_t);
    STEAM_CALLBACK(SteamSession, OnJoinRequested, GameLobbyJoinRequested_t);
    STEAM_CALLBACK(SteamSession, OnPersonaChange, PersonaStateChange_t);

    CCallResult<SteamSession, LobbyCreated_t> lobbyCreated;
    CSteamID lobbyId;
    bool inLobby = false;
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
    inLobby = true;
    RefreshMembers();
    SetHint(hintInvite);
}

void SteamSession::OnLobbyChatUpdate(LobbyChatUpdate_t *callback)
{
    if (callback == nullptr) {
        return;
    }
    RefreshMembers();
}

void SteamSession::OnJoinRequested(GameLobbyJoinRequested_t *callback)
{
    if (callback == nullptr) {
        return;
    }
    ISteamMatchmaking *matchmaking = SteamMatchmaking();
    if (matchmaking == nullptr) {
        return;
    }
    if (inLobby && lobbyId.IsValid()) {
        matchmaking->LeaveLobby(lobbyId);
        inLobby = false;
    }
    matchmaking->JoinLobby(callback->m_steamIDLobby);
}

void SteamSession::OnPersonaChange(PersonaStateChange_t *callback)
{
    (void)callback;
    if (inLobby) {
        RefreshMembers();
    }
}

std::unique_ptr<SteamSession> g_session;

} // namespace

bool SteamInit()
{
    g_ok = SteamAPI_Init();
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
    std::snprintf(g_status.data(), g_status.size(), "Steam: %s", name);
    g_session = std::make_unique<SteamSession>();
    return true;
}

void SteamTick()
{
    if (!g_ok) {
        return;
    }
    SteamAPI_RunCallbacks();
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

void SteamOpenInvite()
{
    if (g_session == nullptr) {
        return;
    }
    g_session->OpenInviteDialog();
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
