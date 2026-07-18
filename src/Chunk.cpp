#include "Chunk.h"
#include "World.h"
#include "Textures.h"
#include <vector>
#include <cstdlib>
#include <cstring>

static void PushVertex(std::vector<float>& verts, std::vector<float>& uvs, std::vector<unsigned char>& cols,
                        Vector3 p, Vector2 uv, Color c) {
    verts.push_back(p.x);
    verts.push_back(p.y);
    verts.push_back(p.z);
    uvs.push_back(uv.x);
    uvs.push_back(uv.y);
    cols.push_back(c.r);
    cols.push_back(c.g);
    cols.push_back(c.b);
    cols.push_back(c.a);
}

// a,b,c,d must go around the quad in order, so colA/colC and colB/colD are
// the two diagonal pairs.
static void PushQuad(std::vector<float>& verts, std::vector<float>& uvs, std::vector<unsigned char>& cols,
                      Vector3 a, Vector3 b, Vector3 c, Vector3 d, Rectangle uvRect,
                      Color colA, Color colB, Color colC, Color colD, bool flip) {
    Vector2 uvA = {uvRect.x, uvRect.y + uvRect.height};
    Vector2 uvB = {uvRect.x, uvRect.y};
    Vector2 uvC = {uvRect.x + uvRect.width, uvRect.y};
    Vector2 uvD = {uvRect.x + uvRect.width, uvRect.y + uvRect.height};
    if (!flip) {
        PushVertex(verts, uvs, cols, a, uvA, colA);
        PushVertex(verts, uvs, cols, b, uvB, colB);
        PushVertex(verts, uvs, cols, c, uvC, colC);
        PushVertex(verts, uvs, cols, a, uvA, colA);
        PushVertex(verts, uvs, cols, c, uvC, colC);
        PushVertex(verts, uvs, cols, d, uvD, colD);
    } else {
        // Split along the other diagonal (b-d) to avoid the AO interpolation
        // artifact that appears when a highly-occluded and unoccluded corner
        // are connected by a diagonal edge.
        PushVertex(verts, uvs, cols, a, uvA, colA);
        PushVertex(verts, uvs, cols, b, uvB, colB);
        PushVertex(verts, uvs, cols, d, uvD, colD);
        PushVertex(verts, uvs, cols, b, uvB, colB);
        PushVertex(verts, uvs, cols, c, uvC, colC);
        PushVertex(verts, uvs, cols, d, uvD, colD);
    }
}

// Classic Minecraft-style ambient occlusion (see 0fps.net "Ambient occlusion
// for Minecraft-like worlds"): each face corner looks at the two adjacent
// edge neighbors and the diagonal corner neighbor in the layer just outside
// the exposed face. If both edge neighbors are solid the corner is fully
// occluded regardless of the diagonal, since no light can reach around them.
static int VertexAO(bool side1, bool side2, bool corner) {
    if (side1 && side2) return 0;
    return 3 - (int)side1 - (int)side2 - (int)corner;
}

struct FaceAO {
    unsigned char a, b, c, d; // 0..3, matches the a,b,c,d vertex order used in AddFace
};

