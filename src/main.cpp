/* Heart House — entry. Owns the window and main loop until phase switching lives here. */
#include "core/game_constants.h"
#include "raylib.h"

int main()
{
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Heart House");
    SetTargetFPS(60);

    while (!WindowShouldClose()) {
        BeginDrawing();
        ClearBackground(Color{32, 30, 28, 255});
        DrawText("Heart House", 40, 40, 32, RAYWHITE);
        DrawText("Build with CMake, then playtest. Close the window to quit.", 40, 88, 20, GRAY);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
