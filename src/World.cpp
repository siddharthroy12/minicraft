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

constexpr int WATER_LEVEL = 18;

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

// Distance fog: reuses the standard raylib uniform/attribute names (mvp,
// matModel, texture0, colDiffuse) so DrawMesh's automatic wiring still
// supplies them; only viewPos/fogColor/fogStart/fogEnd need manual updates.
static const char* kTerrainVS = R"glsl(
#version 330

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec4 vertexColor;

uniform mat4 mvp;
uniform mat4 matModel;

out vec2 fragTexCoord;
out vec4 fragColor;
out vec3 fragPosition;

void main() {
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    fragPosition = vec3(matModel * vec4(vertexPosition, 1.0));
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)glsl";

static const char* kTerrainFS = R"glsl(
#version 330

in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragPosition;

uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform vec3 viewPos;
uniform vec3 fogColor;
uniform float fogStart;
uniform float fogEnd;

out vec4 finalColor;

void main() {
    vec4 texelColor = texture(texture0, fragTexCoord);
    vec4 base = texelColor * fragColor * colDiffuse;

    float dist = length(viewPos - fragPosition);
    float fogFactor = clamp((fogEnd - dist) / (fogEnd - fogStart), 0.0, 1.0);

    finalColor = vec4(mix(fogColor, base.rgb, fogFactor), base.a);
}
)glsl";

static const Color kSkyFogColor = {135, 206, 235, 255};
static const Color kUnderwaterFogColor = {20, 90, 130, 255};
constexpr float kFogStartAbove = RENDER_DISTANCE * CHUNK_SIZE * 0.6f;
constexpr float kFogEndAbove = RENDER_DISTANCE * CHUNK_SIZE * 0.97f;
constexpr float kFogStartUnderwater = 1.0f;
constexpr float kFogEndUnderwater = 10.0f;

World::World(unsigned int s) : seed(s) {
    if (seed == 0) seed = (unsigned int)time(nullptr);
    material = LoadMaterialDefault();
    UnloadTexture(material.maps[MATERIAL_MAP_DIFFUSE].texture);
    material.maps[MATERIAL_MAP_DIFFUSE].texture = LoadBlockAtlas();

    terrainShader = LoadShaderFromMemory(kTerrainVS, kTerrainFS);
    material.shader = terrainShader;
    fogViewPosLoc = GetShaderLocation(terrainShader, "viewPos");
    fogColorLoc = GetShaderLocation(terrainShader, "fogColor");
    fogStartLoc = GetShaderLocation(terrainShader, "fogStart");
    fogEndLoc = GetShaderLocation(terrainShader, "fogEnd");
}

World::~World() {
    for (auto& [key, chunk] : chunks) {
        if (chunk.hasMesh) UnloadMesh(chunk.mesh);
        if (chunk.hasWaterMesh) UnloadMesh(chunk.waterMesh);
    }
    UnloadMaterial(material); // also unloads terrainShader (material.shader)
}

