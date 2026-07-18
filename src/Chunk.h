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
    // Valid only where blocks[i] == Water: bit 0x8 = falling (full-height
    // waterfall column), bits 0x7 = flow level (0 = source, 7 = weakest).
    // Zero-initialized, which correctly reads as "source" for generated water.
    unsigned char waterLevel[SIZE * HEIGHT * SIZE]{};
    Mesh mesh{};
    bool hasMesh = false;
    Mesh waterMesh{};
    bool hasWaterMesh = false;
    bool dirty = true;
    bool generated = false;

    int Index(int lx, int ly, int lz) const { return (ly * SIZE + lz) * SIZE + lx; }
};

void BuildChunkMesh(World& world, int cx, int cz, Chunk& chunk);
