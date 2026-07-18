#include "raylib.h"
#include "rlgl.h"
#include "World.h"
#include "Textures.h"
#include <cmath>
#include <ctime>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <filesystem>

constexpr float PLAYER_HALF_WIDTH = 0.3f;
constexpr float PLAYER_HEIGHT = 1.8f;
constexpr float EYE_HEIGHT = 1.6f;
constexpr float GRAVITY = -20.0f;
constexpr float JUMP_VELOCITY = 8.0f;
constexpr float MOVE_SPEED = 5.0f;
constexpr float MOUSE_SENSITIVITY = 0.003f;
constexpr float REACH = 6.0f;
constexpr float THIRD_PERSON_DISTANCE = 5.0f;

enum class GameState { MENU, CREATE_WORLD, LOAD_WORLD, PLAYING, PAUSED };

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

// Steps a camera back along `back` from `eye`, stopping just before it would
// enter a solid block so the third-person view doesn't clip through walls.
static Vector3 ThirdPersonCameraPos(World& world, Vector3 eye, Vector3 back, float maxDist) {
    const float step = 0.1f;
    float dist = 0.0f;
    while (dist < maxDist) {
        float next = dist + step;
        Vector3 p = { eye.x + back.x * next, eye.y + back.y * next, eye.z + back.z * next };
        if (world.IsSolid((int)floorf(p.x), (int)floorf(p.y), (int)floorf(p.z))) break;
        dist = next;
    }
    return { eye.x + back.x * dist, eye.y + back.y * dist, eye.z + back.z * dist };
}

