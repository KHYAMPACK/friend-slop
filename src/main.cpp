/*
 * Heart House - minimal two-player Steam lobby example.
 *
 * This file intentionally uses:
 *   - no custom classes
 *   - no STEAM_CALLBACK macro
 *   - no CCallResult object
 *
 * Steam callbacks are read manually in SteamTick(), which keeps the entire
 * example procedural: global state plus free functions.
 */

#include "core/game_constants.hpp"
#include "raylib.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
#include "steam/steam_api.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace {

// A lobby created with this limit can contain only the host and one friend.
constexpr int MAX_LOBBY_MEMBERS = 2;

// All Steam state is kept here. These are plain global variables, not a class.
bool g_steamReady = false;
HSteamPipe g_steamPipe = 0;
uint64 g_lobbyId = 0;
SteamAPICall_t g_createLobbyCall = k_uAPICallInvalid;
SteamAPICall_t g_joinLobbyCall = k_uAPICallInvalid;
bool g_joinPending = false;
char g_status[256] = "Steam has not been initialized.";

// Writes a formatted message into the text shown in the window.
void SetStatus(const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(g_status, sizeof(g_status), format, arguments);
    va_end(arguments);
}

// Converts the stored 64-bit lobby ID into Steamworks' CSteamID type.
CSteamID CurrentLobby()
{
    return CSteamID(g_lobbyId);
}

// Returns how many people are currently in our lobby.
int LobbyMemberCount()
{
    if (!g_steamReady || g_lobbyId == 0 || SteamMatchmaking() == nullptr) {
        return 0;
    }

    return SteamMatchmaking()->GetNumLobbyMembers(CurrentLobby());
}

// Returns a lobby member's current Steam display name.
const char *LobbyMemberName(int index)
{
    if (!g_steamReady || g_lobbyId == 0 || SteamMatchmaking() == nullptr ||
        SteamFriends() == nullptr || SteamUser() == nullptr) {
        return "(unknown)";
    }

    const CSteamID member =
        SteamMatchmaking()->GetLobbyMemberByIndex(CurrentLobby(), index);

    if (!member.IsValid()) {
        return "(unknown)";
    }

    // GetPersonaName is the most direct way to obtain our own name.
    if (member == SteamUser()->GetSteamID()) {
        return SteamFriends()->GetPersonaName();
    }

    return SteamFriends()->GetFriendPersonaName(member);
}

// Updates the status whenever somebody joins or leaves the current lobby.
void UpdateLobbyStatus()
{
    const int members = LobbyMemberCount();

    if (members >= MAX_LOBBY_MEMBERS) {
        SetStatus("Connected: both players are in the lobby.");
    } else {
        SetStatus("Lobby ready: %d/%d players. Waiting for your friend...",
                  members, MAX_LOBBY_MEMBERS);
    }
}

// Leaves the current lobby, if there is one.
void LeaveLobby()
{
    if (g_steamReady && g_lobbyId != 0 && SteamMatchmaking() != nullptr) {
        SteamMatchmaking()->LeaveLobby(CurrentLobby());
    }

    g_lobbyId = 0;
    g_joinPending = false;
}

// Starts an asynchronous request to enter a particular lobby.
void JoinLobby(CSteamID lobby)
{
    if (!g_steamReady || !lobby.IsValid() || SteamMatchmaking() == nullptr) {
        SetStatus("Cannot join that lobby.");
        return;
    }

    // Do not start a second operation while a lobby is still being created.
    if (g_createLobbyCall != k_uAPICallInvalid ||
        g_joinLobbyCall != k_uAPICallInvalid) {
        SetStatus("Wait for the current lobby operation to finish.");
        return;
    }

    if (g_lobbyId == lobby.ConvertToUint64()) {
        SetStatus("You are already in that lobby.");
        return;
    }

    LeaveLobby();
    g_joinPending = true;
    g_joinLobbyCall = SteamMatchmaking()->JoinLobby(lobby);

    if (g_joinLobbyCall == k_uAPICallInvalid) {
        g_joinPending = false;
        SetStatus("Steam refused to start the join request.");
        return;
    }

    SetStatus("Joining lobby...");
}

