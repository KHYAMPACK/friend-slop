/* SteamAPI init / tick / shutdown. No netcode, no RestartAppIfNecessary.
 * Area: steam
 */

#include "steam/steam_client.hpp"

#include <array>
#include <cstdio>

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
constexpr char offlineStatus[] = "Open Steam and relaunch";
constexpr char unknownName[] = "(unknown)";

bool g_ok = false;
std::array<char, statusMax> g_status{};

void set_status(const char *text)
{
    std::snprintf(g_status.data(), g_status.size(), "%s", text);
}

} // namespace

bool steam_init()
{
    g_ok = SteamAPI_Init();
    if (!g_ok) {
        set_status(offlineStatus);
        return false;
    }

    ISteamFriends *friends = SteamFriends();
    if (friends == nullptr) {
        set_status(offlineStatus);
        SteamAPI_Shutdown();
        g_ok = false;
        return false;
    }

    const char *name = friends->GetPersonaName();
    if (name == nullptr || name[0] == '\0') {
        name = unknownName;
    }
    std::snprintf(g_status.data(), g_status.size(), "Steam: %s", name);
    return true;
}

void steam_tick()
{
    if (!g_ok) {
        return;
    }
    SteamAPI_RunCallbacks();
}

void steam_shutdown()
{
    if (!g_ok) {
        return;
    }
    SteamAPI_Shutdown();
    g_ok = false;
}

bool steam_logged_in()
{
    return g_ok;
}

const char *steam_status_line()
{
    if (g_status[0] == '\0') {
        return offlineStatus;
    }
    return g_status.data();
}