// For each face, (cu[i],cv[i]) gives vertex i's position along the face's two
// tangent axes (0 = block's own coordinate, 1 = coordinate+1), matching the
// exact a,b,c,d vertex order built in AddFace below.
static FaceAO ComputeFaceAO(World& world, int x, int y, int z, Face face) {
    int nx = 0, ny = 0, nz = 0;
    int ux = 0, uy = 0, uz = 0;
    int vx = 0, vy = 0, vz = 0;
    int cu[4], cv[4];
    switch (face) {
        case Face::PosX:
            nx = 1; uy = 1; vz = 1;
            cu[0]=0; cv[0]=0; cu[1]=1; cv[1]=0; cu[2]=1; cv[2]=1; cu[3]=0; cv[3]=1;
            break;
        case Face::NegX:
            nx = -1; uy = 1; vz = 1;
            cu[0]=0; cv[0]=1; cu[1]=1; cv[1]=1; cu[2]=1; cv[2]=0; cu[3]=0; cv[3]=0;
            break;
        case Face::PosY:
            ny = 1; ux = 1; vz = 1;
            cu[0]=0; cv[0]=0; cu[1]=0; cv[1]=1; cu[2]=1; cv[2]=1; cu[3]=1; cv[3]=0;
            break;
        case Face::NegY:
            ny = -1; ux = 1; vz = 1;
            cu[0]=0; cv[0]=1; cu[1]=0; cv[1]=0; cu[2]=1; cv[2]=0; cu[3]=1; cv[3]=1;
            break;
        case Face::PosZ:
            nz = 1; ux = 1; vy = 1;
            cu[0]=1; cv[0]=0; cu[1]=1; cv[1]=1; cu[2]=0; cv[2]=1; cu[3]=0; cv[3]=0;
            break;
        case Face::NegZ:
            nz = -1; ux = 1; vy = 1;
            cu[0]=0; cv[0]=0; cu[1]=0; cv[1]=1; cu[2]=1; cv[2]=1; cu[3]=1; cv[3]=0;
            break;
    }

    unsigned char ao[4];
    for (int i = 0; i < 4; i++) {
        int su = cu[i] ? 1 : -1;
        int sv = cv[i] ? 1 : -1;
        bool side1 = world.IsSolid(x + nx + su * ux, y + ny + su * uy, z + nz + su * uz);
        bool side2 = world.IsSolid(x + nx + sv * vx, y + ny + sv * vy, z + nz + sv * vz);
        bool corner = world.IsSolid(x + nx + su * ux + sv * vx, y + ny + su * uy + sv * vy, z + nz + su * uz + sv * vz);
        ao[i] = (unsigned char)VertexAO(side1, side2, corner);
    }
    return {ao[0], ao[1], ao[2], ao[3]};
}

static const float kAOLevels[4] = {0.4f, 0.6f, 0.8f, 1.0f};

static Color ApplyAO(Color base, int ao) {
    float f = kAOLevels[ao];
    return Color{
        (unsigned char)(base.r * f),
        (unsigned char)(base.g * f),
        (unsigned char)(base.b * f),
        base.a,
    };
}

static void AddFace(std::vector<float>& verts, std::vector<float>& uvs, std::vector<unsigned char>& cols,
                     World& world, int x, int y, int z, Face face, BlockType type) {
    Color base = GetFaceShade(face);
    Rectangle uv = GetTileUV(type, face);
    FaceAO ao = ComputeFaceAO(world, x, y, z, face);
    Color colA = ApplyAO(base, ao.a);
    Color colB = ApplyAO(base, ao.b);
    Color colC = ApplyAO(base, ao.c);
    Color colD = ApplyAO(base, ao.d);
    bool flip = (ao.a + ao.c) < (ao.b + ao.d);

    float fx = (float)x, fy = (float)y, fz = (float)z;
    switch (face) {
        case Face::PosX:
            PushQuad(verts, uvs, cols, {fx+1,fy,fz}, {fx+1,fy+1,fz}, {fx+1,fy+1,fz+1}, {fx+1,fy,fz+1}, uv,
                      colA, colB, colC, colD, flip);
            break;
        case Face::NegX:
            PushQuad(verts, uvs, cols, {fx,fy,fz+1}, {fx,fy+1,fz+1}, {fx,fy+1,fz}, {fx,fy,fz}, uv,
                      colA, colB, colC, colD, flip);
            break;
        case Face::PosY:
            PushQuad(verts, uvs, cols, {fx,fy+1,fz}, {fx,fy+1,fz+1}, {fx+1,fy+1,fz+1}, {fx+1,fy+1,fz}, uv,
                      colA, colB, colC, colD, flip);
            break;
        case Face::NegY:
            PushQuad(verts, uvs, cols, {fx,fy,fz+1}, {fx,fy,fz}, {fx+1,fy,fz}, {fx+1,fy,fz+1}, uv,
                      colA, colB, colC, colD, flip);
            break;
        case Face::PosZ:
            PushQuad(verts, uvs, cols, {fx+1,fy,fz+1}, {fx+1,fy+1,fz+1}, {fx,fy+1,fz+1}, {fx,fy,fz+1}, uv,
                      colA, colB, colC, colD, flip);
            break;
        case Face::NegZ:
            PushQuad(verts, uvs, cols, {fx,fy,fz}, {fx,fy+1,fz}, {fx+1,fy+1,fz}, {fx+1,fy,fz}, uv,
                      colA, colB, colC, colD, flip);
            break;
    }
}

