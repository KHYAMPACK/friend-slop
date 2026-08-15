/* House graybox + snap map builder. Geometry lives in assets/maps/house.txt.
 * Area: maps/house. */

#include "maps/house/house.hpp"

#include "core/game_constants.hpp"

#include <raylib.h>
#include <raymath.h>

#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace {

constexpr float WALL_THICKNESS = 0.3f;
constexpr float WALL_HEIGHT = 3.0f;
constexpr float WALL_LENGTH = 2.0f * hh::BUILD_SNAP_M;
constexpr float FLOOR_THICKNESS = 0.1f;
constexpr float PLAYER_HALF_WIDTH = 0.28f;
constexpr float PLAYER_HEIGHT = 1.8f;
constexpr int MAX_HOUSE_PIECES = 256;
constexpr float SAME_PIECE_M = 0.05f;
constexpr float BUILD_PLANE_M = 400.0f;
constexpr int BUILD_GRID_SLICES = 200;
constexpr float BUILD_GRID_SPACING = 2.0f;
constexpr float BUILDER_RAY_M = 1000.0f;

const Color GRASS_COLOR{70, 140, 70, 255};
const Color FLOOR_COLOR{140, 110, 80, 255};
const Color WALL_COLOR{90, 95, 110, 255};
const Color GHOST_OK_COLOR{255, 220, 80, 255};
const Color GHOST_BAD_COLOR{220, 70, 70, 255};

#ifdef HH_HOUSE_FILE
constexpr const char* HOUSE_FILE = HH_HOUSE_FILE;
#else
constexpr const char* HOUSE_FILE = "assets/maps/house.txt";
#endif

struct SolidBox {
    House::PieceKind kind = House::PieceKind::Wall;
    Vector3 center{};
    Vector3 size{};
    Color color = WALL_COLOR;
};

std::vector<SolidBox> pieces;
House::PieceKind currentKind = House::PieceKind::Wall;
bool wallAlongX = true;
char builderHint[160] = "B - map builder";

[[nodiscard]] float Snap(const float value, const float grid)
{
    return std::round(value / grid) * grid;
}

[[nodiscard]] BoundingBox BoxBounds(const SolidBox& box)
{
    const Vector3 half = Vector3Scale(box.size, 0.5f);
    return {Vector3Subtract(box.center, half), Vector3Add(box.center, half)};
}

[[nodiscard]] bool SamePiece(const SolidBox& a, const SolidBox& b)
{
    return Vector3Distance(a.center, b.center) < SAME_PIECE_M &&
           Vector3Distance(a.size, b.size) < SAME_PIECE_M;
}

[[nodiscard]] bool Occupied(const SolidBox& candidate)
{
    for (const SolidBox& piece : pieces) {
        if (SamePiece(piece, candidate)) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] SolidBox MakeWall(const Vector3& snapped)
{
    SolidBox box;
    box.kind = House::PieceKind::Wall;
    box.color = WALL_COLOR;
    box.center = {snapped.x, WALL_HEIGHT * 0.5f, snapped.z};
    if (wallAlongX) {
        box.size = {WALL_LENGTH, WALL_HEIGHT, WALL_THICKNESS};
    } else {
        box.size = {WALL_THICKNESS, WALL_HEIGHT, WALL_LENGTH};
    }
    return box;
}

[[nodiscard]] SolidBox MakeFloor(const Vector3& snapped)
{
    SolidBox box;
    box.kind = House::PieceKind::Floor;
    box.color = FLOOR_COLOR;
    box.center = {snapped.x, -FLOOR_THICKNESS * 0.5f, snapped.z};
    box.size = {hh::BUILD_SNAP_M, FLOOR_THICKNESS, hh::BUILD_SNAP_M};
    return box;
}

[[nodiscard]] Ray AimRay(const Camera3D& camera)
{
    return GetScreenToWorldRay(GetMousePosition(), camera);
}

[[nodiscard]] bool GroundHit(const Camera3D& camera, Vector3& hit)
{
    const Ray ray = AimRay(camera);
    if (std::fabs(ray.direction.y) < 0.001f) {
        return false;
    }

    const float t = -ray.position.y / ray.direction.y;
    if (t < 0.0f || t > BUILDER_RAY_M) {
        return false;
    }

    hit = Vector3Add(ray.position, Vector3Scale(ray.direction, t));
    return true;
}

[[nodiscard]] bool PreviewBox(const Camera3D& camera, SolidBox& box)
{
    Vector3 hit{};
    if (!GroundHit(camera, hit)) {
        return false;
    }

    const Vector3 snapped = {
        Snap(hit.x, hh::BUILD_SNAP_M),
        0.0f,
        Snap(hit.z, hh::BUILD_SNAP_M)};

    if (currentKind == House::PieceKind::Floor) {
        box = MakeFloor(snapped);
    } else {
        box = MakeWall(snapped);
    }
    return true;
}

void DrawSolid(const SolidBox& box)
{
    DrawCubeV(box.center, box.size, box.color);
}

void RefreshHint()
{
    const char* kindName = currentKind == House::PieceKind::Floor ? "FLOOR" : "WALL";
    const char* axisName = wallAlongX ? "along X" : "along Z";
    if (currentKind == House::PieceKind::Floor) {
        std::snprintf(
            builderHint,
            sizeof(builderHint),
            "%s | %d pieces | LMB place RMB del 1/2 type T top-down ESC",
            kindName,
            static_cast<int>(pieces.size()));
    } else {
        std::snprintf(
            builderHint,
            sizeof(builderHint),
            "%s %s | %d pieces | LMB place RMB del R rotate 1/2 T top-down ESC",
            kindName,
            axisName,
            static_cast<int>(pieces.size()));
    }
}

[[nodiscard]] bool OverlapsWalls(const Vector3& eyePosition)
{
    const BoundingBox player{
        {eyePosition.x - PLAYER_HALF_WIDTH, 0.0f, eyePosition.z - PLAYER_HALF_WIDTH},
        {eyePosition.x + PLAYER_HALF_WIDTH, PLAYER_HEIGHT, eyePosition.z + PLAYER_HALF_WIDTH}};

    for (const SolidBox& piece : pieces) {
        if (piece.kind != House::PieceKind::Wall) {
            continue;
        }
        if (CheckCollisionBoxes(player, BoxBounds(piece))) {
            return true;
        }
    }
    return false;
}

} // namespace

