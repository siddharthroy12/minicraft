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
    Rectangle uv = GetTileUV(GetTileIndex(type, face));
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

void BuildChunkMesh(World& world, int cx, int cz, Chunk& chunk) {
    std::vector<float> verts;
    std::vector<float> uvs;
    std::vector<unsigned char> cols;

    int startX = cx * CHUNK_SIZE;
    int startZ = cz * CHUNK_SIZE;

    for (int lx = 0; lx < CHUNK_SIZE; lx++) {
        int x = startX + lx;
        for (int lz = 0; lz < CHUNK_SIZE; lz++) {
            int z = startZ + lz;
            for (int y = 0; y < CHUNK_HEIGHT; y++) {
                BlockType t = world.GetBlock(x, y, z);
                if (t == BlockType::Air) continue;

                if (!world.IsSolid(x + 1, y, z)) AddFace(verts, uvs, cols, world, x, y, z, Face::PosX, t);
                if (!world.IsSolid(x - 1, y, z)) AddFace(verts, uvs, cols, world, x, y, z, Face::NegX, t);
                if (!world.IsSolid(x, y + 1, z)) AddFace(verts, uvs, cols, world, x, y, z, Face::PosY, t);
                if (!world.IsSolid(x, y - 1, z)) AddFace(verts, uvs, cols, world, x, y, z, Face::NegY, t);
                if (!world.IsSolid(x, y, z + 1)) AddFace(verts, uvs, cols, world, x, y, z, Face::PosZ, t);
                if (!world.IsSolid(x, y, z - 1)) AddFace(verts, uvs, cols, world, x, y, z, Face::NegZ, t);
            }
        }
    }

    if (chunk.hasMesh) {
        UnloadMesh(chunk.mesh);
        chunk.hasMesh = false;
        chunk.mesh = Mesh{};
    }

    if (verts.empty()) return;

    Mesh mesh{};
    int vertexCount = (int)(verts.size() / 3);
    mesh.vertexCount = vertexCount;
    mesh.triangleCount = vertexCount / 3;
    mesh.vertices = (float*)malloc(verts.size() * sizeof(float));
    memcpy(mesh.vertices, verts.data(), verts.size() * sizeof(float));
    mesh.texcoords = (float*)malloc(uvs.size() * sizeof(float));
    memcpy(mesh.texcoords, uvs.data(), uvs.size() * sizeof(float));
    mesh.colors = (unsigned char*)malloc(cols.size() * sizeof(unsigned char));
    memcpy(mesh.colors, cols.data(), cols.size() * sizeof(unsigned char));

    UploadMesh(&mesh, false);
    chunk.mesh = mesh;
    chunk.hasMesh = true;
}