void World::Save(const char* path) const {
    FILE* f = fopen(path, "wb");
    if (!f) return;

    const char magic[4] = {'M','I','N','I'};
    fwrite(magic, 1, 4, f);
    uint32_t version = 2; // v2 adds per-block water flow level
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
        fwrite(chunk.waterLevel, sizeof(unsigned char), CHUNK_SIZE * CHUNK_HEIGHT * CHUNK_SIZE, f);
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
    if (version != 1 && version != 2) { fclose(f); return false; }

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
        // v1 saves predate water flow levels; they default to 0 (source),
        // which is correct since v1 saves never contain non-source water.
        if (version >= 2) {
            fread(chunk.waterLevel, sizeof(unsigned char), CHUNK_SIZE * CHUNK_HEIGHT * CHUNK_SIZE, f);
        }
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

        // Neighbors that were meshed before this chunk existed treated its
        // space as air, so they may be missing culled faces (most visibly,
        // stray water faces facing what is now a same-level water block in
        // this chunk). Force them to remesh now that real data is here.
        if (Chunk* c = GetChunk(cx - 1, cz)) c->dirty = true;
        if (Chunk* c = GetChunk(cx + 1, cz)) c->dirty = true;
        if (Chunk* c = GetChunk(cx, cz - 1)) c->dirty = true;
        if (Chunk* c = GetChunk(cx, cz + 1)) c->dirty = true;
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

            bool underwater = h < WATER_LEVEL;
            for (int y = 0; y <= h; y++) {
                BlockType t;
                if (y == h) t = underwater ? BlockType::Dirt : BlockType::Grass;
                else if (y >= h - 3) t = BlockType::Dirt;
                else t = BlockType::Stone;
                chunk.blocks[chunk.Index(lx, y, lz)] = t;
            }
            if (underwater) {
                for (int y = h + 1; y <= WATER_LEVEL; y++) {
                    chunk.blocks[chunk.Index(lx, y, lz)] = BlockType::Water;
                }
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

void World::MarkChunkAndBordersDirty(int cx, int cz, int lx, int lz) {
    if (Chunk* chunk = GetChunk(cx, cz)) chunk->dirty = true;
    if (lx == 0) { Chunk* c = GetChunk(cx - 1, cz); if (c) c->dirty = true; }
    if (lx == CHUNK_SIZE - 1) { Chunk* c = GetChunk(cx + 1, cz); if (c) c->dirty = true; }
    if (lz == 0) { Chunk* c = GetChunk(cx, cz - 1); if (c) c->dirty = true; }
    if (lz == CHUNK_SIZE - 1) { Chunk* c = GetChunk(cx, cz + 1); if (c) c->dirty = true; }
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
    // A player-placed water block is always a fresh source.
    if (type == BlockType::Water) chunk->waterLevel[chunk->Index(lx, y, lz)] = 0;

    MarkChunkAndBordersDirty(cx, cz, lx, lz);

    // Notify neighbors so any adjacent flow starts spreading into this
    // change (new water source) or reacts to losing support (block placed
    // over/into existing flowing water).
    ScheduleWaterUpdate(x, y, z);
    ScheduleWaterUpdate(x + 1, y, z);
    ScheduleWaterUpdate(x - 1, y, z);
    ScheduleWaterUpdate(x, y + 1, z);
    ScheduleWaterUpdate(x, y - 1, z);
    ScheduleWaterUpdate(x, y, z + 1);
    ScheduleWaterUpdate(x, y, z - 1);
}

bool World::IsSolid(int x, int y, int z) const {
    BlockType t = GetBlock(x, y, z);
    return t != BlockType::Air && t != BlockType::Water;
}

int World::HeightAt(int x, int z) const {
    for (int y = CHUNK_HEIGHT - 1; y >= 0; y--) {
        BlockType t = GetBlock(x, y, z);
        if (t != BlockType::Air && t != BlockType::Water) return y;
    }
    return 0;
}

bool World::IsUnderwater(Vector3 pos) const {
    return GetBlock((int)floorf(pos.x), (int)floorf(pos.y), (int)floorf(pos.z)) == BlockType::Water;
}

Color World::GetFogColor(Vector3 eye) const {
    return IsUnderwater(eye) ? kUnderwaterFogColor : kSkyFogColor;
}

unsigned char World::GetWaterLevelRaw(int x, int y, int z) const {
    if (y < 0 || y >= CHUNK_HEIGHT) return 0;
    int cx = (int)floorf((float)x / CHUNK_SIZE);
    int cz = (int)floorf((float)z / CHUNK_SIZE);
    const Chunk* chunk = GetChunk(cx, cz);
    if (!chunk) return 0;
    int lx = x - cx * CHUNK_SIZE;
    int lz = z - cz * CHUNK_SIZE;
    return chunk->waterLevel[chunk->Index(lx, y, lz)];
}

void World::ScheduleWaterUpdate(int x, int y, int z) {
    pendingWaterUpdates.push_back({x, y, z});
}

constexpr int kMaxWaterLevel = 7;
constexpr float kWaterTickInterval = 0.25f; // 5 game ticks at Minecraft's 20 TPS
constexpr int kSlopeFindDistance = 4;       // wiki: water looks for holes within 4 blocks
constexpr int kNoHole = 1000;               // wiki: initial weight when no hole is found

static const int kDirX[4] = {1, -1, 0, 0};
static const int kDirZ[4] = {0, 0, 1, -1};

static unsigned char EncodeWaterRaw(int level, bool falling) {
    return (unsigned char)((falling ? 0x8 : 0) | (level & 0x7));
}

// A "hole" is a position water wants to reach: its below-neighbor is air,
// or an existing waterfall shaft it can join.
static bool IsHoleBelow(const World& world, int x, int y, int z) {
    BlockType below = world.GetBlock(x, y - 1, z);
    if (below == BlockType::Air) return true;
    return below == BlockType::Water && (world.GetWaterLevelRaw(x, y - 1, z) & 0x8);
}

// Distance from (startX,y,startZ) through air cells to the nearest hole.
// The start cell is already one step from the spreading water block, so the
// search goes at most kSlopeFindDistance-1 further steps; kNoHole if none.
static int SlopeDistance(const World& world, int startX, int y, int startZ) {
    if (IsHoleBelow(world, startX, y, startZ)) return 0;
    std::vector<std::array<int, 2>> frontier{{startX, startZ}};
    std::vector<std::array<int, 2>> visited{{startX, startZ}};
    for (int depth = 1; depth < kSlopeFindDistance; depth++) {
        std::vector<std::array<int, 2>> next;
        for (auto& cell : frontier) {
            for (int i = 0; i < 4; i++) {
                std::array<int, 2> n{cell[0] + kDirX[i], cell[1] + kDirZ[i]};
                if (std::find(visited.begin(), visited.end(), n) != visited.end()) continue;
                visited.push_back(n);
                if (world.GetBlock(n[0], y, n[1]) != BlockType::Air) continue;
                if (IsHoleBelow(world, n[0], y, n[1])) return depth;
                next.push_back(n);
            }
        }
        frontier = std::move(next);
        if (frontier.empty()) break;
    }
    return kNoHole;
}

// Water pours straight down when nothing is beneath it; it only spreads
// sideways when resting on solid ground or settled (non-falling) water.
static bool CanSpreadSideways(const World& world, int x, int y, int z) {
    BlockType below = world.GetBlock(x, y - 1, z);
    if (below == BlockType::Air) return false;
    if (below == BlockType::Water && (world.GetWaterLevelRaw(x, y - 1, z) & 0x8)) return false;
    return true;
}

// Bitmask over kDirX/kDirZ of the directions the water block at (x,y,z)
// actively flows into, per the wiki's flow-weight rule: every enterable
// (air) direction gets the distance to its nearest reachable hole, and only
// the minimal-weight directions flow. With no hole in range all enterable
// directions tie at kNoHole and water spreads out evenly.
static int PreferredFlowDirs(const World& world, int x, int y, int z) {
    int weights[4];
    int minWeight = kNoHole;
    for (int i = 0; i < 4; i++) {
        int nx = x + kDirX[i], nz = z + kDirZ[i];
        if (world.GetBlock(nx, y, nz) != BlockType::Air) { weights[i] = -1; continue; }
        weights[i] = SlopeDistance(world, nx, y, nz);
        if (weights[i] < minWeight) minWeight = weights[i];
    }
    int mask = 0;
    for (int i = 0; i < 4; i++) {
        if (weights[i] >= 0 && weights[i] == minWeight) mask |= 1 << i;
    }
    return mask;
}

// Recomputes whether (x,y,z) should be water and at what flow level,
// following the wiki's per-tick rules: water directly below other water
// becomes a full-height falling column; air only receives sideways flow
// from a neighbor that both rests on something and prefers this direction
// (nearest-hole weighting); existing flowing water keeps the strongest
// adjacent support or dries up; and flowing water beside two or more
// sources, resting on a solid block or another source, becomes a new
// source (infinite water). Sources themselves are permanent.
void World::ProcessWaterCell(int x, int y, int z) {
    if (y < 0 || y >= CHUNK_HEIGHT) return;

    BlockType t = GetBlock(x, y, z);
    if (t != BlockType::Air && t != BlockType::Water) return;
    if (t == BlockType::Water && GetWaterLevelRaw(x, y, z) == 0) return; // stable source

    bool aboveIsWater = GetBlock(x, y + 1, z) == BlockType::Water;

    int sourceNeighbors = 0;
    int bestLevel = -1;   // support for existing flowing water (direction-free, like vanilla decay)
    int acceptLevel = -1; // for air: only neighbors that actively flow this way count
    for (int i = 0; i < 4; i++) {
        int nx = x + kDirX[i], nz = z + kDirZ[i];
        if (GetBlock(nx, y, nz) != BlockType::Water) continue;
        unsigned char nRaw = GetWaterLevelRaw(nx, y, nz);
        if (nRaw == 0) sourceNeighbors++;
        int candidate = (nRaw & 0x7) + 1; // falling columns spread at full strength, like sources
        if (candidate > kMaxWaterLevel || !CanSpreadSideways(*this, nx, y, nz)) continue;
        if (bestLevel == -1 || candidate < bestLevel) bestLevel = candidate;
        // kDirX[i] points from this cell to the neighbor; i^1 is the
        // opposite direction, i.e. the neighbor flowing toward this cell.
        if (t == BlockType::Air && (PreferredFlowDirs(*this, nx, y, nz) & (1 << (i ^ 1)))) {
            if (acceptLevel == -1 || candidate < acceptLevel) acceptLevel = candidate;
        }
    }

    int newLevel = (t == BlockType::Air) ? acceptLevel : bestLevel;
    bool shouldBeWater = aboveIsWater || newLevel != -1;

    int cx = (int)floorf((float)x / CHUNK_SIZE);
    int cz = (int)floorf((float)z / CHUNK_SIZE);
    Chunk* chunk = GetChunk(cx, cz);
    if (!chunk) return;
    int lx = x - cx * CHUNK_SIZE, lz = z - cz * CHUNK_SIZE;

    if (shouldBeWater) {
        unsigned char newRaw = aboveIsWater ? EncodeWaterRaw(0, true) : EncodeWaterRaw(newLevel, false);
        // Infinite-water rule: 2+ adjacent sources and a solid block or
        // another source underneath turn this into a new source.
        if (sourceNeighbors >= 2) {
            BlockType below = GetBlock(x, y - 1, z);
            bool belowSolid = below != BlockType::Air && below != BlockType::Water;
            bool belowSource = below == BlockType::Water && GetWaterLevelRaw(x, y - 1, z) == 0;
            if (belowSolid || belowSource) newRaw = EncodeWaterRaw(0, false);
        }
        if (t == BlockType::Water && GetWaterLevelRaw(x, y, z) == newRaw) return; // unchanged
        chunk->blocks[chunk->Index(lx, y, lz)] = BlockType::Water;
        chunk->waterLevel[chunk->Index(lx, y, lz)] = newRaw;
    } else {
        if (t != BlockType::Water) return; // was air with no support: nothing changed
        chunk->blocks[chunk->Index(lx, y, lz)] = BlockType::Air;
    }
    MarkChunkAndBordersDirty(cx, cz, lx, lz);

    // This cell changed, so neighbors (and the cell above, for waterfalls)
    // may need to react next tick.
    ScheduleWaterUpdate(x + 1, y, z);
    ScheduleWaterUpdate(x - 1, y, z);
    ScheduleWaterUpdate(x, y + 1, z);
    ScheduleWaterUpdate(x, y - 1, z);
    ScheduleWaterUpdate(x, y, z + 1);
    ScheduleWaterUpdate(x, y, z - 1);
}

void World::SimulateWater(float dt) {
    waterTickAccum += dt;
    if (waterTickAccum < kWaterTickInterval) return;
    waterTickAccum -= kWaterTickInterval;

    std::vector<std::array<int, 3>> queue = std::move(pendingWaterUpdates);
    pendingWaterUpdates.clear();

    for (auto& p : queue) {
        ProcessWaterCell(p[0], p[1], p[2]);
    }
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
    bool underwater = IsUnderwater(center);
    Color fog = underwater ? kUnderwaterFogColor : kSkyFogColor;
    float fogStart = underwater ? kFogStartUnderwater : kFogStartAbove;
    float fogEnd = underwater ? kFogEndUnderwater : kFogEndAbove;

    float fogColorVec[3] = { fog.r / 255.0f, fog.g / 255.0f, fog.b / 255.0f };
    float viewPosVec[3] = { center.x, center.y, center.z };
    SetShaderValue(terrainShader, fogColorLoc, fogColorVec, SHADER_UNIFORM_VEC3);
    SetShaderValue(terrainShader, fogStartLoc, &fogStart, SHADER_UNIFORM_FLOAT);
    SetShaderValue(terrainShader, fogEndLoc, &fogEnd, SHADER_UNIFORM_FLOAT);
    SetShaderValue(terrainShader, fogViewPosLoc, viewPosVec, SHADER_UNIFORM_VEC3);

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

    BeginBlendMode(BLEND_ALPHA);
    for (int cz = cz0; cz <= cz1; cz++) {
        for (int cx = cx0; cx <= cx1; cx++) {
            const Chunk* chunk = GetChunk(cx, cz);
            if (chunk && chunk->hasWaterMesh) {
                DrawMesh(chunk->waterMesh, material, MatrixIdentity());
            }
        }
    }
    EndBlendMode();
}

void World::UpdateWater(float dt) {
    UpdateWaterAnimation(material.maps[MATERIAL_MAP_DIFFUSE].texture, dt);
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
