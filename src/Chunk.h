#pragma once
#include "raylib.h"

class World;

struct Chunk {
    Mesh mesh{};
    bool hasMesh = false;
    bool dirty = true;
};

void BuildChunkMesh(World& world, int cx, int cz, Chunk& chunk);
