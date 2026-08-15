#include <raylib.h>
#include <raymath.h>

#include "steam/steam_client.hpp"

#include <steam/steam_api.h>

#include <charconv>
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

bool ParseSteamID64(
    const std::string& text,
    std::uint64_t& output)
{
    if (text.empty())
        return false;

    const char* begin = text.data();
    const char* end = begin + text.size();

    const auto result =
        std::from_chars(begin, end, output);

    if (result.ec != std::errc{} || result.ptr != end)
        return false;

    return CSteamID(output).IsValid();
}

enum class GameScreen
{
    Menu,
    Hosting,
    EnterHostID,
    Connecting,
    Playing
};

int main()
{
    constexpr int screenWidth = 1280;
    constexpr int screenHeight = 720;

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

        const std::string ownSteamID =
            std::to_string(
                SteamUser()->GetSteamID().ConvertToUint64()
            );

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

        std::string hostIDInput;
        std::string inputError;

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
            }

            if (currentScreen == GameScreen::Menu)
            {
                if (IsKeyPressed(KEY_SPACE))
                {
                    camera.position = {
                        -3.0f,
                        PLAYER_EYE_HEIGHT,
                        0.0f
                    };

                    if (network.StartHost()) currentScreen = GameScreen::Hosting;
                }

                if (IsKeyPressed(KEY_W))
                {
                    hostIDInput.clear();
                    inputError.clear();
                    currentScreen = GameScreen::EnterHostID;
                }
            }
            else if (currentScreen == GameScreen::EnterHostID)
            {
                int character = 0;

                while ((character = GetCharPressed()) > 0)
                {
                    if (character >= '0' &&
                        character <= '9' &&
                        hostIDInput.size() < 20)
                    {
                        hostIDInput.push_back(
                            static_cast<char>(character)
                        );
                    }
                }

                if (IsKeyPressed(KEY_BACKSPACE) &&
                    !hostIDInput.empty())
                {
                    hostIDInput.pop_back();
                }

                if (IsKeyPressed(KEY_ENTER))
                {
                    std::uint64_t hostSteamID = 0;

                    if (!ParseSteamID64(hostIDInput, hostSteamID))
                        inputError = "Invalid SteamID64";
                    else if (hostSteamID == SteamUser()->GetSteamID().ConvertToUint64())inputError = "You cannot connect to yourself";
                    else if (network.ConnectToHost(hostSteamID))
                    {
                        camera.position = {3.0f, PLAYER_EYE_HEIGHT,
                            0.0f
                        };

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
                    "SPACE - Host game",
                    60,
                    60,
                    30,
                    WHITE
                );

                DrawText(
                    "W - Join game",
                    60,
                    110,
                    30,
                    WHITE
                );

                DrawText(
                    network.Status().c_str(),
                    60,
                    180,
                    20,
                    GRAY
                );
            }
            else if (currentScreen == GameScreen::Hosting)
            {
                DrawText(
                    "Waiting for client",
                    60,
                    60,
                    30,
                    WHITE
                );

                DrawText(
                    "Send this SteamID64 to the other player:",
                    60,
                    120,
                    20,
                    GRAY
                );

                DrawText(
                    ownSteamID.c_str(),
                    60,
                    160,
                    36,
                    YELLOW
                );

                DrawText(
                    network.Status().c_str(),
                    60,
                    230,
                    20,
                    WHITE
                );
            }
            else if (currentScreen == GameScreen::EnterHostID)
            {
                DrawText(
                    "Enter host SteamID64:",
                    60,
                    60,
                    30,
                    WHITE
                );

                DrawRectangleLines(
                    60,
                    120,
                    500,
                    50,
                    WHITE
                );

                DrawText(
                    hostIDInput.c_str(),
                    75,
                    132,
                    26,
                    YELLOW
                );

                DrawText(
                    "Press ENTER to connect",
                    60,
                    195,
                    20,
                    GRAY
                );

                if (!inputError.empty())
                {
                    DrawText(
                        inputError.c_str(),
                        60,
                        235,
                        20,
                        RED
                    );
                }
            }
            else if (currentScreen == GameScreen::Connecting)
            {
                DrawText(
                    network.Status().c_str(),
                    60,
                    60,
                    30,
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