// Creates a friends-only lobby with exactly two available places.
void CreateTwoPlayerLobby()
{
    if (!g_steamReady || SteamMatchmaking() == nullptr) {
        SetStatus("Steam is not ready. Open Steam and restart the game.");
        return;
    }

    if (g_createLobbyCall != k_uAPICallInvalid ||
        g_joinLobbyCall != k_uAPICallInvalid || g_joinPending) {
        SetStatus("A lobby operation is already running.");
        return;
    }

    LeaveLobby();

    // CreateLobby is asynchronous. Its result is handled later by SteamTick().
    g_createLobbyCall = SteamMatchmaking()->CreateLobby(
        k_ELobbyTypeFriendsOnly,
        MAX_LOBBY_MEMBERS
    );

    if (g_createLobbyCall == k_uAPICallInvalid) {
        SetStatus("Steam refused to start the lobby request.");
        return;
    }

    SetStatus("Creating a two-player lobby...");
}

// Finds the first Steam friend who is running this App ID inside a lobby.
// The host presses C first; the other player can then press J.
void JoinFirstFriendLobby()
{
    if (!g_steamReady || SteamFriends() == nullptr || SteamUtils() == nullptr) {
        SetStatus("Steam is not ready.");
        return;
    }

    if (g_createLobbyCall != k_uAPICallInvalid ||
        g_joinLobbyCall != k_uAPICallInvalid || g_joinPending) {
        SetStatus("A lobby operation is already running.");
        return;
    }

    const AppId_t ourAppId = SteamUtils()->GetAppID();
    const int friendCount = SteamFriends()->GetFriendCount(k_EFriendFlagImmediate);

    for (int index = 0; index < friendCount; ++index) {
        const CSteamID friendId =
            SteamFriends()->GetFriendByIndex(index, k_EFriendFlagImmediate);

        FriendGameInfo_t gameInfo{};
        if (!friendId.IsValid() ||
            !SteamFriends()->GetFriendGamePlayed(friendId, &gameInfo)) {
            continue;
        }

        // Ignore friends playing another game or not currently in a lobby.
        if (gameInfo.m_gameID.AppID() != ourAppId ||
            !gameInfo.m_steamIDLobby.IsValid()) {
            continue;
        }

        JoinLobby(gameInfo.m_steamIDLobby);
        return;
    }

    SetStatus("No friend's lobby found. The host must press C first.");
}

// Handles the result of our asynchronous CreateLobby call.
void HandleCreateLobbyResult(const SteamAPICallCompleted_t &completed)
{
    if (completed.m_hAsyncCall != g_createLobbyCall) {
        return;
    }

    LobbyCreated_t result{};
    bool ioFailure = false;

    const bool received = SteamAPI_ManualDispatch_GetAPICallResult(
        g_steamPipe,
        completed.m_hAsyncCall,
        &result,
        sizeof(result),
        LobbyCreated_t::k_iCallback,
        &ioFailure
    );

    g_createLobbyCall = k_uAPICallInvalid;

    if (!received || ioFailure || result.m_eResult != k_EResultOK) {
        SetStatus("Lobby creation failed. Steam result: %d",
                  received ? static_cast<int>(result.m_eResult) : -1);
        return;
    }

    // CreateLobby automatically puts the creator inside the new lobby.
    g_lobbyId = result.m_ulSteamIDLobby;
    SteamMatchmaking()->SetLobbyJoinable(CurrentLobby(), true);
    UpdateLobbyStatus();
}

// Forward declaration because the join call-result handler uses this function.
void HandleLobbyEnter(const LobbyEnter_t &event);

