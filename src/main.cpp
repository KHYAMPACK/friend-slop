#include <raylib.h>
#include <raymath.h>

#include "steam/steam_client.hpp"

#include <steam/steam_api.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

constexpr const char* PLAYER_TEXTURE_PATH =
    R"(player.png)";

constexpr std::uint32_t POSITION_PACKET_MAGIC = 0x504F5331;
constexpr float PLAYER_EYE_HEIGHT = 1.8f;

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
        if (connection_ != k_HSteamNetConnection_Invalid)
        {
            SteamNetworkingSockets()->CloseConnection(
                connection_,
                0,
                "Game closed",
                false
            );
        }

        if (listenSocket_ != k_HSteamListenSocket_Invalid)
            SteamNetworkingSockets()->CloseListenSocket(listenSocket_);
    }

    bool StartHost()
    {
        hosting_ = true;

        ISteamNetworkingSockets *sockets = SteamNetworkingSockets();
        if (sockets == nullptr)
        {
            status_ = "Steam networking is not available";
            hosting_ = false;
            return false;
        }

        listenSocket_ = sockets->CreateListenSocketP2P(
            0,          // Virtual port
            0,          // Option count
            nullptr
        );

        if (listenSocket_ == k_HSteamListenSocket_Invalid)
        {
            status_ = "Failed to create P2P listen socket";
            hosting_ = false;
            return false;
        }

        status_ = "Waiting for another player...";
        return true;
    }

    bool ConnectToHost(std::uint64_t hostSteamID)
    {
        hosting_ = false;

        ISteamNetworkingSockets *sockets = SteamNetworkingSockets();
        if (sockets == nullptr)
        {
            status_ = "Steam networking is not available";
            return false;
        }

        SteamNetworkingIdentity identity{};
        identity.Clear();
        identity.SetSteamID64(hostSteamID);

        connection_ = sockets->ConnectP2P(
            identity,
            0,          // Must match the host virtual port
            0,
            nullptr
        );

        if (connection_ == k_HSteamNetConnection_Invalid)
        {
            status_ = "Failed to start connection";
            return false;
        }

        status_ = "Connecting through Steam...";
        return true;
    }

    void SendPosition(float x, float z)
    {
        if (!connected_)
            return;

        const PositionPacket packet{
            POSITION_PACKET_MAGIC,
            x,
            z
        };

        SteamNetworkingSockets()->SendMessageToConnection(
            connection_,
            &packet,
            static_cast<std::uint32_t>(sizeof(packet)),
            k_nSteamNetworkingSend_Unreliable |
            k_nSteamNetworkingSend_NoNagle,
            nullptr
        );
    }

    bool ReceivePosition(float& outputX, float& outputZ)
    {
        if (!connected_)
            return false;

        bool receivedPosition = false;

        while (true)
        {
            SteamNetworkingMessage_t* message = nullptr;

            const int count =
                SteamNetworkingSockets()->ReceiveMessagesOnConnection(
                    connection_,
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
                        std::abs(packet.x) < 10000.0f &&
                        std::abs(packet.z) < 10000.0f)
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
        return connected_;
    }

    [[nodiscard]] bool Hosting() const
    {
        return hosting_;
    }

    [[nodiscard]] const std::string& Status() const
    {
        return status_;
    }

private:
    HSteamListenSocket listenSocket_ =
        k_HSteamListenSocket_Invalid;

    HSteamNetConnection connection_ =
        k_HSteamNetConnection_Invalid;

    bool connected_ = false;
    bool hosting_ = false;

    std::string status_ = "Not connected";

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
        listenSocket_ != k_HSteamListenSocket_Invalid &&
        callback->m_info.m_hListenSocket == listenSocket_)
    {
        // This example only allows one remote player.
        if (connection_ != k_HSteamNetConnection_Invalid)
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
            connection_ = changedConnection;
            status_ = "Player found; finishing connection...";
        }
        else
        {
            SteamNetworkingSockets()->CloseConnection(
                changedConnection,
                0,
                "Could not accept connection",
                false
            );

            status_ = "Could not accept connection";
        }

        return;
    }

    // Ignore callbacks unrelated to our active connection.
    if (changedConnection != connection_)
        return;

    if (newState == k_ESteamNetworkingConnectionState_Connected)
    {
        connected_ = true;
        status_ = "Connected";
        return;
    }

    if (newState ==
            k_ESteamNetworkingConnectionState_ClosedByPeer ||
        newState ==
            k_ESteamNetworkingConnectionState_ProblemDetectedLocally)
    {
        connected_ = false;

        status_ = "Disconnected";

        if (callback->m_info.m_szEndDebug[0] != '\0')
        {
            status_ += ": ";
            status_ += callback->m_info.m_szEndDebug;
        }

        // Steam requires the local connection object to be destroyed
        // after receiving a closed/problem callback.
        SteamNetworkingSockets()->CloseConnection(
            connection_,
            0,
            nullptr,
            false
        );

        connection_ = k_HSteamNetConnection_Invalid;
    }
}

enum class GameScreen
{
    Menu,
    Hosting,
    Connecting,
    Playing
};

int main()
{
    constexpr int screenWidth = 1280;
    constexpr int screenHeight = 720;

    SteamPrepareLaunch();

    InitWindow(
        screenWidth,
        screenHeight,
        "Friendslop Steam P2P"
    );

    SetTargetFPS(144);

    if (!SteamInit())
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
    ISteamNetworkingUtils *networkingUtils = SteamNetworkingUtils();
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

        constexpr float movementSpeed = 8.0f;
        constexpr float mouseSensitivity = 0.0025f;
        constexpr float sendInterval = 1.0f / 20.0f;

        while (!WindowShouldClose())
        {
            // Steam callbacks must be processed regularly.
            SteamTick();

            if (!p2pJoinStarted &&
                !network.Hosting() &&
                !network.Connected() &&
                SteamInLobby() &&
                !SteamIsLobbyOwner())
            {
                const std::uint64_t ownerSteamID = SteamLobbyOwnerID64();
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
                        SteamCreateLobby();
                        currentScreen = GameScreen::Hosting;
                    }
                }

                if (IsKeyPressed(KEY_J))
                {
                    if (SteamJoinFriendLobby())
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

                yaw -= mouseDelta.x * mouseSensitivity;
                pitch -= mouseDelta.y * mouseSensitivity;
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
                        movementSpeed * deltaTime
                    );

                    camera.position =
                        Vector3Add(camera.position, movement);
                }

                camera.target =
                    Vector3Add(camera.position, lookDirection);

                sendTimer += deltaTime;

                if (sendTimer >= sendInterval)
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
                    SteamLobbyHint(),
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
                    SteamLobbyHint(),
                    60,
                    150,
                    20,
                    GRAY
                );

                const int memberCount = SteamLobbyMemberCount();
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
                        SteamLobbyMemberName(i),
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
                    SteamLobbyHint(),
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

                DrawFPS(screenWidth - 100, 20);

                // Crosshair
                DrawLine(
                    screenWidth / 2 - 8,
                    screenHeight / 2,
                    screenWidth / 2 + 8,
                    screenHeight / 2,
                    WHITE
                );

                DrawLine(
                    screenWidth / 2,
                    screenHeight / 2 - 8,
                    screenWidth / 2,
                    screenHeight / 2 + 8,
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

    SteamShutdown();
    CloseWindow();

    return 0;
}
