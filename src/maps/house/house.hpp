#pragma once

/* House graybox + in-game map builder.
 * Area: maps/house. Players still get one saved house; traps come later. */

#include <raylib.h>

namespace House {

enum class PieceKind {
    Wall,
    Floor
};

void Load();
void Save();
void Draw();
void DrawBuilderPreview(const Camera3D& camera);

[[nodiscard]] Vector3 MovePlayer(const Vector3& eyePosition, const Vector3& worldDelta);
[[nodiscard]] Vector3 SpawnPosition(const bool host, const float eyeHeight);

void SetKind(const PieceKind kind);
void RotatePiece();
bool PlaceFromCamera(const Camera3D& camera);
bool DeleteFromCamera(const Camera3D& camera);
[[nodiscard]] const char* BuilderHint();

} // namespace House
