#pragma once
#include "raylib.h"
#include "Block.h"

class World;

constexpr int CHUNK_SIZE = 16;
constexpr int CHUNK_HEIGHT = 48;

struct Chunk {
    static constexpr int SIZE = CHUNK_SIZE;
    static constexpr int HEIGHT = CHUNK_HEIGHT;

    BlockType blocks[SIZE * HEIGHT * SIZE]{};
    Mesh mesh{};
    bool hasMesh = false;
    bool dirty = true;
    bool generated = false;

    int Index(int lx, int ly, int lz) const { return (ly * SIZE + lz) * SIZE + lx; }
};

void BuildChunkMesh(World& world, int cx, int cz, Chunk& chunk);
