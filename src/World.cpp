#include "World.h"
#include "Textures.h"
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>

static unsigned int HashCoord(int x, int y, unsigned int seed) {
    unsigned int h = (unsigned int)(x * 374761393 + y * 668265263 + seed);
    h = (h ^ (h >> 13)) * 1274126177;
    return h;
}

static float ValueNoise(float x, float y, unsigned int seed) {
    int ix = (int)floorf(x);
    int iy = (int)floorf(y);
    float fx = x - ix;
    float fy = y - iy;

    float ux = fx * fx * (3.0f - 2.0f * fx);
    float uy = fy * fy * (3.0f - 2.0f * fy);

    float a = (HashCoord(ix, iy, seed) & 0xffff) / 65535.0f;
    float b = (HashCoord(ix + 1, iy, seed) & 0xffff) / 65535.0f;
    float c = (HashCoord(ix, iy + 1, seed) & 0xffff) / 65535.0f;
    float d = (HashCoord(ix + 1, iy + 1, seed) & 0xffff) / 65535.0f;

    return a + (b - a) * ux + (c - a) * uy + (a - b - c + d) * ux * uy;
}

static float TerrainNoise(int x, int z, unsigned int seed) {
    float sum = 0;
    float amp = 1.0f;
    float freq = 1.0f;
    for (int i = 0; i < 4; i++) {
        sum += ValueNoise(x * freq / 64.0f, z * freq / 64.0f, seed) * amp;
        amp *= 0.5f;
        freq *= 2.0f;
    }
    return sum;
}

World::World(unsigned int s) : seed(s) {
    if (seed == 0) seed = (unsigned int)time(nullptr);
    material = LoadMaterialDefault();
    UnloadTexture(material.maps[MATERIAL_MAP_DIFFUSE].texture);
    material.maps[MATERIAL_MAP_DIFFUSE].texture = LoadBlockAtlas();
}

World::~World() {
    for (auto& [key, chunk] : chunks) {
        if (chunk.hasMesh) UnloadMesh(chunk.mesh);
    }
    UnloadMaterial(material);
}

void World::Save(const char* path) const {
    FILE* f = fopen(path, "wb");
    if (!f) return;

    const char magic[4] = {'M','I','N','I'};
    fwrite(magic, 1, 4, f);
    uint32_t version = 1;
    fwrite(&version, sizeof(uint32_t), 1, f);
    fwrite(&seed, sizeof(unsigned int), 1, f);
    fwrite(&playerPos, sizeof(Vector3), 1, f);
    fwrite(&playerYaw, sizeof(float), 1, f);
    fwrite(&playerPitch, sizeof(float), 1, f);

    uint32_t numChunks = (uint32_t)chunks.size();
    fwrite(&numChunks, sizeof(uint32_t), 1, f);

    for (auto& [key, chunk] : chunks) {
        int32_t cx = key.first;
        int32_t cz = key.second;
        fwrite(&cx, sizeof(int32_t), 1, f);
        fwrite(&cz, sizeof(int32_t), 1, f);
        fwrite(chunk.blocks, sizeof(BlockType), CHUNK_SIZE * CHUNK_HEIGHT * CHUNK_SIZE, f);
    }

    fclose(f);
}

bool World::Load(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;

    char magic[4];
    fread(magic, 1, 4, f);
    if (memcmp(magic, "MINI", 4) != 0) { fclose(f); return false; }

    uint32_t version;
    fread(&version, sizeof(uint32_t), 1, f);
    if (version != 1) { fclose(f); return false; }

    fread(&seed, sizeof(unsigned int), 1, f);
    fread(&playerPos, sizeof(Vector3), 1, f);
    fread(&playerYaw, sizeof(float), 1, f);
    fread(&playerPitch, sizeof(float), 1, f);

    uint32_t numChunks;
    fread(&numChunks, sizeof(uint32_t), 1, f);

    for (uint32_t i = 0; i < numChunks; i++) {
        int32_t cx, cz;
        fread(&cx, sizeof(int32_t), 1, f);
        fread(&cz, sizeof(int32_t), 1, f);

        auto key = std::make_pair((int)cx, (int)cz);
        Chunk& chunk = chunks[key];
        fread(chunk.blocks, sizeof(BlockType), CHUNK_SIZE * CHUNK_HEIGHT * CHUNK_SIZE, f);
        chunk.generated = true;
        chunk.dirty = true;
    }

    fclose(f);
    return true;
}

Chunk* World::GetChunk(int cx, int cz) {
    auto it = chunks.find({cx, cz});
    return (it != chunks.end()) ? &it->second : nullptr;
}

const Chunk* World::GetChunk(int cx, int cz) const {
    auto it = chunks.find({cx, cz});
    return (it != chunks.end()) ? &it->second : nullptr;
}

void World::EnsureChunk(int cx, int cz) {
    auto key = std::make_pair(cx, cz);
    if (chunks.find(key) == chunks.end()) {
        GenerateChunk(chunks[key], cx, cz);
    }
}