// Source blocks and falling (waterfall) columns render at full cube height;
// flowing water gets shorter the farther it is from a source, matching
// Minecraft's 8-step water levels.
static float WaterHeight(unsigned char raw) {
    bool falling = raw & 0x8;
    int level = raw & 0x7;
    if (falling || level == 0) return 1.0f;
    return 1.0f - (level / 8.0f);
}

struct FaceHeights { float a, b, c, d; }; // matches Face::PosY's a,b,c,d vertex order in AddFace

// Averages this water block's surface height with whichever of its 3
// touching neighbor columns (edge, edge, diagonal) are also water at the
// same layer, producing the smoothly-sloped, quilted surface Minecraft's
// water is known for. Falling columns skip this and stay flat/vertical.
static FaceHeights ComputeWaterCornerHeights(World& world, int x, int y, int z) {
    unsigned char rawSelf = world.GetWaterLevelRaw(x, y, z);
    if (rawSelf & 0x8) return {1.0f, 1.0f, 1.0f, 1.0f};

    float selfHeight = WaterHeight(rawSelf);
    static const int cu[4] = {0, 0, 1, 1};
    static const int cv[4] = {0, 1, 1, 0};
    float heights[4];
    for (int i = 0; i < 4; i++) {
        int su = cu[i] ? 1 : -1;
        int sv = cv[i] ? 1 : -1;
        float sum = selfHeight;
        int count = 1;
        if (world.GetBlock(x + su, y, z) == BlockType::Water) {
            sum += WaterHeight(world.GetWaterLevelRaw(x + su, y, z));
            count++;
        }
        if (world.GetBlock(x, y, z + sv) == BlockType::Water) {
            sum += WaterHeight(world.GetWaterLevelRaw(x, y, z + sv));
            count++;
        }
        if (world.GetBlock(x + su, y, z + sv) == BlockType::Water) {
            sum += WaterHeight(world.GetWaterLevelRaw(x + su, y, z + sv));
            count++;
        }
        heights[i] = sum / count;
    }
    return {heights[0], heights[1], heights[2], heights[3]};
}

