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
    constexpr int overlayY = 172;
    constexpr int titleSize = 32;
    constexpr int bodySize = 20;

    steam_init();

    InitWindow(hh::WINDOW_WIDTH, hh::WINDOW_HEIGHT, "Heart House");
    SetTargetFPS(hh::TARGET_FPS);

    while (!WindowShouldClose()) {
        steam_tick();

        BeginDrawing();
        ClearBackground(clearColor);
        DrawText("Heart House", padX, titleY, titleSize, RAYWHITE);
        DrawText("Build with CMake, then playtest. Close the window to quit.", padX, hintY, bodySize,
            GRAY);
        DrawText(steam_status_line(), padX, steamY, bodySize, RAYWHITE);
        DrawText("Shift+Tab for Steam overlay (Steam must be open).", padX, overlayY, bodySize,
            GRAY);
        EndDrawing();
    }

    CloseWindow();
    steam_shutdown();
    return 0;
}
