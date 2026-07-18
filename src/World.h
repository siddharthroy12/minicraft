#pragma once
#include <array>
#include <map>
#include <utility>
#include <vector>
#include "raylib.h"
#include "Block.h"
#include "Chunk.h"

constexpr int RENDER_DISTANCE = 8;

class World {
public:
    World(unsigned int seed = 0);
    ~World();

    void Save(const char* path) const;
    bool Load(const char* path);

    void RebuildDirtyChunks(Vector3 center, float radius);
    void Render(Vector3 center, float radius);
    void UpdateWater(float dt);
    void SimulateWater(float dt);

    BlockType GetBlock(int x, int y, int z) const;
    void SetBlock(int x, int y, int z, BlockType type);
    bool IsSolid(int x, int y, int z) const;
    int HeightAt(int x, int z) const;
    bool IsUnderwater(Vector3 pos) const;
    Color GetFogColor(Vector3 eye) const;

    // Bit 0x8 = falling (full-height waterfall column), bits 0x7 = flow
    // level (0 = source, 7 = weakest/farthest from source). 0 if not water.
    unsigned char GetWaterLevelRaw(int x, int y, int z) const;

    bool Raycast(Vector3 origin, Vector3 dir, float maxDist, Vector3& outBreak, Vector3& outPlace) const;

    Chunk* GetChunk(int cx, int cz);
    const Chunk* GetChunk(int cx, int cz) const;
    void EnsureChunk(int cx, int cz);

    unsigned int GetSeed() const { return seed; }
    Texture2D GetAtlas() const { return material.maps[MATERIAL_MAP_DIFFUSE].texture; }

    void SetPlayerState(Vector3 pos, float yaw, float pitch);
    Vector3 GetPlayerPos() const { return playerPos; }
    float GetPlayerYaw() const { return playerYaw; }
    float GetPlayerPitch() const { return playerPitch; }

private:
    std::map<std::pair<int,int>, Chunk> chunks;
    Material material{};
    Shader terrainShader{};
    int fogViewPosLoc = -1;
    int fogColorLoc = -1;
    int fogStartLoc = -1;
    int fogEndLoc = -1;
    unsigned int seed;
    Vector3 playerPos{};
    float playerYaw = 0.0f;
    float playerPitch = 0.0f;

    std::vector<std::array<int, 3>> pendingWaterUpdates;
    float waterTickAccum = 0.0f;

    void GenerateChunk(Chunk& chunk, int cx, int cz);
    void MarkChunkAndBordersDirty(int cx, int cz, int lx, int lz);
    void ScheduleWaterUpdate(int x, int y, int z);
    void ProcessWaterCell(int x, int y, int z);
};