void World::GenerateChunk(Chunk& chunk, int cx, int cz) {
    int startX = cx * CHUNK_SIZE;
    int startZ = cz * CHUNK_SIZE;

    for (int lx = 0; lx < CHUNK_SIZE; lx++) {
        for (int lz = 0; lz < CHUNK_SIZE; lz++) {
            int x = startX + lx;
            int z = startZ + lz;

            float n = TerrainNoise(x, z, seed);
            int h = 18 + (int)((n - 0.5f) * 2.0f * 10);
            h = std::max(2, std::min(h, CHUNK_HEIGHT - 12));

            for (int y = 0; y <= h; y++) {
                BlockType t;
                if (y == h) t = BlockType::Grass;
                else if (y >= h - 3) t = BlockType::Dirt;
                else t = BlockType::Stone;
                chunk.blocks[chunk.Index(lx, y, lz)] = t;
            }
        }
    }

    for (int lx = 2; lx < CHUNK_SIZE - 2; lx++) {
        for (int lz = 2; lz < CHUNK_SIZE - 2; lz++) {
            int x = startX + lx;
            int z = startZ + lz;

            int h = 0;
            for (int y = CHUNK_HEIGHT - 1; y >= 0; y--) {
                if (chunk.blocks[chunk.Index(lx, y, lz)] == BlockType::Grass) {
                    h = y;
                    break;
                }
            }
            if (h == 0) continue;

            unsigned int treeHash = HashCoord(x * 13, z * 17, seed);
            if ((treeHash % 1000) >= 15) continue;

            int trunkHeight = 4 + (treeHash >> 16) % 2;
            for (int ty = 1; ty <= trunkHeight; ty++) {
                if (h + ty < CHUNK_HEIGHT)
                    chunk.blocks[chunk.Index(lx, h + ty, lz)] = BlockType::Wood;
            }

            int trunkTop = h + trunkHeight;
            for (int ly = 0; ly <= 2; ly++) {
                int ry = trunkTop - 2 + ly;
                int radius = (ly == 2) ? 1 : 2;
                for (int dx = -radius; dx <= radius; dx++) {
                    for (int dz = -radius; dz <= radius; dz++) {
                        if (radius == 2 && abs(dx) == 2 && abs(dz) == 2) continue;
                        int tlx = lx + dx;
                        int tlz = lz + dz;
                        if (tlx < 0 || tlx >= CHUNK_SIZE || tlz < 0 || tlz >= CHUNK_SIZE) continue;
                        if (ry >= 0 && ry < CHUNK_HEIGHT && chunk.blocks[chunk.Index(tlx, ry, tlz)] == BlockType::Air) {
                            chunk.blocks[chunk.Index(tlx, ry, tlz)] = BlockType::Leaves;
                        }
                    }
                }
            }
        }
    }

    chunk.generated = true;
    chunk.dirty = true;
}

BlockType World::GetBlock(int x, int y, int z) const {
    if (y < 0 || y >= CHUNK_HEIGHT) return BlockType::Air;
    int cx = (int)floorf((float)x / CHUNK_SIZE);
    int cz = (int)floorf((float)z / CHUNK_SIZE);
    const Chunk* chunk = GetChunk(cx, cz);
    if (!chunk) return BlockType::Air;
    int lx = x - cx * CHUNK_SIZE;
    int lz = z - cz * CHUNK_SIZE;
    return chunk->blocks[chunk->Index(lx, y, lz)];
}

void World::SetBlock(int x, int y, int z, BlockType type) {
    if (y < 0 || y >= CHUNK_HEIGHT) return;
    int cx = (int)floorf((float)x / CHUNK_SIZE);
    int cz = (int)floorf((float)z / CHUNK_SIZE);
    Chunk* chunk = GetChunk(cx, cz);
    if (!chunk) return;
    int lx = x - cx * CHUNK_SIZE;
    int lz = z - cz * CHUNK_SIZE;
    chunk->blocks[chunk->Index(lx, y, lz)] = type;

    chunk->dirty = true;
    if (lx == 0) { Chunk* c = GetChunk(cx - 1, cz); if (c) c->dirty = true; }
    if (lx == CHUNK_SIZE - 1) { Chunk* c = GetChunk(cx + 1, cz); if (c) c->dirty = true; }
    if (lz == 0) { Chunk* c = GetChunk(cx, cz - 1); if (c) c->dirty = true; }
    if (lz == CHUNK_SIZE - 1) { Chunk* c = GetChunk(cx, cz + 1); if (c) c->dirty = true; }
}

bool World::IsSolid(int x, int y, int z) const {
    return GetBlock(x, y, z) != BlockType::Air;
}

int World::HeightAt(int x, int z) const {
    for (int y = CHUNK_HEIGHT - 1; y >= 0; y--) {
        if (GetBlock(x, y, z) != BlockType::Air) return y;
    }
    return 0;
}

void World::RebuildDirtyChunks(Vector3 center, float radius) {
    int cx0 = (int)floorf((center.x - radius) / CHUNK_SIZE);
    int cx1 = (int)floorf((center.x + radius) / CHUNK_SIZE);
    int cz0 = (int)floorf((center.z - radius) / CHUNK_SIZE);
    int cz1 = (int)floorf((center.z + radius) / CHUNK_SIZE);

    for (int cz = cz0; cz <= cz1; cz++) {
        for (int cx = cx0; cx <= cx1; cx++) {
            Chunk* chunk = GetChunk(cx, cz);
            if (chunk && chunk->dirty) {
                BuildChunkMesh(*this, cx, cz, *chunk);
                chunk->dirty = false;
            }
        }
    }
}

void World::Render(Vector3 center, float radius) {
    int cx0 = (int)floorf((center.x - radius) / CHUNK_SIZE);
    int cx1 = (int)floorf((center.x + radius) / CHUNK_SIZE);
    int cz0 = (int)floorf((center.z - radius) / CHUNK_SIZE);
    int cz1 = (int)floorf((center.z + radius) / CHUNK_SIZE);

    for (int cz = cz0; cz <= cz1; cz++) {
        for (int cx = cx0; cx <= cx1; cx++) {
            const Chunk* chunk = GetChunk(cx, cz);
            if (chunk && chunk->hasMesh) {
                DrawMesh(chunk->mesh, material, MatrixIdentity());
            }
        }
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

void World::SetPlayerState(Vector3 pos, float yaw, float pitch) {
    playerPos = pos;
    playerYaw = yaw;
    playerPitch = pitch;
}