// Handles the asynchronous call result returned specifically by JoinLobby.
void HandleJoinLobbyResult(const SteamAPICallCompleted_t &completed)
{
    if (completed.m_hAsyncCall != g_joinLobbyCall) {
        return;
    }

    LobbyEnter_t result{};
    bool ioFailure = false;

    const bool received = SteamAPI_ManualDispatch_GetAPICallResult(
        g_steamPipe,
        completed.m_hAsyncCall,
        &result,
        sizeof(result),
        LobbyEnter_t::k_iCallback,
        &ioFailure
    );

    g_joinLobbyCall = k_uAPICallInvalid;

    if (!received || ioFailure) {
        g_joinPending = false;
        g_lobbyId = 0;
        SetStatus("Steam could not finish the join request.");
        return;
    }

    HandleLobbyEnter(result);
}

// Handles the callback fired after CreateLobby or JoinLobby enters a lobby.
void HandleLobbyEnter(const LobbyEnter_t &event)
{
    g_joinPending = false;

    if (event.m_EChatRoomEnterResponse != k_EChatRoomEnterResponseSuccess) {
        g_lobbyId = 0;
        SetStatus("Could not enter lobby. Steam response: %u",
                  event.m_EChatRoomEnterResponse);
        return;
    }

    g_lobbyId = event.m_ulSteamIDLobby;
    UpdateLobbyStatus();
}

// Handles a Steam invite or "Join Game" click while the game is already open.
void HandleJoinRequested(const GameLobbyJoinRequested_t &event)
{
    JoinLobby(event.m_steamIDLobby);
}

// Pulls every pending Steam callback and sends it to the correct free function.
void SteamTick()
{
    if (!g_steamReady) {
        return;
    }

    SteamAPI_ManualDispatch_RunFrame(g_steamPipe);

    CallbackMsg_t callback{};
    while (SteamAPI_ManualDispatch_GetNextCallback(g_steamPipe, &callback)) {
        if (callback.m_iCallback == SteamAPICallCompleted_t::k_iCallback &&
            callback.m_cubParam >= sizeof(SteamAPICallCompleted_t)) {
            SteamAPICallCompleted_t event{};
            std::memcpy(&event, callback.m_pubParam, sizeof(event));

            if (event.m_hAsyncCall == g_createLobbyCall) {
                HandleCreateLobbyResult(event);
            } else if (event.m_hAsyncCall == g_joinLobbyCall) {
                HandleJoinLobbyResult(event);
            }
        } else if (callback.m_iCallback == LobbyEnter_t::k_iCallback &&
                   callback.m_cubParam >= sizeof(LobbyEnter_t)) {
            LobbyEnter_t event{};
            std::memcpy(&event, callback.m_pubParam, sizeof(event));
            HandleLobbyEnter(event);
        } else if (callback.m_iCallback == LobbyChatUpdate_t::k_iCallback &&
                   callback.m_cubParam >= sizeof(LobbyChatUpdate_t)) {
            LobbyChatUpdate_t event{};
            std::memcpy(&event, callback.m_pubParam, sizeof(event));

            if (event.m_ulSteamIDLobby == g_lobbyId) {
                UpdateLobbyStatus();
            }
        } else if (callback.m_iCallback == GameLobbyJoinRequested_t::k_iCallback &&
                   callback.m_cubParam >= sizeof(GameLobbyJoinRequested_t)) {
            GameLobbyJoinRequested_t event{};
            std::memcpy(&event, callback.m_pubParam, sizeof(event));
            HandleJoinRequested(event);
        }

        // Steam requires this exactly once after every successful GetNextCallback.
        SteamAPI_ManualDispatch_FreeLastCallback(g_steamPipe);
    }

    // Manual dispatch does not call SteamAPI_RunCallbacks, so explicitly release
    // any temporary Steam API memory owned by this thread.
    SteamAPI_ReleaseCurrentThreadMemory();
}

