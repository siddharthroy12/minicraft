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

static void PushQuad(std::vector<float>& verts, std::vector<float>& uvs, std::vector<unsigned char>& cols,
                      Vector3 a, Vector3 b, Vector3 c, Vector3 d, Rectangle uvRect, Color col) {
    Vector2 uvA = {uvRect.x, uvRect.y + uvRect.height};
    Vector2 uvB = {uvRect.x, uvRect.y};
    Vector2 uvC = {uvRect.x + uvRect.width, uvRect.y};
    Vector2 uvD = {uvRect.x + uvRect.width, uvRect.y + uvRect.height};
    PushVertex(verts, uvs, cols, a, uvA, col);
    PushVertex(verts, uvs, cols, b, uvB, col);
    PushVertex(verts, uvs, cols, c, uvC, col);
    PushVertex(verts, uvs, cols, a, uvA, col);
    PushVertex(verts, uvs, cols, c, uvC, col);
    PushVertex(verts, uvs, cols, d, uvD, col);
}

static void AddFace(std::vector<float>& verts, std::vector<float>& uvs, std::vector<unsigned char>& cols,
                     float x, float y, float z, Face face, BlockType type) {
    Color col = GetFaceShade(face);
    Rectangle uv = GetTileUV(GetTileIndex(type, face));
    switch (face) {
        case Face::PosX:
            PushQuad(verts, uvs, cols, {x+1,y,z}, {x+1,y+1,z}, {x+1,y+1,z+1}, {x+1,y,z+1}, uv, col);
            break;
        case Face::NegX:
            PushQuad(verts, uvs, cols, {x,y,z+1}, {x,y+1,z+1}, {x,y+1,z}, {x,y,z}, uv, col);
            break;
        case Face::PosY:
            PushQuad(verts, uvs, cols, {x,y+1,z}, {x,y+1,z+1}, {x+1,y+1,z+1}, {x+1,y+1,z}, uv, col);
            break;
        case Face::NegY:
            PushQuad(verts, uvs, cols, {x,y,z+1}, {x,y,z}, {x+1,y,z}, {x+1,y,z+1}, uv, col);
            break;
        case Face::PosZ:
            PushQuad(verts, uvs, cols, {x+1,y,z+1}, {x+1,y+1,z+1}, {x,y+1,z+1}, {x,y,z+1}, uv, col);
            break;
        case Face::NegZ:
            PushQuad(verts, uvs, cols, {x,y,z}, {x,y+1,z}, {x+1,y+1,z}, {x+1,y,z}, uv, col);
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

                float fx = (float)x, fy = (float)y, fz = (float)z;
                if (!world.IsSolid(x + 1, y, z)) AddFace(verts, uvs, cols, fx, fy, fz, Face::PosX, t);
                if (!world.IsSolid(x - 1, y, z)) AddFace(verts, uvs, cols, fx, fy, fz, Face::NegX, t);
                if (!world.IsSolid(x, y + 1, z)) AddFace(verts, uvs, cols, fx, fy, fz, Face::PosY, t);
                if (!world.IsSolid(x, y - 1, z)) AddFace(verts, uvs, cols, fx, fy, fz, Face::NegY, t);
                if (!world.IsSolid(x, y, z + 1)) AddFace(verts, uvs, cols, fx, fy, fz, Face::PosZ, t);
                if (!world.IsSolid(x, y, z - 1)) AddFace(verts, uvs, cols, fx, fy, fz, Face::NegZ, t);
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