namespace House {

void Load()
{
    pieces.clear();
    std::ifstream file(HOUSE_FILE);
    if (!file) {
        RefreshHint();
        return;
    }

    std::string tag;
    int version = 0;
    file >> tag >> version;
    if (tag != "hhhouse" || version != 1) {
        RefreshHint();
        return;
    }

    std::string kindName;
    while (file >> kindName) {
        SolidBox box;
        file >> box.center.x >> box.center.y >> box.center.z >> box.size.x >> box.size.y >>
            box.size.z;
        if (!file) {
            break;
        }
        if (kindName == "floor") {
            box.kind = PieceKind::Floor;
            box.color = FLOOR_COLOR;
        } else {
            box.kind = PieceKind::Wall;
            box.color = WALL_COLOR;
        }
        if (static_cast<int>(pieces.size()) < MAX_HOUSE_PIECES) {
            pieces.push_back(box);
        }
    }

    RefreshHint();
}

void Save()
{
    std::ofstream file(HOUSE_FILE, std::ios::trunc);
    if (!file) {
        return;
    }

    file << "hhhouse 1\n";
    for (const SolidBox& piece : pieces) {
        const char* kindName = piece.kind == PieceKind::Floor ? "floor" : "wall";
        file << kindName << ' ' << piece.center.x << ' ' << piece.center.y << ' ' << piece.center.z
             << ' ' << piece.size.x << ' ' << piece.size.y << ' ' << piece.size.z << '\n';
    }

    RefreshHint();
}

void Draw()
{
    DrawPlane({0.0f, -0.08f, 0.0f}, {BUILD_PLANE_M, BUILD_PLANE_M}, GRASS_COLOR);
    DrawGrid(BUILD_GRID_SLICES, BUILD_GRID_SPACING);
    for (const SolidBox& piece : pieces) {
        DrawSolid(piece);
    }
}

void DrawBuilderPreview(const Camera3D& camera)
{
    SolidBox box;
    if (!PreviewBox(camera, box)) {
        return;
    }

    box.color = Occupied(box) ? GHOST_BAD_COLOR : GHOST_OK_COLOR;
    DrawCubeWiresV(box.center, box.size, box.color);
}

Vector3 MovePlayer(const Vector3& eyePosition, const Vector3& worldDelta)
{
    Vector3 next = eyePosition;

    Vector3 tryX = next;
    tryX.x += worldDelta.x;
    if (!OverlapsWalls(tryX)) {
        next.x = tryX.x;
    }

    Vector3 tryZ = next;
    tryZ.z += worldDelta.z;
    if (!OverlapsWalls(tryZ)) {
        next.z = tryZ.z;
    }

    return next;
}

Vector3 SpawnPosition(const bool host, const float eyeHeight)
{
    return {host ? -2.0f : 2.0f, eyeHeight, 0.0f};
}

void SetKind(const PieceKind kind)
{
    currentKind = kind;
    RefreshHint();
}

void RotatePiece()
{
    wallAlongX = !wallAlongX;
    RefreshHint();
}

bool PlaceFromCamera(const Camera3D& camera)
{
    SolidBox box;
    if (!PreviewBox(camera, box)) {
        return false;
    }
    if (Occupied(box) || static_cast<int>(pieces.size()) >= MAX_HOUSE_PIECES) {
        return false;
    }

    pieces.push_back(box);
    Save();
    return true;
}

bool DeleteFromCamera(const Camera3D& camera)
{
    const Ray ray = AimRay(camera);

    int hitIndex = -1;
    float nearest = BUILDER_RAY_M;
    for (int i = 0; i < static_cast<int>(pieces.size()); ++i) {
        const RayCollision hit = GetRayCollisionBox(ray, BoxBounds(pieces[static_cast<std::size_t>(i)]));
        if (hit.hit && hit.distance < nearest) {
            nearest = hit.distance;
            hitIndex = i;
        }
    }

    if (hitIndex < 0) {
        return false;
    }

    pieces.erase(pieces.begin() + hitIndex);
    Save();
    return true;
}

const char* BuilderHint()
{
    return builderHint;
}

} // namespace House
