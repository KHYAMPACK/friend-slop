#include <raylib.h>
#include <raymath.h>

#include "steam/steam_client.hpp"
#include "core/game_constants.hpp"

#include <steam/steam_api.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

namespace {

constexpr const char* PLAYER_TEXTURE_PATH =
    R"(player.png)";

constexpr std::uint32_t POSITION_PACKET_MAGIC = 0x504F5331;
constexpr float PLAYER_EYE_HEIGHT = 1.8f;
constexpr float MOVEMENT_SPEED = 8.0f;
constexpr float MOUSE_SENSITIVITY = 0.0025f;
constexpr float SEND_INTERVAL = 1.0f / 20.0f;
constexpr float POSITION_ABS_MAX = 10000.0f;
constexpr int WINDOW_FPS = 144;

struct PositionPacket
{
    std::uint32_t magic;
    float x;
    float z;
};

class PeerNetwork
{
public:
    ~PeerNetwork() {
        if (connection != k_HSteamNetConnection_Invalid)
        {
            SteamNetworkingSockets()->CloseConnection(
                connection,
                0,
                "Game closed",
                false
            );
        }

        if (listenSocket != k_HSteamListenSocket_Invalid)
            SteamNetworkingSockets()->CloseListenSocket(listenSocket);
    }

    bool StartHost()
    {
        hosting = true;

        ISteamNetworkingSockets* sockets = SteamNetworkingSockets();
        if (sockets == nullptr)
        {
            status = "Steam networking is not available";
            hosting = false;
            return false;
        }

        listenSocket = sockets->CreateListenSocketP2P(
            0,          // Virtual port
            0,          // Option count
            nullptr
        );

        if (listenSocket == k_HSteamListenSocket_Invalid)
        {
            status = "Failed to create P2P listen socket";
            hosting = false;
            return false;
        }

        status = "Waiting for another player...";
        return true;
    }

    bool ConnectToHost(const std::uint64_t hostSteamID)
    {
        hosting = false;

        ISteamNetworkingSockets* sockets = SteamNetworkingSockets();
        if (sockets == nullptr)
        {
            status = "Steam networking is not available";
            return false;
        }

        SteamNetworkingIdentity identity{};
        identity.Clear();
        identity.SetSteamID64(hostSteamID);

        connection = sockets->ConnectP2P(
            identity,
            0,          // Must match the host virtual port
            0,
            nullptr
        );

        if (connection == k_HSteamNetConnection_Invalid)
        {
            status = "Failed to start connection";
            return false;
        }

        status = "Connecting through Steam...";
        return true;
    }

    void SendPosition(const float x, const float z)
    {
        if (!connected)
            return;

        const PositionPacket packet{
            POSITION_PACKET_MAGIC,
            x,
            z
        };

        SteamNetworkingSockets()->SendMessageToConnection(
            connection,
            &packet,
            static_cast<std::uint32_t>(sizeof(packet)),
            k_nSteamNetworkingSend_Unreliable |
            k_nSteamNetworkingSend_NoNagle,
            nullptr
        );
    }

    bool ReceivePosition(float& outputX, float& outputZ)
    {
        if (!connected)
            return false;

        bool receivedPosition = false;

        while (true)
        {
            SteamNetworkingMessage_t* message = nullptr;

            const int count =
                SteamNetworkingSockets()->ReceiveMessagesOnConnection(
                    connection,
                    &message,
                    1
                );

            if (count <= 0)
                break;

            if (message != nullptr)
            {
                if (message->m_cbSize == sizeof(PositionPacket))
                {
                    PositionPacket packet{};
                    std::memcpy(
                        &packet,
                        message->m_pData,
                        sizeof(packet)
                    );

                    if (packet.magic == POSITION_PACKET_MAGIC &&
                        std::isfinite(packet.x) &&
                        std::isfinite(packet.z) &&
                        std::abs(packet.x) < POSITION_ABS_MAX &&
                        std::abs(packet.z) < POSITION_ABS_MAX)
                    {
                        outputX = packet.x;
                        outputZ = packet.z;
                        receivedPosition = true;
                    }
                }

                message->Release();
            }
        }

        return receivedPosition;
    }

    [[nodiscard]] bool Connected() const
    {
        return connected;
    }

    [[nodiscard]] bool Hosting() const
    {
        return hosting;
    }

    [[nodiscard]] const std::string& Status() const
    {
        return status;
    }

private:
    HSteamListenSocket listenSocket =
        k_HSteamListenSocket_Invalid;

