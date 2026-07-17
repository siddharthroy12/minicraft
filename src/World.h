#pragma once
#include <vector>
#include "raylib.h"
#include "Block.h"
#include "Chunk.h"

constexpr int WORLD_SIZE_X = 64;
constexpr int WORLD_SIZE_Z = 64;
constexpr int WORLD_HEIGHT = 48;
constexpr int CHUNK_SIZE = 16;
constexpr int CHUNKS_X = WORLD_SIZE_X / CHUNK_SIZE;
constexpr int CHUNKS_Z = WORLD_SIZE_Z / CHUNK_SIZE;

class World {
public:
    World();
    ~World();

    void Generate();
    void RebuildDirtyChunks();
    void Render();

    BlockType GetBlock(int x, int y, int z) const;
    void SetBlock(int x, int y, int z, BlockType type);
    bool IsSolid(int x, int y, int z) const;
    int HeightAt(int x, int z) const;

    bool Raycast(Vector3 origin, Vector3 dir, float maxDist, Vector3& outBreak, Vector3& outPlace) const;

private:
    std::vector<BlockType> blocks;
    std::vector<Chunk> chunks;
    Material material{};

    int Index(int x, int y, int z) const { return (y * WORLD_SIZE_Z + z) * WORLD_SIZE_X + x; }
    bool InBounds(int x, int y, int z) const;
    void MarkDirty(int x, int y, int z);
};