// Side faces span from `bot` to `h` along their shared edge corners: against
// air, bot is 0 (the full face, with the sloped "lip" top edge); against a
// shorter water neighbor, bot is the neighbor's surface so only the exposed
// strip between the two surfaces is drawn. Corner heights of adjacent water
// blocks agree at shared corners, so strips meet both surfaces seamlessly.
static void AddWaterFace(std::vector<float>& verts, std::vector<float>& uvs, std::vector<unsigned char>& cols,
                          World& world, int x, int y, int z, Face face, const FaceHeights& h,
                          const FaceHeights& bot = {0, 0, 0, 0}) {
    Color base = GetFaceShade(face);
    base.a = 210;
    Rectangle uv = GetTileUV(BlockType::Water, face);
    FaceAO ao = ComputeFaceAO(world, x, y, z, face);
    Color colA = ApplyAO(base, ao.a);
    Color colB = ApplyAO(base, ao.b);
    Color colC = ApplyAO(base, ao.c);
    Color colD = ApplyAO(base, ao.d);
    bool flip = (ao.a + ao.c) < (ao.b + ao.d);

    float fx = (float)x, fy = (float)y, fz = (float)z;
    switch (face) {
        case Face::PosX:
            PushQuad(verts, uvs, cols, {fx+1,fy+bot.d,fz}, {fx+1,fy+h.d,fz}, {fx+1,fy+h.c,fz+1}, {fx+1,fy+bot.c,fz+1}, uv,
                      colA, colB, colC, colD, flip);
            break;
        case Face::NegX:
            PushQuad(verts, uvs, cols, {fx,fy+bot.b,fz+1}, {fx,fy+h.b,fz+1}, {fx,fy+h.a,fz}, {fx,fy+bot.a,fz}, uv,
                      colA, colB, colC, colD, flip);
            break;
        case Face::PosY:
            PushQuad(verts, uvs, cols, {fx,fy+h.a,fz}, {fx,fy+h.b,fz+1}, {fx+1,fy+h.c,fz+1}, {fx+1,fy+h.d,fz}, uv,
                      colA, colB, colC, colD, flip);
            break;
        case Face::NegY:
            PushQuad(verts, uvs, cols, {fx,fy,fz+1}, {fx,fy,fz}, {fx+1,fy,fz}, {fx+1,fy,fz+1}, uv,
                      colA, colB, colC, colD, flip);
            break;
        case Face::PosZ:
            PushQuad(verts, uvs, cols, {fx+1,fy+bot.c,fz+1}, {fx+1,fy+h.c,fz+1}, {fx,fy+h.b,fz+1}, {fx,fy+bot.b,fz+1}, uv,
                      colA, colB, colC, colD, flip);
            break;
        case Face::NegZ:
            PushQuad(verts, uvs, cols, {fx,fy+bot.a,fz}, {fx,fy+h.a,fz}, {fx+1,fy+h.d,fz}, {fx+1,fy+bot.d,fz}, uv,
                      colA, colB, colC, colD, flip);
            break;
    }
}

static void UploadChunkMesh(Mesh& mesh, bool& hasMesh, std::vector<float>& verts, std::vector<float>& uvs, std::vector<unsigned char>& cols) {
    if (hasMesh) {
        UnloadMesh(mesh);
        hasMesh = false;
        mesh = Mesh{};
    }

    if (verts.empty()) return;

    Mesh m{};
    int vertexCount = (int)(verts.size() / 3);
    m.vertexCount = vertexCount;
    m.triangleCount = vertexCount / 3;
    m.vertices = (float*)malloc(verts.size() * sizeof(float));
    memcpy(m.vertices, verts.data(), verts.size() * sizeof(float));
    m.texcoords = (float*)malloc(uvs.size() * sizeof(float));
    memcpy(m.texcoords, uvs.data(), uvs.size() * sizeof(float));
    m.colors = (unsigned char*)malloc(cols.size() * sizeof(unsigned char));
    memcpy(m.colors, cols.data(), cols.size() * sizeof(unsigned char));

    UploadMesh(&m, false);
    mesh = m;
    hasMesh = true;
}