    HSteamNetConnection connection =
        k_HSteamNetConnection_Invalid;

    bool connected = false;
    bool hosting = false;

    std::string status = "Not connected";

    STEAM_CALLBACK(
        PeerNetwork,
        OnConnectionStatusChanged,
        SteamNetConnectionStatusChangedCallback_t
    );
};

void PeerNetwork::OnConnectionStatusChanged(
    SteamNetConnectionStatusChangedCallback_t* callback)
{
    if (callback == nullptr)
        return;

    const HSteamNetConnection changedConnection =
        callback->m_hConn;

    const ESteamNetworkingConnectionState newState =
        callback->m_info.m_eState;

    // Incoming ConnectP2P only. Outbound joins report listenSocket Invalid,
    // which also matches a client that never hosted — do not Accept/Close those.
    if (newState == k_ESteamNetworkingConnectionState_Connecting &&
        listenSocket != k_HSteamListenSocket_Invalid &&
        callback->m_info.m_hListenSocket == listenSocket)
    {
        // This example only allows one remote player.
        if (connection != k_HSteamNetConnection_Invalid)
        {
            SteamNetworkingSockets()->CloseConnection(
                changedConnection,
                0,
                "Host already has a player",
                false
            );

            return;
        }

        const EResult result =
            SteamNetworkingSockets()->AcceptConnection(
                changedConnection
            );

        if (result == k_EResultOK)
        {
            connection = changedConnection;
            status = "Player found; finishing connection...";
        }
        else
        {
            SteamNetworkingSockets()->CloseConnection(
                changedConnection,
                0,
                "Could not accept connection",
                false
            );

            status = "Could not accept connection";
        }

        return;
    }

    // Ignore callbacks unrelated to our active connection.
    if (changedConnection != connection)
        return;

    if (newState == k_ESteamNetworkingConnectionState_Connected)
    {
        connected = true;
        status = "Connected";
        return;
    }

    if (newState ==
            k_ESteamNetworkingConnectionState_ClosedByPeer ||
        newState ==
            k_ESteamNetworkingConnectionState_ProblemDetectedLocally)
    {
        connected = false;

        status = "Disconnected";

        if (callback->m_info.m_szEndDebug[0] != '\0')
        {
            status += ": ";
            status += callback->m_info.m_szEndDebug;
        }

        // Steam requires the local connection object to be destroyed
        // after receiving a closed/problem callback.
        SteamNetworkingSockets()->CloseConnection(
            connection,
            0,
            nullptr,
            false
        );

        connection = k_HSteamNetConnection_Invalid;
    }
}

enum class GameScreen
{
    Menu,
    Hosting,
    Connecting,
    Playing
};

} // namespace