// Starts Steamworks and enables the manual callback system used above.
bool SteamInit()
{
    SteamErrMsg errorMessage{};
    const ESteamAPIInitResult result = SteamAPI_InitEx(&errorMessage);

    if (result != k_ESteamAPIInitResult_OK) {
        SetStatus("Steam initialization failed: %s", errorMessage);
        return false;
    }

    SteamAPI_ManualDispatch_Init();
    g_steamPipe = SteamAPI_GetHSteamPipe();

    if (g_steamPipe == 0 || SteamFriends() == nullptr ||
        SteamMatchmaking() == nullptr || SteamUser() == nullptr ||
        SteamUtils() == nullptr) {
        SetStatus("Steam initialized, but a required Steam interface is missing.");
        SteamAPI_Shutdown();
        g_steamPipe = 0;
        return false;
    }

    g_steamReady = true;
    SetStatus("Steam: %s. Press C to host or J to join.",
              SteamFriends()->GetPersonaName());
    return true;
}

// Cleans up the lobby and Steamworks when the program closes.
void SteamShutdown()
{
    if (!g_steamReady) {
        return;
    }

    LeaveLobby();
    SteamAPI_Shutdown();
    g_steamReady = false;
    g_steamPipe = 0;
}

// Steam launches a closed game with: +connect_lobby <64-bit lobby ID>.
// This handles that command line so Steam invitations also work from launch.
void JoinLobbyFromCommandLine(int argc, char **argv)
{
    if (!g_steamReady) {
        return;
    }

    for (int index = 1; index + 1 < argc; ++index) {
        if (std::strcmp(argv[index], "+connect_lobby") != 0) {
            continue;
        }

        const unsigned long long parsed = std::strtoull(argv[index + 1], nullptr, 10);
        if (parsed != 0) {
            JoinLobby(CSteamID(static_cast<uint64>(parsed)));
        }
        return;
    }
}

} // namespace

int main(int argc, char **argv)
{
    constexpr Color clearColor{32, 30, 28, 255};
    constexpr int padX = 40;
    constexpr int titleY = 40;
    constexpr int bodySize = 20;

    // Initialize Steam before creating the graphics window.
    SteamInit();

    SetConfigFlags(FLAG_VSYNC_HINT);
    InitWindow(hh::WINDOW_WIDTH, hh::WINDOW_HEIGHT, "Heart House");
    SetTargetFPS(hh::TARGET_FPS);

    // If Steam launched us from an invitation, join its lobby immediately.
    JoinLobbyFromCommandLine(argc, argv);

    while (!WindowShouldClose()) {
        // Steam callbacks must be pumped every frame.
        SteamTick();

        if (g_steamReady && IsKeyPressed(KEY_C)) {
            CreateTwoPlayerLobby();
        }
        if (g_steamReady && IsKeyPressed(KEY_J)) {
            JoinFirstFriendLobby();
        }
        if (g_steamReady && IsKeyPressed(KEY_L)) {
            LeaveLobby();
            SetStatus("Left the lobby. Press C to host or J to join.");
        }

        BeginDrawing();
        ClearBackground(clearColor);

        DrawText("Heart House", padX, titleY, 32, RAYWHITE);
        DrawText("C = host   J = join friend   L = leave",
                 padX, 88, bodySize, GRAY);
        DrawText(g_status, padX, 128, bodySize, RAYWHITE);

        if (g_lobbyId != 0) {
            const int memberCount = LobbyMemberCount();

            DrawText(TextFormat("Lobby ID: %llu",
                                static_cast<unsigned long long>(g_lobbyId)),
                     padX, 168, bodySize, GRAY);
            DrawText(TextFormat("Members: %d/%d",
                                memberCount, MAX_LOBBY_MEMBERS),
                     padX, 204, bodySize, RAYWHITE);

            for (int index = 0; index < memberCount; ++index) {
                DrawText(LobbyMemberName(index),
                         padX + 24,
                         240 + index * 30,
                         bodySize,
                         RAYWHITE);
            }
        }

        EndDrawing();
    }

    SteamShutdown();
    CloseWindow();
    return 0;
}