void BuildChunkMesh(World& world, int cx, int cz, Chunk& chunk) {
    std::vector<float> verts, waterVerts;
    std::vector<float> uvs, waterUvs;
    std::vector<unsigned char> cols, waterCols;

    int startX = cx * CHUNK_SIZE;
    int startZ = cz * CHUNK_SIZE;

    for (int lx = 0; lx < CHUNK_SIZE; lx++) {
        int x = startX + lx;
        for (int lz = 0; lz < CHUNK_SIZE; lz++) {
            int z = startZ + lz;
            for (int y = 0; y < CHUNK_HEIGHT; y++) {
                BlockType t = world.GetBlock(x, y, z);
                if (t == BlockType::Air) continue;

                if (t == BlockType::Water) {
                    // Water shows a face against open air, and against
                    // neighboring water that sits *lower* — there, only the
                    // exposed strip between the two surfaces is drawn (a
                    // full-height falling column beside partial flowing
                    // water would otherwise leave a see-through slit).
                    // Faces against solid blocks stay culled: the solid
                    // block's own face covers that boundary.
                    FaceHeights h = ComputeWaterCornerHeights(world, x, y, z);
                    constexpr float kEps = 0.001f;
                    auto sideFace = [&](Face face, int nx, int nz) {
                        BlockType n = world.GetBlock(nx, y, nz);
                        if (n == BlockType::Air) {
                            AddWaterFace(waterVerts, waterUvs, waterCols, world, x, y, z, face, h);
                        } else if (n == BlockType::Water) {
                            FaceHeights nb = ComputeWaterCornerHeights(world, nx, y, nz);
                            FaceHeights bot{0, 0, 0, 0};
                            // Map the neighbor's corners on the shared face
                            // onto this block's corner slots for that face.
                            switch (face) {
                                case Face::PosX: bot.d = nb.a; bot.c = nb.b; break;
                                case Face::NegX: bot.a = nb.d; bot.b = nb.c; break;
                                case Face::PosZ: bot.b = nb.a; bot.c = nb.d; break;
                                case Face::NegZ: bot.a = nb.b; bot.d = nb.c; break;
                                default: return;
                            }
                            bool exposed = false;
                            switch (face) {
                                case Face::PosX: exposed = h.d > bot.d + kEps || h.c > bot.c + kEps; break;
                                case Face::NegX: exposed = h.a > bot.a + kEps || h.b > bot.b + kEps; break;
                                case Face::PosZ: exposed = h.b > bot.b + kEps || h.c > bot.c + kEps; break;
                                case Face::NegZ: exposed = h.a > bot.a + kEps || h.d > bot.d + kEps; break;
                                default: break;
                            }
                            if (exposed) AddWaterFace(waterVerts, waterUvs, waterCols, world, x, y, z, face, h, bot);
                        }
                    };
                    sideFace(Face::PosX, x + 1, z);
                    sideFace(Face::NegX, x - 1, z);
                    sideFace(Face::PosZ, x, z + 1);
                    sideFace(Face::NegZ, x, z - 1);
                    if (world.GetBlock(x, y + 1, z) == BlockType::Air) AddWaterFace(waterVerts, waterUvs, waterCols, world, x, y, z, Face::PosY, h);
                    if (world.GetBlock(x, y - 1, z) == BlockType::Air) AddWaterFace(waterVerts, waterUvs, waterCols, world, x, y, z, Face::NegY, h);
                } else {
                    if (!world.IsSolid(x + 1, y, z)) AddFace(verts, uvs, cols, world, x, y, z, Face::PosX, t);
                    if (!world.IsSolid(x - 1, y, z)) AddFace(verts, uvs, cols, world, x, y, z, Face::NegX, t);
                    if (!world.IsSolid(x, y + 1, z)) AddFace(verts, uvs, cols, world, x, y, z, Face::PosY, t);
                    if (!world.IsSolid(x, y - 1, z)) AddFace(verts, uvs, cols, world, x, y, z, Face::NegY, t);
                    if (!world.IsSolid(x, y, z + 1)) AddFace(verts, uvs, cols, world, x, y, z, Face::PosZ, t);
                    if (!world.IsSolid(x, y, z - 1)) AddFace(verts, uvs, cols, world, x, y, z, Face::NegZ, t);
                }
            }
        }
    }

    UploadChunkMesh(chunk.mesh, chunk.hasMesh, verts, uvs, cols);
    UploadChunkMesh(chunk.waterMesh, chunk.hasWaterMesh, waterVerts, waterUvs, waterCols);
}
