#include "raylib.h"
#include "rlgl.h"
#include "World.h"
#include <cmath>
#include <ctime>

constexpr float PLAYER_HALF_WIDTH = 0.3f;
constexpr float PLAYER_HEIGHT = 1.8f;
constexpr float EYE_HEIGHT = 1.6f;
constexpr float GRAVITY = -20.0f;
constexpr float JUMP_VELOCITY = 8.0f;
constexpr float MOVE_SPEED = 5.0f;
constexpr float MOUSE_SENSITIVITY = 0.003f;
constexpr float REACH = 6.0f;

enum class GameState { MENU, PLAYING, PAUSED };

struct Player {
    Vector3 pos{};
    Vector3 vel{};
    float yaw = 0.0f;
    float pitch = 0.0f;
    bool grounded = false;
};

static bool CheckCollision(World& world, Vector3 pos) {
    float minX = pos.x - PLAYER_HALF_WIDTH, maxX = pos.x + PLAYER_HALF_WIDTH;
    float minY = pos.y, maxY = pos.y + PLAYER_HEIGHT;
    float minZ = pos.z - PLAYER_HALF_WIDTH, maxZ = pos.z + PLAYER_HALF_WIDTH;

    int x0 = (int)floorf(minX), x1 = (int)floorf(maxX - 0.0001f);
    int y0 = (int)floorf(minY), y1 = (int)floorf(maxY - 0.0001f);
    int z0 = (int)floorf(minZ), z1 = (int)floorf(maxZ - 0.0001f);

    for (int x = x0; x <= x1; x++)
        for (int y = y0; y <= y1; y++)
            for (int z = z0; z <= z1; z++)
                if (world.IsSolid(x, y, z)) return true;
    return false;
}

static void MoveAndCollide(World& world, Player& p, Vector3 delta) {
    p.pos.x += delta.x;
    if (CheckCollision(world, p.pos)) { p.pos.x -= delta.x; p.vel.x = 0; }

    p.pos.z += delta.z;
    if (CheckCollision(world, p.pos)) { p.pos.z -= delta.z; p.vel.z = 0; }

    p.pos.y += delta.y;
    if (CheckCollision(world, p.pos)) {
        if (delta.y < 0) p.grounded = true;
        p.pos.y -= delta.y;
        p.vel.y = 0;
    } else {
        p.grounded = false;
    }
}

static bool BoxOverlapsPlayer(Player& p, Vector3 blockPos) {
    float bx0 = blockPos.x, bx1 = blockPos.x + 1;
    float by0 = blockPos.y, by1 = blockPos.y + 1;
    float bz0 = blockPos.z, bz1 = blockPos.z + 1;
    float px0 = p.pos.x - PLAYER_HALF_WIDTH, px1 = p.pos.x + PLAYER_HALF_WIDTH;
    float py0 = p.pos.y, py1 = p.pos.y + PLAYER_HEIGHT;
    float pz0 = p.pos.z - PLAYER_HALF_WIDTH, pz1 = p.pos.z + PLAYER_HALF_WIDTH;
    return (bx0 < px1 && bx1 > px0 && by0 < py1 && by1 > py0 && bz0 < pz1 && bz1 > pz0);
}