static void DrawPlayerModel(const Player& p) {
    constexpr float legHeight = 0.8f, torsoHeight = 0.6f, headSize = 0.4f;
    constexpr Color pants = { 60, 60, 140, 255 };
    constexpr Color shirt = { 40, 130, 180, 255 };
    constexpr Color skin = { 235, 190, 150, 255 };

    rlPushMatrix();
    rlTranslatef(p.pos.x, p.pos.y, p.pos.z);
    rlRotatef(-p.yaw * RAD2DEG, 0, 1, 0);

    Vector3 legsCenter = { 0, legHeight / 2, 0 };
    DrawCube(legsCenter, PLAYER_HALF_WIDTH * 1.8f, legHeight, PLAYER_HALF_WIDTH * 1.8f, pants);

    Vector3 torsoCenter = { 0, legHeight + torsoHeight / 2, 0 };
    DrawCube(torsoCenter, PLAYER_HALF_WIDTH * 2.0f, torsoHeight, PLAYER_HALF_WIDTH * 2.0f, shirt);

    Vector3 headCenter = { 0, legHeight + torsoHeight + headSize / 2, 0 };
    DrawCube(headCenter, headSize, headSize, headSize, skin);
    DrawCubeWires(headCenter, headSize, headSize, headSize, BLACK);

    rlPopMatrix();
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

static std::vector<std::string> ListSavedWorlds() {
    std::vector<std::string> worlds;
    namespace fs = std::filesystem;
    if (fs::exists("saves") && fs::is_directory("saves")) {
        for (auto& entry : fs::directory_iterator("saves")) {
            if (entry.is_regular_file() && entry.path().extension() == ".world") {
                worlds.push_back(entry.path().stem().string());
            }
        }
    }
    return worlds;
}

static void StartPlaying(World*& world, Player& player, int& spawnX, int& spawnZ, int& groundY, int& selected, bool& cursorLocked, bool& skipInput, GameState& state, const std::string& worldName, bool isLoaded) {
    for (int dx = -2; dx <= 2; dx++)
        for (int dz = -2; dz <= 2; dz++)
            world->EnsureChunk(dx, dz);
    spawnX = 0;
    spawnZ = 0;
    groundY = world->HeightAt(spawnX, spawnZ);
    player = {};
    if (isLoaded) {
        player.pos = world->GetPlayerPos();
        player.yaw = world->GetPlayerYaw();
        player.pitch = world->GetPlayerPitch();
        if (player.pos.y < -10.0f) {
            player.pos = { (float)spawnX + 0.5f, (float)(groundY + 1), (float)spawnZ + 0.5f };
        } else if (CheckCollision(*world, player.pos)) {
            // Stale saved position embedded in terrain (e.g. files written
            // before the player was ever placed): surface at the same x,z.
            int top = world->HeightAt((int)floorf(player.pos.x), (int)floorf(player.pos.z));
            player.pos.y = (float)(top + 1);
        }
    } else {
        player.pos = { (float)spawnX + 0.5f, (float)(groundY + 1), (float)spawnZ + 0.5f };
    }
    selected = 0;
    DisableCursor();
    cursorLocked = false;
    skipInput = true;
    state = GameState::PLAYING;
}

int main() {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(1280, 720, "Minicraft");
    SetExitKey(KEY_NULL);
    rlDisableBackfaceCulling();
    SetRandomSeed((unsigned int)time(nullptr));

    // Build pixel-perfect font from monogram bitmap JSON
    std::map<int, std::vector<int>> glyphData;
    {
        FILE* f = fopen("assets/monogram-bitmap.json", "r");
        if (f) {
            char line[512];
            while (fgets(line, sizeof(line), f)) {
                char* q1 = strchr(line, '"');
                if (!q1) continue;
                char* q2 = strchr(q1 + 1, '"');
                if (!q2) continue;
                std::string key(q1 + 1, q2);
                char* bracket = strchr(q2, '[');
                if (!bracket) continue;
                std::vector<int> rows;
                char* p = bracket + 1;
                while (*p && *p != ']') {
                    if (isdigit(*p) || *p == '-') {
                        rows.push_back((int)strtol(p, &p, 10));
                    } else {
                        p++;
                    }
                }
                if (key.size() == 1 && rows.size() == 12) {
                    glyphData[(unsigned char)key[0]] = rows;
                }
            }
            fclose(f);
        }
    }

    const int cellW = 8, cellH = 12;
    const int glyphW = 5, glyphH = 10;
    const int cols = 16;
    int numGlyphs = 95; // ASCII 32..126
    int numRows = (numGlyphs + cols - 1) / cols;
    int atlasW = cols * cellW;
    int atlasH = numRows * cellH;
    Image atlasImg = GenImageColor(atlasW, atlasH, BLANK);
    for (int ch = 32; ch <= 126; ch++) {
        auto it = glyphData.find(ch);
        if (it == glyphData.end()) continue;
        int idx = ch - 32;
        int col = idx % cols;
        int row = idx / cols;
        int ox = col * cellW + 1;
        int oy = row * cellH + 1;
        auto& data = it->second;
        for (int y = 0; y < glyphH; y++) {
            int rowData = data[y + 2];
            for (int x = 0; x < glyphW; x++) {
                if (rowData & (1 << x)) {
                    ImageDrawPixel(&atlasImg, ox + x, oy + y, WHITE);
                }
            }
        }
    }

    // Build the Font manually instead of via LoadFontFromImage: that function
    // auto-detects glyph rectangles by scanning for the first non-key pixel,
    // which breaks down when glyphs (e.g. space, quotes, punctuation) have
    // blank rows/columns inside their cell. Since we already know the exact
    // grid layout, place the glyph rects ourselves.
    Font fonts[1];
    Font& font = fonts[0];
    font.baseSize = glyphH;
    font.glyphCount = numGlyphs;
    font.glyphPadding = 0;
    font.texture = LoadTextureFromImage(atlasImg);
    UnloadImage(atlasImg);
    SetTextureFilter(font.texture, TEXTURE_FILTER_POINT);
    font.recs = (Rectangle*)RL_MALLOC(numGlyphs * sizeof(Rectangle));
    font.glyphs = (GlyphInfo*)RL_MALLOC(numGlyphs * sizeof(GlyphInfo));
    for (int i = 0; i < numGlyphs; i++) {
        int col = i % cols;
        int row = i / cols;
        font.recs[i] = { (float)(col * cellW + 1), (float)(row * cellH + 1), (float)glyphW, (float)glyphH };
        font.glyphs[i] = { 0 };
        font.glyphs[i].value = 32 + i;
        font.glyphs[i].advanceX = cellW;
    }

    // Snap to an integer multiple of the glyph's native pixel size so every
    // source pixel maps to a whole number of screen pixels (point filtering
    // alone doesn't prevent uneven pixel sizes at fractional scales).
    auto PixelPerfectSize = [&](int fontSize) -> float {
        int scale = (int)roundf((float)fontSize / fonts[0].baseSize);
        if (scale < 1) scale = 1;
        return (float)(scale * fonts[0].baseSize);
    };
    auto DrawGUI = [&](const char* text, int x, int y, int fontSize, Color color) {
        DrawTextEx(fonts[0], text, { (float)x, (float)y }, PixelPerfectSize(fontSize), 0.0f, color);
    };
    auto MeasureGUI = [&](const char* text, int fontSize) -> int {
        return (int)MeasureTextEx(fonts[0], text, PixelPerfectSize(fontSize), 0.0f).x;
    };

    GameState state = GameState::MENU;
    bool cursorLocked = false;
    bool skipInput = false;
    bool thirdPerson = false;

    World* world = nullptr;
    Player player{};
    int spawnX = 0;
    int spawnZ = 0;
    int groundY = 0;

    BlockType hotbar[6] = { BlockType::Dirt, BlockType::Stone, BlockType::Wood, BlockType::Leaves, BlockType::Sand, BlockType::Water };
    const char* hotbarNames[6] = { "Dirt", "Stone", "Wood", "Leaves", "Sand", "Water" };
    int selected = 0;

    char worldNameInput[64] = "";
    int worldNameLen = 0;
    std::vector<std::string> savedWorlds;
    bool worldsLoaded = false;
    std::string currentWorldName;

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        if (dt > 0.05f) dt = 0.05f;

        if (IsKeyPressed(KEY_F11)) ToggleFullscreen();

        if (state == GameState::MENU) {
            bool skip = skipInput;
            if (skipInput) { skipInput = false; }
            if (!cursorLocked) {
                EnableCursor();
                cursorLocked = true;
            }

            Vector2 mousePos = GetMousePosition();
            float btnW = 240, btnH = 50;
            float btnX = GetScreenWidth() / 2.0f - btnW / 2.0f;
            float newBtnY = GetScreenHeight() / 2.0f - 45;
            float loadBtnY = newBtnY + 70;
            float quitBtnY = loadBtnY + 70;
            bool mouseOverNew = CheckCollisionPointRec(mousePos, { btnX, newBtnY, btnW, btnH });
            bool mouseOverLoad = CheckCollisionPointRec(mousePos, { btnX, loadBtnY, btnW, btnH });
            bool mouseOverQuit = CheckCollisionPointRec(mousePos, { btnX, quitBtnY, btnW, btnH });

            if (!skip && mouseOverNew && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                worldNameInput[0] = '\0';
                worldNameLen = 0;
                skipInput = true;
                state = GameState::CREATE_WORLD;
            }
            if (!skip && mouseOverLoad && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                savedWorlds = ListSavedWorlds();
                worldsLoaded = true;
                skipInput = true;
                state = GameState::LOAD_WORLD;
            }
            if (!skip && mouseOverQuit && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) break;

            BeginDrawing();
            ClearBackground(Color{ 30, 30, 30, 255 });

            const char* title = "MINICRAFT";
            int titleSize = 64;
            int titleWidth = MeasureGUI(title, titleSize);
            DrawGUI(title, GetScreenWidth() / 2 - titleWidth / 2, GetScreenHeight() / 2 - 150, titleSize, WHITE);

            const char* subtitle = "A Voxel Sandbox";
            int subSize = 16;
            int subWidth = MeasureGUI(subtitle, subSize);
            DrawGUI(subtitle, GetScreenWidth() / 2 - subWidth / 2, GetScreenHeight() / 2 - 80, subSize, GRAY);

            Color newBg = mouseOverNew ? Color{ 80, 160, 80, 255 } : Color{ 60, 120, 60, 255 };
            DrawRectangle((int)btnX, (int)newBtnY, (int)btnW, (int)btnH, newBg);
            const char* newText = "New World";
            int newTextW = MeasureGUI(newText, 24);
            DrawGUI(newText, (int)(btnX + btnW / 2 - newTextW / 2), (int)(newBtnY + 13), 24, WHITE);

            Color loadBg = mouseOverLoad ? Color{ 80, 130, 180, 255 } : Color{ 50, 100, 150, 255 };
            DrawRectangle((int)btnX, (int)loadBtnY, (int)btnW, (int)btnH, loadBg);
            const char* loadText = "Load World";
            int loadTextW = MeasureGUI(loadText, 24);
            DrawGUI(loadText, (int)(btnX + btnW / 2 - loadTextW / 2), (int)(loadBtnY + 13), 24, WHITE);

            Color quitBg = mouseOverQuit ? Color{ 160, 60, 60, 255 } : Color{ 120, 50, 50, 255 };
            DrawRectangle((int)btnX, (int)quitBtnY, (int)btnW, (int)btnH, quitBg);
            const char* quitText = "Quit";
            int quitTextW = MeasureGUI(quitText, 24);
            DrawGUI(quitText, (int)(btnX + btnW / 2 - quitTextW / 2), (int)(quitBtnY + 13), 24, WHITE);

            DrawFPS(10, 10);
            EndDrawing();
        } else if (state == GameState::CREATE_WORLD) {
            bool skip = skipInput;
            if (skipInput) { skipInput = false; }

            if (!skip) {
                if (IsKeyPressed(KEY_BACKSPACE) && worldNameLen > 0) {
                    worldNameLen--;
                    worldNameInput[worldNameLen] = '\0';
                }
                int ch = GetCharPressed();
                while (ch > 0 && worldNameLen < 30) {
                    if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == ' ') {
                        worldNameInput[worldNameLen++] = (char)ch;
                        worldNameInput[worldNameLen] = '\0';
                    }
                    ch = GetCharPressed();
                }
            }

            Vector2 mousePos = GetMousePosition();
            float btnW = 240, btnH = 50;
            float btnX = GetScreenWidth() / 2.0f - btnW / 2.0f;
            float createBtnY = GetScreenHeight() / 2.0f + 40;
            float backBtnY = createBtnY + 70;
            bool mouseOverCreate = CheckCollisionPointRec(mousePos, { btnX, createBtnY, btnW, btnH }) && worldNameLen > 0;
            bool mouseOverBack = CheckCollisionPointRec(mousePos, { btnX, backBtnY, btnW, btnH });

            if (!skip && mouseOverCreate && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                namespace fs = std::filesystem;
                if (!fs::exists("saves")) fs::create_directories("saves");
                std::string path = "saves/" + std::string(worldNameInput) + ".world";
                currentWorldName = worldNameInput;
                world = new World();
                // Place the player before the initial save, so the file
                // never holds a bogus (0,0,0) position if the player later
                // quits without saving.
                StartPlaying(world, player, spawnX, spawnZ, groundY, selected, cursorLocked, skipInput, state, currentWorldName, false);
                world->SetPlayerState(player.pos, player.yaw, player.pitch);
                world->Save(path.c_str());
            }
            if (!skip && mouseOverBack && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                skipInput = true;
                state = GameState::MENU;
            }
            if (!skip && IsKeyPressed(KEY_ESCAPE)) {
                skipInput = true;
                state = GameState::MENU;
            }

            BeginDrawing();
            ClearBackground(Color{ 30, 30, 30, 255 });

            const char* title = "NEW WORLD";
            int titleSize = 48;
            int titleWidth = MeasureGUI(title, titleSize);
            DrawGUI(title, GetScreenWidth() / 2 - titleWidth / 2, GetScreenHeight() / 2 - 140, titleSize, WHITE);

            const char* label = "World Name:";
            int labelW = MeasureGUI(label, 16);
            DrawGUI(label, GetScreenWidth() / 2 - labelW / 2, GetScreenHeight() / 2 - 70, 16, GRAY);

            float inputW = 300, inputH = 40;
            float inputX = GetScreenWidth() / 2.0f - inputW / 2.0f;
            float inputY = GetScreenHeight() / 2.0f - 35;
            DrawRectangle((int)inputX, (int)inputY, (int)inputW, (int)inputH, Color{ 60, 60, 60, 255 });
            DrawRectangleLines((int)inputX, (int)inputY, (int)inputW, (int)inputH, Color{ 120, 120, 120, 255 });

            const char* displayText = worldNameLen > 0 ? worldNameInput : "Enter name...";
            Color textColor = worldNameLen > 0 ? WHITE : GRAY;
            DrawGUI(displayText, (int)inputX + 10, (int)inputY + 10, 16, textColor);

            if (((int)(GetTime() * 2.0f) % 2 == 0) && worldNameLen < 30) {
                int textW = MeasureGUI(displayText, 16);
                DrawGUI("_", (int)inputX + 10 + textW + 2, (int)inputY + 10, 16, WHITE);
            }

            Color createBg = mouseOverCreate ? Color{ 80, 160, 80, 255 } : Color{ 60, 120, 60, 255 };
            if (worldNameLen == 0) createBg = Color{ 40, 40, 40, 255 };
            DrawRectangle((int)btnX, (int)createBtnY, (int)btnW, (int)btnH, createBg);
            const char* createText = "Create";
            int createTextW = MeasureGUI(createText, 24);
            DrawGUI(createText, (int)(btnX + btnW / 2 - createTextW / 2), (int)(createBtnY + 13), 24, worldNameLen > 0 ? WHITE : GRAY);

            Color backBg = mouseOverBack ? Color{ 100, 100, 100, 255 } : Color{ 70, 70, 70, 255 };
            DrawRectangle((int)btnX, (int)backBtnY, (int)btnW, (int)btnH, backBg);
            const char* backText = "Back";
            int backTextW = MeasureGUI(backText, 24);
            DrawGUI(backText, (int)(btnX + btnW / 2 - backTextW / 2), (int)(backBtnY + 13), 24, WHITE);

            DrawFPS(10, 10);
            EndDrawing();
        } else if (state == GameState::LOAD_WORLD) {
            bool skip = skipInput;
            if (skipInput) { skipInput = false; }

            Vector2 mousePos = GetMousePosition();
            float btnW = 240, btnH = 50;
            float btnX = GetScreenWidth() / 2.0f - btnW / 2.0f;
            float backBtnY = GetScreenHeight() - 80;

            if (!skip && IsKeyPressed(KEY_ESCAPE)) {
                skipInput = true;
                state = GameState::MENU;
            }

            bool clickedWorld = false;
            if (!skip && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                for (int i = 0; i < (int)savedWorlds.size(); i++) {
                    float entryY = 120 + i * 55;
                    if (entryY + 45 > backBtnY - 10) break;
                    if (CheckCollisionPointRec(mousePos, { btnX, entryY, btnW, 45 })) {
                        namespace fs = std::filesystem;
                        std::string path = "saves/" + savedWorlds[i] + ".world";
                        currentWorldName = savedWorlds[i];
                        world = new World();
                        if (world->Load(path.c_str())) {
                            StartPlaying(world, player, spawnX, spawnZ, groundY, selected, cursorLocked, skipInput, state, currentWorldName, true);
                            clickedWorld = true;
                        } else {
                            delete world;
                            world = nullptr;
                        }
                        break;
                    }
                }
            }

            bool mouseOverBack = CheckCollisionPointRec(mousePos, { btnX, backBtnY, btnW, btnH });
            if (!skip && !clickedWorld && mouseOverBack && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                skipInput = true;
                state = GameState::MENU;
            }

            BeginDrawing();
            ClearBackground(Color{ 30, 30, 30, 255 });

            const char* title = "LOAD WORLD";
            int titleSize = 48;
            int titleWidth = MeasureGUI(title, titleSize);
            DrawGUI(title, GetScreenWidth() / 2 - titleWidth / 2, 40, titleSize, WHITE);

            if (savedWorlds.empty()) {
                const char* emptyMsg = "No saved worlds found";
                int emptyW = MeasureGUI(emptyMsg, 16);
                DrawGUI(emptyMsg, GetScreenWidth() / 2 - emptyW / 2, GetScreenHeight() / 2 - 10, 16, GRAY);
            } else {
                for (int i = 0; i < (int)savedWorlds.size(); i++) {
                    float entryY = 120 + i * 55;
                    if (entryY + 45 > backBtnY - 10) break;
                    bool mouseOver = CheckCollisionPointRec(mousePos, { btnX, entryY, btnW, 45 });
                    Color entryBg = mouseOver ? Color{ 70, 110, 160, 255 } : Color{ 50, 50, 60, 255 };
                    DrawRectangle((int)btnX, (int)entryY, (int)btnW, 45, entryBg);
                    DrawGUI(savedWorlds[i].c_str(), (int)btnX + 15, (int)entryY + 12, 16, WHITE);
                }
            }

            Color backBg = mouseOverBack ? Color{ 100, 100, 100, 255 } : Color{ 70, 70, 70, 255 };
            DrawRectangle((int)btnX, (int)backBtnY, (int)btnW, (int)btnH, backBg);
            const char* backText = "Back";
            int backTextW = MeasureGUI(backText, 24);
            DrawGUI(backText, (int)(btnX + btnW / 2 - backTextW / 2), (int)(backBtnY + 13), 24, WHITE);

            DrawFPS(10, 10);
            EndDrawing();
        } else if (state == GameState::PLAYING) {
            if (IsKeyPressed(KEY_ESCAPE)) {
                EnableCursor();
                skipInput = true;
                state = GameState::PAUSED;
                continue;
            }

            if (IsKeyPressed(KEY_F5)) thirdPerson = !thirdPerson;

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

            for (int k = 0; k < 6; k++) {
                if (IsKeyPressed(KEY_ONE + k)) selected = k;
            }
            float wheel = GetMouseWheelMove();
            if (wheel < 0.0f) selected = (selected + 1) % 6;
            else if (wheel > 0.0f) selected = (selected + 5) % 6;

            world->RebuildDirtyChunks(eye, RENDER_DISTANCE * CHUNK_SIZE);
            world->UpdateWater(dt);
            world->SimulateWater(dt);

            Camera3D camera{};
            if (thirdPerson) {
                Vector3 back = { -forward.x, -forward.y, -forward.z };
                camera.position = ThirdPersonCameraPos(*world, eye, back, THIRD_PERSON_DISTANCE);
            } else {
                camera.position = eye;
            }
            camera.target = { eye.x + forward.x, eye.y + forward.y, eye.z + forward.z };
            camera.up = { 0.0f, 1.0f, 0.0f };
            camera.fovy = 70.0f;
            camera.projection = CAMERA_PERSPECTIVE;

            BeginDrawing();
            ClearBackground(world->GetFogColor(eye));

            BeginMode3D(camera);
            world->Render(eye, RENDER_DISTANCE * CHUNK_SIZE);
            if (thirdPerson) DrawPlayerModel(player);
            if (hit) {
                Vector3 center = { breakPos.x + 0.5f, breakPos.y + 0.5f, breakPos.z + 0.5f };
                DrawCubeWires(center, 1.02f, 1.02f, 1.02f, BLACK);
            }
            EndMode3D();

            DrawLine(GetScreenWidth() / 2 - 8, GetScreenHeight() / 2, GetScreenWidth() / 2 + 8, GetScreenHeight() / 2, WHITE);
            DrawLine(GetScreenWidth() / 2, GetScreenHeight() / 2 - 8, GetScreenWidth() / 2, GetScreenHeight() / 2 + 8, WHITE);

            {
                const int slotSize = 48, slotPad = 4;
                int barW = 6 * slotSize + 5 * slotPad;
                int barX = GetScreenWidth() / 2 - barW / 2;
                int barY = GetScreenHeight() - slotSize - 14;
                Texture2D atlas = world->GetAtlas();
                for (int k = 0; k < 6; k++) {
                    int sx = barX + k * (slotSize + slotPad);
                    DrawRectangle(sx, barY, slotSize, slotSize, Color{ 0, 0, 0, 130 });
                    Rectangle uv = GetTileUV(hotbar[k], Face::PosZ);
                    Rectangle src = { uv.x * atlas.width, uv.y * atlas.height, uv.width * atlas.width, uv.height * atlas.height };
                    Rectangle dst = { (float)(sx + 7), (float)(barY + 7), (float)(slotSize - 14), (float)(slotSize - 14) };
                    DrawTexturePro(atlas, src, dst, { 0, 0 }, 0.0f, WHITE);
                    if (k == selected) {
                        DrawRectangleLinesEx({ (float)(sx - 2), (float)(barY - 2), (float)(slotSize + 4), (float)(slotSize + 4) }, 3, WHITE);
                    } else {
                        DrawRectangleLinesEx({ (float)sx, (float)barY, (float)slotSize, (float)slotSize }, 1, Color{ 190, 190, 190, 160 });
                    }
                    DrawGUI(TextFormat("%d", k + 1), sx + 3, barY + 1, 12, Color{ 255, 255, 255, 210 });
                }
                const char* selName = hotbarNames[selected];
                int selNameW = MeasureGUI(selName, 16);
                DrawGUI(selName, GetScreenWidth() / 2 - selNameW / 2, barY - 26, 16, WHITE);
            }
            DrawFPS(10, 10);

            EndDrawing();
        } else if (state == GameState::PAUSED) {
            bool skip = skipInput;
            if (skipInput) { skipInput = false; }
            Vector2 mousePos = GetMousePosition();
            float btnW = 240, btnH = 50;
            float btnX = GetScreenWidth() / 2.0f - btnW / 2.0f;
            float resumeBtnY = GetScreenHeight() / 2.0f - 45;
            float saveQuitBtnY = resumeBtnY + 70;
            float menuBtnY = saveQuitBtnY + 70;
            bool mouseOverResume = CheckCollisionPointRec(mousePos, { btnX, resumeBtnY, btnW, btnH });
            bool mouseOverSaveQuit = CheckCollisionPointRec(mousePos, { btnX, saveQuitBtnY, btnW, btnH });
            bool mouseOverMenu = CheckCollisionPointRec(mousePos, { btnX, menuBtnY, btnW, btnH });

            if (!skip && mouseOverResume && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                DisableCursor();
                skipInput = true;
                state = GameState::PLAYING;
            }
            if (!skip && mouseOverSaveQuit && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                namespace fs = std::filesystem;
                if (!fs::exists("saves")) fs::create_directories("saves");
                std::string path = "saves/" + currentWorldName + ".world";
                world->SetPlayerState(player.pos, player.yaw, player.pitch);
                world->Save(path.c_str());
                cursorLocked = true;
                delete world;
                world = nullptr;
                skipInput = true;
                state = GameState::MENU;
                continue;
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
            if (thirdPerson) {
                Vector3 back = { -forward.x, -forward.y, -forward.z };
                camera.position = ThirdPersonCameraPos(*world, eye, back, THIRD_PERSON_DISTANCE);
            } else {
                camera.position = eye;
            }
            camera.target = { eye.x + forward.x, eye.y + forward.y, eye.z + forward.z };
            camera.up = { 0.0f, 1.0f, 0.0f };
            camera.fovy = 70.0f;
            camera.projection = CAMERA_PERSPECTIVE;

            BeginDrawing();
            ClearBackground(world->GetFogColor(eye));

            BeginMode3D(camera);
            world->Render(eye, RENDER_DISTANCE * CHUNK_SIZE);
            if (thirdPerson) DrawPlayerModel(player);
            EndMode3D();

            DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Color{ 0, 0, 0, 140 });

            const char* pauseTitle = "PAUSED";
            int pauseTitleSize = 48;
            int pauseTitleW = MeasureGUI(pauseTitle, pauseTitleSize);
            DrawGUI(pauseTitle, GetScreenWidth() / 2 - pauseTitleW / 2, GetScreenHeight() / 2 - 130, pauseTitleSize, WHITE);

            Color resumeBg = mouseOverResume ? Color{ 80, 160, 80, 255 } : Color{ 60, 120, 60, 255 };
            DrawRectangle((int)btnX, (int)resumeBtnY, (int)btnW, (int)btnH, resumeBg);
            const char* resumeText = "Resume";
            int resumeTextW = MeasureGUI(resumeText, 24);
            DrawGUI(resumeText, (int)(btnX + btnW / 2 - resumeTextW / 2), (int)(resumeBtnY + 13), 24, WHITE);

            Color saveQuitBg = mouseOverSaveQuit ? Color{ 80, 130, 180, 255 } : Color{ 50, 100, 150, 255 };
            DrawRectangle((int)btnX, (int)saveQuitBtnY, (int)btnW, (int)btnH, saveQuitBg);
            const char* saveQuitText = "Save & Quit";
            int saveQuitTextW = MeasureGUI(saveQuitText, 24);
            DrawGUI(saveQuitText, (int)(btnX + btnW / 2 - saveQuitTextW / 2), (int)(saveQuitBtnY + 13), 24, WHITE);

            Color menuBg = mouseOverMenu ? Color{ 160, 60, 60, 255 } : Color{ 120, 50, 50, 255 };
            DrawRectangle((int)btnX, (int)menuBtnY, (int)btnW, (int)btnH, menuBg);
            const char* menuText = "Quit to Menu";
            int menuTextW = MeasureGUI(menuText, 24);
            DrawGUI(menuText, (int)(btnX + btnW / 2 - menuTextW / 2), (int)(menuBtnY + 13), 24, WHITE);

            DrawFPS(10, 10);
            EndDrawing();
        }
    }

    delete world;
    UnloadFont(fonts[0]);
    CloseWindow();
    return 0;
}