int main()
{
    HhSteam::PrepareLaunch();

    InitWindow(
        hh::WINDOW_WIDTH,
        hh::WINDOW_HEIGHT,
        "Friendslop Steam P2P"
    );

    SetTargetFPS(WINDOW_FPS);

    if (!HhSteam::Init())
    {
        while (!WindowShouldClose())
        {
            BeginDrawing();
            ClearBackground(BLACK);

            DrawText(
                "SteamAPI_Init failed",
                40,
                40,
                32,
                RED
            );

            DrawText(
                "Make sure Steam is running and steam_appid.txt exists.",
                40,
                90,
                20,
                WHITE
            );

            EndDrawing();
        }

        CloseWindow();
        return 1;
    }

    // Starts Steam Datagram Relay initialization early.
    ISteamNetworkingUtils* networkingUtils = SteamNetworkingUtils();
    if (networkingUtils != nullptr)
        networkingUtils->InitRelayNetworkAccess();

    {
        PeerNetwork network;

        const std::string ownName =
            SteamFriends() != nullptr &&
                    SteamFriends()->GetPersonaName() != nullptr
                ? SteamFriends()->GetPersonaName()
                : "(unknown)";

        Texture2D playerTexture =
            LoadTexture(PLAYER_TEXTURE_PATH);

        const char* billboardFragmentShader = R"(
            #version 330

            in vec2 fragTexCoord;
            in vec4 fragColor;

            uniform sampler2D texture0;

            out vec4 finalColor;

            void main()
            {
                vec4 color =
                    texture(texture0, fragTexCoord) * fragColor;

                if (color.a < 0.1)
                    discard;

                finalColor = color;
            }
        )";

        Shader billboardShader =
            LoadShaderFromMemory(
                nullptr,
                billboardFragmentShader
            );

        Camera3D camera{};
        camera.position = {-3.0f, PLAYER_EYE_HEIGHT, 0.0f};
        camera.target = {-3.0f, PLAYER_EYE_HEIGHT, 1.0f};
        camera.up = {0.0f, 1.0f, 0.0f};
        camera.fovy = 75.0f;
        camera.projection = CAMERA_PERSPECTIVE;

        GameScreen currentScreen = GameScreen::Menu;

        bool p2pJoinStarted = false;

        float remoteX = 0.0f;
        float remoteZ = 0.0f;
        bool receivedRemotePosition = false;

        float yaw = 0.0f;
        float pitch = 0.0f;
        float sendTimer = 0.0f;

        bool cursorLocked = false;

        while (!WindowShouldClose())
        {
            // Steam callbacks must be processed regularly.
            HhSteam::Tick();

            if (!p2pJoinStarted &&
                !network.Hosting() &&
                !network.Connected() &&
                HhSteam::InLobby() &&
                !HhSteam::IsLobbyOwner())
            {
                const std::uint64_t ownerSteamID = HhSteam::LobbyOwnerID64();
                if (ownerSteamID != 0 && network.ConnectToHost(ownerSteamID))
                {
                    camera.position = {
                        3.0f,
                        PLAYER_EYE_HEIGHT,
                        0.0f
                    };
                    p2pJoinStarted = true;
                    currentScreen = GameScreen::Connecting;
                }
            }

            if ((currentScreen == GameScreen::Hosting || currentScreen == GameScreen::Connecting) && network.Connected())
            {
                currentScreen = GameScreen::Playing;
                receivedRemotePosition = false;
                sendTimer = 0.0f;

                DisableCursor();
                cursorLocked = true;
            }

            if (currentScreen == GameScreen::Playing &&
                !network.Connected())
            {
                if (cursorLocked)
                {
                    EnableCursor();
                    cursorLocked = false;
                }

                currentScreen = network.Hosting()
                    ? GameScreen::Hosting
                    : GameScreen::Menu;
                if (!network.Hosting())
                    p2pJoinStarted = false;
            }

            if (currentScreen == GameScreen::Menu)
            {
                if (IsKeyPressed(KEY_C))
                {
                    camera.position = {
                        -3.0f,
                        PLAYER_EYE_HEIGHT,
                        0.0f
                    };

                    if (network.StartHost())
                    {
                        HhSteam::CreateLobby();
                        currentScreen = GameScreen::Hosting;
                    }
                }

                if (IsKeyPressed(KEY_J))
                {
                    if (HhSteam::JoinFriendLobby())
                    {
                        camera.position = {
                            3.0f,
                            PLAYER_EYE_HEIGHT,
                            0.0f
                        };
                        p2pJoinStarted = false;
                        currentScreen = GameScreen::Connecting;
                    }
                }
            }
            else if (currentScreen == GameScreen::Playing)
            {
                const float deltaTime = GetFrameTime();
                const Vector2 mouseDelta = GetMouseDelta();

                yaw -= mouseDelta.x * MOUSE_SENSITIVITY;
                pitch -= mouseDelta.y * MOUSE_SENSITIVITY;
                pitch = Clamp(pitch, -1.5f, 1.5f);

                const Vector3 lookDirection = {
                    std::sin(yaw) * std::cos(pitch),
                    std::sin(pitch),
                    std::cos(yaw) * std::cos(pitch)
                };

                const Vector3 forward = {
                    std::sin(yaw),
                    0.0f,
                    std::cos(yaw)
                };

                const Vector3 right = {
                    std::cos(yaw),
                    0.0f,
                    -std::sin(yaw)
                };

                Vector3 movement{};

                if (IsKeyDown(KEY_W))
                    movement = Vector3Add(movement, forward);

                if (IsKeyDown(KEY_S))
                    movement = Vector3Subtract(movement, forward);

                if (IsKeyDown(KEY_D))
                    movement = Vector3Add(movement, right);

                if (IsKeyDown(KEY_A))
                    movement = Vector3Subtract(movement, right);

                if (Vector3LengthSqr(movement) > 0.0f)
                {
                    movement = Vector3Normalize(movement);
                    movement = Vector3Scale(
                        movement,
                        MOVEMENT_SPEED * deltaTime
                    );

                    camera.position =
                        Vector3Add(camera.position, movement);
                }

                camera.target =
                    Vector3Add(camera.position, lookDirection);

                sendTimer += deltaTime;

                if (sendTimer >= SEND_INTERVAL)
                {
                    sendTimer = 0.0f;

                    network.SendPosition(
                        camera.position.x,
                        camera.position.z
                    );
                }

                if (network.ReceivePosition(remoteX, remoteZ))
                    receivedRemotePosition = true;
            }

            BeginDrawing();

            if (currentScreen == GameScreen::Playing)
                ClearBackground({120, 180, 235, 255});
            else
                ClearBackground(BLACK);

            if (currentScreen == GameScreen::Menu)
            {
                DrawText(
                    "C - Host game",
                    60,
                    60,
                    30,
                    WHITE
                );

                DrawText(
                    "J - Join friend's lobby",
                    60,
                    110,
                    30,
                    WHITE
                );

                DrawText(
                    HhSteam::LobbyHint(),
                    60,
                    160,
                    20,
                    GRAY
                );

                DrawText(
                    network.Status().c_str(),
                    60,
                    200,
                    20,
                    GRAY
                );
            }
            else if (currentScreen == GameScreen::Hosting)
            {
                DrawText(
                    "Waiting for friend (they press J)",
                    60,
                    60,
                    30,
                    WHITE
                );

                DrawText(
                    ownName.c_str(),
                    60,
                    110,
                    22,
                    WHITE
                );

                DrawText(
                    HhSteam::LobbyHint(),
                    60,
                    150,
                    20,
                    GRAY
                );

                const int memberCount = HhSteam::LobbyMemberCount();
                DrawText(
                    TextFormat("In lobby (%d):", memberCount),
                    60,
                    190,
                    20,
                    WHITE
                );

                for (int i = 0; i < memberCount; ++i)
                {
                    DrawText(
                        HhSteam::LobbyMemberName(i),
                        84,
                        220 + i * 28,
                        20,
                        RAYWHITE
                    );
                }

                DrawText(
                    network.Status().c_str(),
                    60,
                    220 + memberCount * 28 + 20,
                    20,
                    WHITE
                );
            }
            else if (currentScreen == GameScreen::Connecting)
            {
                DrawText(
                    HhSteam::LobbyHint(),
                    60,
                    60,
                    24,
                    WHITE
                );

                DrawText(
                    network.Status().c_str(),
                    60,
                    110,
                    24,
                    WHITE
                );
            }
            else if (currentScreen == GameScreen::Playing)
            {
                BeginMode3D(camera);

                DrawPlane(
                    {0.0f, 0.0f, 0.0f},
                    {80.0f, 80.0f},
                    {70, 140, 70, 255}
                );

                DrawGrid(80, 1.0f);

                if (receivedRemotePosition)
                {
                    const Vector3 remotePosition = {
                        remoteX,
                        1.5f,
                        remoteZ
                    };

                    if (playerTexture.id != 0)
                    {
                        BeginShaderMode(billboardShader);

                        DrawBillboard(
                            camera,
                            playerTexture,
                            remotePosition,
                            3.0f,
                            WHITE
                        );

                        EndShaderMode();
                    }
                    else
                    {
                        // Fallback if the texture path is wrong.
                        DrawCube(
                            {remoteX, 0.9f, remoteZ},
                            0.8f,
                            1.8f,
                            0.8f,
                            RED
                        );
                    }
                }

                EndMode3D();

                DrawText(
                    network.Hosting() ? "HOST" : "CLIENT",
                    20,
                    20,
                    22,
                    WHITE
                );

                DrawFPS(hh::WINDOW_WIDTH - 100, 20);

                // Crosshair
                DrawLine(
                    hh::WINDOW_WIDTH / 2 - 8,
                    hh::WINDOW_HEIGHT / 2,
                    hh::WINDOW_WIDTH / 2 + 8,
                    hh::WINDOW_HEIGHT / 2,
                    WHITE
                );

                DrawLine(
                    hh::WINDOW_WIDTH / 2,
                    hh::WINDOW_HEIGHT / 2 - 8,
                    hh::WINDOW_WIDTH / 2,
                    hh::WINDOW_HEIGHT / 2 + 8,
                    WHITE
                );
            }

            EndDrawing();
        }

        if (cursorLocked)
            EnableCursor();

        if (playerTexture.id != 0)
            UnloadTexture(playerTexture);

        if (billboardShader.id != 0)
            UnloadShader(billboardShader);
    }

    HhSteam::Shutdown();
    CloseWindow();

    return 0;
}