int main() {
    const int screenWidth = 1280;
    const int screenHeight = 720;

    InitWindow(screenWidth, screenHeight, "Minicraft");
    SetExitKey(KEY_NULL);
    rlDisableBackfaceCulling();
    SetRandomSeed((unsigned int)time(nullptr));

    GameState state = GameState::MENU;
    bool cursorLocked = false;
    bool skipInput = false;

    World* world = nullptr;
    Player player{};
    int spawnX = 0;
    int spawnZ = 0;
    int groundY = 0;

    BlockType hotbar[5] = { BlockType::Dirt, BlockType::Stone, BlockType::Wood, BlockType::Leaves, BlockType::Sand };
    const char* hotbarNames[5] = { "Dirt", "Stone", "Wood", "Leaves", "Sand" };
    int selected = 0;

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        if (dt > 0.05f) dt = 0.05f;

        if (state == GameState::MENU) {
            bool skip = skipInput;
            if (skipInput) { skipInput = false; }
            if (!cursorLocked) {
                EnableCursor();
                cursorLocked = true;
            }

            Vector2 mousePos = GetMousePosition();
            float btnW = 240, btnH = 50;
            float btnX = screenWidth / 2.0f - btnW / 2.0f;
            float playBtnY = screenHeight / 2.0f - 10;
            float quitBtnY = playBtnY + 70;
            bool mouseOverPlay = CheckCollisionPointRec(mousePos, { btnX, playBtnY, btnW, btnH });
            bool mouseOverQuit = CheckCollisionPointRec(mousePos, { btnX, quitBtnY, btnW, btnH });

            bool startGame = !skip && ((mouseOverPlay && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) || IsKeyPressed(KEY_ENTER));
            bool quitGame = !skip && (mouseOverQuit && IsMouseButtonPressed(MOUSE_BUTTON_LEFT));

            if (startGame) {
                world = new World();
                for (int dx = -2; dx <= 2; dx++)
                    for (int dz = -2; dz <= 2; dz++)
                        world->EnsureChunk(dx, dz);
                spawnX = 0;
                spawnZ = 0;
                groundY = world->HeightAt(spawnX, spawnZ);
                player = {};
                player.pos = { (float)spawnX + 0.5f, (float)(groundY + 1), (float)spawnZ + 0.5f };
                selected = 0;
                DisableCursor();
                cursorLocked = false;
                skipInput = true;
                state = GameState::PLAYING;
            }
            if (quitGame) break;

            BeginDrawing();
            ClearBackground(Color{ 30, 30, 30, 255 });

            const char* title = "MINICRAFT";
            int titleSize = 60;
            int titleWidth = MeasureText(title, titleSize);
            DrawText(title, screenWidth / 2 - titleWidth / 2, screenHeight / 2 - 120, titleSize, WHITE);

            const char* subtitle = "A Voxel Sandbox";
            int subSize = 18;
            int subWidth = MeasureText(subtitle, subSize);
            DrawText(subtitle, screenWidth / 2 - subWidth / 2, screenHeight / 2 - 50, subSize, GRAY);

            Color playBg = mouseOverPlay ? Color{ 80, 160, 80, 255 } : Color{ 60, 120, 60, 255 };
            DrawRectangle((int)btnX, (int)playBtnY, (int)btnW, (int)btnH, playBg);
            const char* playText = "Play";
            int playTextW = MeasureText(playText, 24);
            DrawText(playText, (int)(btnX + btnW / 2 - playTextW / 2), (int)(playBtnY + 13), 24, WHITE);

            Color quitBg = mouseOverQuit ? Color{ 160, 60, 60, 255 } : Color{ 120, 50, 50, 255 };
            DrawRectangle((int)btnX, (int)quitBtnY, (int)btnW, (int)btnH, quitBg);
            const char* quitText = "Quit";
            int quitTextW = MeasureText(quitText, 24);
            DrawText(quitText, (int)(btnX + btnW / 2 - quitTextW / 2), (int)(quitBtnY + 13), 24, WHITE);

            DrawFPS(10, 10);
            EndDrawing();
        } else if (state == GameState::PLAYING) {
            if (IsKeyPressed(KEY_ESCAPE)) {
                EnableCursor();
                skipInput = true;
                state = GameState::PAUSED;
                continue;
            }

            Vector2 mouseDelta = GetMouseDelta();
            player.yaw -= mouseDelta.x * MOUSE_SENSITIVITY;
            player.pitch -= mouseDelta.y * MOUSE_SENSITIVITY;
            if (player.pitch > 1.5f) player.pitch = 1.5f;
            if (player.pitch < -1.5f) player.pitch = -1.5f;

            Vector3 forward = {
                cosf(player.pitch) * sinf(player.yaw),
                sinf(player.pitch),
                cosf(player.pitch) * cosf(player.yaw)
            };
            Vector3 flatForward = { sinf(player.yaw), 0.0f, cosf(player.yaw) };
            Vector3 right = { -cosf(player.yaw), 0.0f, sinf(player.yaw) };

            Vector3 moveDir = { 0.0f, 0.0f, 0.0f };
            if (IsKeyDown(KEY_W)) { moveDir.x += flatForward.x; moveDir.z += flatForward.z; }
            if (IsKeyDown(KEY_S)) { moveDir.x -= flatForward.x; moveDir.z -= flatForward.z; }
            if (IsKeyDown(KEY_D)) { moveDir.x += right.x; moveDir.z += right.z; }
            if (IsKeyDown(KEY_A)) { moveDir.x -= right.x; moveDir.z -= right.z; }
            float len = sqrtf(moveDir.x * moveDir.x + moveDir.z * moveDir.z);
            if (len > 0.0001f) { moveDir.x /= len; moveDir.z /= len; }

            player.vel.x = moveDir.x * MOVE_SPEED;
            player.vel.z = moveDir.z * MOVE_SPEED;

            if (player.grounded && IsKeyPressed(KEY_SPACE)) {
                player.vel.y = JUMP_VELOCITY;
                player.grounded = false;
            }
            player.vel.y += GRAVITY * dt;
            if (player.vel.y < -40.0f) player.vel.y = -40.0f;

            Vector3 delta = { player.vel.x * dt, player.vel.y * dt, player.vel.z * dt };
            MoveAndCollide(*world, player, delta);

            if (player.pos.y < -30.0f) {
                player.pos = { (float)spawnX + 0.5f, (float)(groundY + 1), (float)spawnZ + 0.5f };
                player.vel = { 0.0f, 0.0f, 0.0f };
            }

            Vector3 eye = { player.pos.x, player.pos.y + EYE_HEIGHT, player.pos.z };

            int pcx = (int)floorf(player.pos.x / CHUNK_SIZE);
            int pcz = (int)floorf(player.pos.z / CHUNK_SIZE);
            for (int dx = -RENDER_DISTANCE; dx <= RENDER_DISTANCE; dx++)
                for (int dz = -RENDER_DISTANCE; dz <= RENDER_DISTANCE; dz++)
                    world->EnsureChunk(pcx + dx, pcz + dz);

            Vector3 breakPos{}, placePos{};
            bool hit = world->Raycast(eye, forward, REACH, breakPos, placePos);

            if (hit && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                world->SetBlock((int)breakPos.x, (int)breakPos.y, (int)breakPos.z, BlockType::Air);
            }
            if (hit && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
                if (!BoxOverlapsPlayer(player, placePos)) {
                    world->SetBlock((int)placePos.x, (int)placePos.y, (int)placePos.z, hotbar[selected]);
                }
            }

            for (int k = 0; k < 5; k++) {
                if (IsKeyPressed(KEY_ONE + k)) selected = k;
            }

            world->RebuildDirtyChunks(eye, RENDER_DISTANCE * CHUNK_SIZE);

            Camera3D camera{};
            camera.position = eye;
            camera.target = { eye.x + forward.x, eye.y + forward.y, eye.z + forward.z };
            camera.up = { 0.0f, 1.0f, 0.0f };
            camera.fovy = 70.0f;
            camera.projection = CAMERA_PERSPECTIVE;

            BeginDrawing();
            ClearBackground(Color{ 135, 206, 235, 255 });

            BeginMode3D(camera);
            world->Render(eye, RENDER_DISTANCE * CHUNK_SIZE);
            if (hit) {
                Vector3 center = { breakPos.x + 0.5f, breakPos.y + 0.5f, breakPos.z + 0.5f };
                DrawCubeWires(center, 1.02f, 1.02f, 1.02f, BLACK);
            }
            EndMode3D();

            DrawLine(screenWidth / 2 - 8, screenHeight / 2, screenWidth / 2 + 8, screenHeight / 2, WHITE);
            DrawLine(screenWidth / 2, screenHeight / 2 - 8, screenWidth / 2, screenHeight / 2 + 8, WHITE);

            for (int k = 0; k < 5; k++) {
                Color c = (k == selected) ? YELLOW : WHITE;
                DrawText(TextFormat("%d:%s", k + 1, hotbarNames[k]), 10 + k * 130, screenHeight - 30, 20, c);
            }
            DrawFPS(10, 10);

            EndDrawing();
        } else if (state == GameState::PAUSED) {
            bool skip = skipInput;
            if (skipInput) { skipInput = false; }
            Vector2 mousePos = GetMousePosition();
            float btnW = 240, btnH = 50;
            float btnX = screenWidth / 2.0f - btnW / 2.0f;
            float resumeBtnY = screenHeight / 2.0f - 10;
            float menuBtnY = resumeBtnY + 70;
            bool mouseOverResume = CheckCollisionPointRec(mousePos, { btnX, resumeBtnY, btnW, btnH });
            bool mouseOverMenu = CheckCollisionPointRec(mousePos, { btnX, menuBtnY, btnW, btnH });

            if (!skip && mouseOverResume && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                DisableCursor();
                skipInput = true;
                state = GameState::PLAYING;
            }
            if (!skip && mouseOverMenu && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                cursorLocked = true;
                delete world;
                world = nullptr;
                skipInput = true;
                state = GameState::MENU;
                continue;
            }

            Camera3D camera{};
            Vector3 eye = { player.pos.x, player.pos.y + EYE_HEIGHT, player.pos.z };
            Vector3 forward = {
                cosf(player.pitch) * sinf(player.yaw),
                sinf(player.pitch),
                cosf(player.pitch) * cosf(player.yaw)
            };
            camera.position = eye;
            camera.target = { eye.x + forward.x, eye.y + forward.y, eye.z + forward.z };
            camera.up = { 0.0f, 1.0f, 0.0f };
            camera.fovy = 70.0f;
            camera.projection = CAMERA_PERSPECTIVE;

            BeginDrawing();
            ClearBackground(Color{ 135, 206, 235, 255 });

            BeginMode3D(camera);
            world->Render(eye, RENDER_DISTANCE * CHUNK_SIZE);
            EndMode3D();

            DrawRectangle(0, 0, screenWidth, screenHeight, Color{ 0, 0, 0, 140 });

            const char* pauseTitle = "PAUSED";
            int pauseTitleSize = 50;
            int pauseTitleW = MeasureText(pauseTitle, pauseTitleSize);
            DrawText(pauseTitle, screenWidth / 2 - pauseTitleW / 2, screenHeight / 2 - 110, pauseTitleSize, WHITE);

            Color resumeBg = mouseOverResume ? Color{ 80, 160, 80, 255 } : Color{ 60, 120, 60, 255 };
            DrawRectangle((int)btnX, (int)resumeBtnY, (int)btnW, (int)btnH, resumeBg);
            const char* resumeText = "Resume";
            int resumeTextW = MeasureText(resumeText, 24);
            DrawText(resumeText, (int)(btnX + btnW / 2 - resumeTextW / 2), (int)(resumeBtnY + 13), 24, WHITE);

            Color menuBg = mouseOverMenu ? Color{ 160, 60, 60, 255 } : Color{ 120, 50, 50, 255 };
            DrawRectangle((int)btnX, (int)menuBtnY, (int)btnW, (int)btnH, menuBg);
            const char* menuText = "Quit to Menu";
            int menuTextW = MeasureText(menuText, 24);
            DrawText(menuText, (int)(btnX + btnW / 2 - menuTextW / 2), (int)(menuBtnY + 13), 24, WHITE);

            DrawFPS(10, 10);
            EndDrawing();
        }
    }

    delete world;
    CloseWindow();
    return 0;
}
