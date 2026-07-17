#include "World.h"
#include "Textures.h"
#include <cmath>
#include <algorithm>

World::World() {
    blocks.assign((size_t)WORLD_SIZE_X * WORLD_HEIGHT * WORLD_SIZE_Z, BlockType::Air);
    chunks.resize((size_t)CHUNKS_X * CHUNKS_Z);
    material = LoadMaterialDefault();
    UnloadTexture(material.maps[MATERIAL_MAP_DIFFUSE].texture);
    material.maps[MATERIAL_MAP_DIFFUSE].texture = LoadBlockAtlas();
}

World::~World() {
    for (auto& c : chunks) {
        if (c.hasMesh) UnloadMesh(c.mesh);
    }
    UnloadMaterial(material);
}

bool World::InBounds(int x, int y, int z) const {
    return x >= 0 && x < WORLD_SIZE_X && y >= 0 && y < WORLD_HEIGHT && z >= 0 && z < WORLD_SIZE_Z;
}

BlockType World::GetBlock(int x, int y, int z) const {
    if (!InBounds(x, y, z)) return BlockType::Air;
    return blocks[Index(x, y, z)];
}

bool World::IsSolid(int x, int y, int z) const {
    return GetBlock(x, y, z) != BlockType::Air;
}

void World::SetBlock(int x, int y, int z, BlockType type) {
    if (!InBounds(x, y, z)) return;
    blocks[Index(x, y, z)] = type;
    MarkDirty(x, y, z);
}

void World::MarkDirty(int x, int y, int z) {
    int cx = x / CHUNK_SIZE;
    int cz = z / CHUNK_SIZE;
    auto mark = [&](int ccx, int ccz) {
        if (ccx >= 0 && ccx < CHUNKS_X && ccz >= 0 && ccz < CHUNKS_Z) {
            chunks[ccz * CHUNKS_X + ccx].dirty = true;
        }
    };
    mark(cx, cz);
    int lx = x % CHUNK_SIZE;
    int lz = z % CHUNK_SIZE;
    if (lx == 0) mark(cx - 1, cz);
    if (lx == CHUNK_SIZE - 1) mark(cx + 1, cz);
    if (lz == 0) mark(cx, cz - 1);
    if (lz == CHUNK_SIZE - 1) mark(cx, cz + 1);
}

int World::HeightAt(int x, int z) const {
    for (int y = WORLD_HEIGHT - 1; y >= 0; y--) {
        if (GetBlock(x, y, z) != BlockType::Air) return y;
    }
    return 0;
}

void World::Generate() {
    Image noise = GenImagePerlinNoise(WORLD_SIZE_X, WORLD_SIZE_Z, 0, 0, 3.0f);
    Color* pixels = LoadImageColors(noise);

    const int baseHeight = 18;
    const int variation = 10;

    for (int x = 0; x < WORLD_SIZE_X; x++) {
        for (int z = 0; z < WORLD_SIZE_Z; z++) {
            float n = pixels[z * WORLD_SIZE_X + x].r / 255.0f;
            int h = baseHeight + (int)((n - 0.5f) * 2.0f * variation);
            h = std::max(2, std::min(h, WORLD_HEIGHT - 12));
            for (int y = 0; y <= h; y++) {
                BlockType t;
                if (y == h) t = BlockType::Grass;
                else if (y >= h - 3) t = BlockType::Dirt;
                else t = BlockType::Stone;
                blocks[Index(x, y, z)] = t;
            }
        }
    }

    UnloadImageColors(pixels);
    UnloadImage(noise);

    for (int x = 3; x < WORLD_SIZE_X - 3; x++) {
        for (int z = 3; z < WORLD_SIZE_Z - 3; z++) {
            int h = HeightAt(x, z);
            if (blocks[Index(x, h, z)] != BlockType::Grass) continue;
            if (GetRandomValue(0, 999) >= 15) continue;

            int trunkHeight = GetRandomValue(4, 5);
            for (int ty = 1; ty <= trunkHeight; ty++) {
                blocks[Index(x, h + ty, z)] = BlockType::Wood;
            }
            int trunkTop = h + trunkHeight;
            for (int ly = 0; ly <= 2; ly++) {
                int ry = trunkTop - 2 + ly;
                int radius = (ly == 2) ? 1 : 2;
                for (int dx = -radius; dx <= radius; dx++) {
                    for (int dz = -radius; dz <= radius; dz++) {
                        if (radius == 2 && std::abs(dx) == 2 && std::abs(dz) == 2) continue;
                        int bx = x + dx, bz = z + dz;
                        if (!InBounds(bx, ry, bz)) continue;
                        if (blocks[Index(bx, ry, bz)] == BlockType::Air) {
                            blocks[Index(bx, ry, bz)] = BlockType::Leaves;
                        }
                    }
                }
            }
        }
    }

    for (auto& c : chunks) c.dirty = true;
    RebuildDirtyChunks();
}

void World::RebuildDirtyChunks() {
    for (int cz = 0; cz < CHUNKS_Z; cz++) {
        for (int cx = 0; cx < CHUNKS_X; cx++) {
            Chunk& chunk = chunks[cz * CHUNKS_X + cx];
            if (chunk.dirty) {
                BuildChunkMesh(*this, cx, cz, chunk);
                chunk.dirty = false;
            }
        }
    }
}

void World::Render() {
    for (auto& c : chunks) {
        if (c.hasMesh) DrawMesh(c.mesh, material, MatrixIdentity());
    }
}

bool World::Raycast(Vector3 origin, Vector3 dir, float maxDist, Vector3& outBreak, Vector3& outPlace) const {
    const float step = 0.05f;
    Vector3 prev = origin;
    int steps = (int)(maxDist / step);
    for (int i = 0; i < steps; i++) {
        float t = i * step;
        Vector3 pos = { origin.x + dir.x * t, origin.y + dir.y * t, origin.z + dir.z * t };
        int bx = (int)floorf(pos.x), by = (int)floorf(pos.y), bz = (int)floorf(pos.z);
        if (IsSolid(bx, by, bz)) {
            outBreak = { (float)bx, (float)by, (float)bz };
            int pbx = (int)floorf(prev.x), pby = (int)floorf(prev.y), pbz = (int)floorf(prev.z);
            outPlace = { (float)pbx, (float)pby, (float)pbz };
            return true;
        }
        prev = pos;
    }
    return false;
}
