/* Heart House — entry. Owns the window and main loop until phase switching lives here. */
#include "core/game_constants.hpp"
#include "raylib.h"
#include "steam/steam_client.hpp"

int main()
{
    constexpr Color clearColor{32, 30, 28, 255};
    constexpr int padX = 40;
    constexpr int titleY = 40;
    constexpr int hintY = 88;
    constexpr int steamY = 136;
    constexpr int lobbyHintY = 172;
    constexpr int membersY = 216;
    constexpr int memberLine = 28;
    constexpr int titleSize = 32;
    constexpr int bodySize = 20;

    SteamUseExeDirectory();
    SetConfigFlags(FLAG_VSYNC_HINT);
    InitWindow(hh::WINDOW_WIDTH, hh::WINDOW_HEIGHT, "Heart House");
    SetTargetFPS(hh::TARGET_FPS);

    SteamInit();

    while (!WindowShouldClose()) {
        SteamTick();

        if (SteamLoggedIn()) {
            if (IsKeyPressed(KEY_C)) {
                SteamCreateLobby();
            }
            if (IsKeyPressed(KEY_J)) {
                SteamJoinFriendLobby();
            }
        }

        BeginDrawing();
        ClearBackground(clearColor);
        DrawText("Heart House", padX, titleY, titleSize, RAYWHITE);
        DrawText("C = host lobby. J = join friend.", padX, hintY, bodySize, GRAY);
        DrawText(SteamStatusLine(), padX, steamY, bodySize, RAYWHITE);
        DrawText(SteamLobbyHint(), padX, lobbyHintY, bodySize, GRAY);

        const int memberCount = SteamLobbyMemberCount();
        if (SteamInLobby()) {
            DrawText(TextFormat("In lobby (%d/%d):", memberCount, hh::MAX_PLAYERS), padX, membersY,
                bodySize, RAYWHITE);
            for (int i = 0; i < memberCount; ++i) {
                DrawText(SteamLobbyMemberName(i), padX + 24, membersY + memberLine * (i + 1),
                    bodySize, RAYWHITE);
            }
        }
        EndDrawing();
    }

    SteamShutdown();
    CloseWindow();
    return 0;
}
