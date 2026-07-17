#include "Textures.h"
#include "raymath.h"
#include <cmath>

static Color Noisy(Color base, int variance) {
    int r = base.r + GetRandomValue(-variance, variance);
    int g = base.g + GetRandomValue(-variance, variance);
    int b = base.b + GetRandomValue(-variance, variance);
    return Color{
        (unsigned char)Clamp((float)r, 0, 255),
        (unsigned char)Clamp((float)g, 0, 255),
        (unsigned char)Clamp((float)b, 0, 255),
        255
    };
}

static void Put(Image& img, int tile, int x, int y, Color c) {
    ImageDrawPixel(&img, tile * TILE_SIZE + x, y, c);
}

static void GenGrassTop(Image& img, int t) {
    for (int y = 0; y < TILE_SIZE; y++)
        for (int x = 0; x < TILE_SIZE; x++)
            Put(img, t, x, y, Noisy(Color{97, 173, 62, 255}, 18));
}

static void GenGrassSide(Image& img, int t) {
    for (int x = 0; x < TILE_SIZE; x++) {
        int overhang = 3 + GetRandomValue(0, 1);
        for (int y = 0; y < TILE_SIZE; y++) {
            if (y < overhang) Put(img, t, x, y, Noisy(Color{97, 173, 62, 255}, 18));
            else Put(img, t, x, y, Noisy(Color{110, 79, 51, 255}, 14));
        }
    }
}

static void GenDirt(Image& img, int t) {
    for (int y = 0; y < TILE_SIZE; y++)
        for (int x = 0; x < TILE_SIZE; x++)
            Put(img, t, x, y, Noisy(Color{110, 79, 51, 255}, 16));
}

static void GenStone(Image& img, int t) {
    for (int y = 0; y < TILE_SIZE; y++) {
        for (int x = 0; x < TILE_SIZE; x++) {
            Color c = Noisy(Color{130, 130, 130, 255}, 14);
            if (GetRandomValue(0, 100) < 8) c = Noisy(Color{95, 95, 95, 255}, 6);
            Put(img, t, x, y, c);
        }
    }
}

static void GenSand(Image& img, int t) {
    for (int y = 0; y < TILE_SIZE; y++)
        for (int x = 0; x < TILE_SIZE; x++)
            Put(img, t, x, y, Noisy(Color{219, 208, 155, 255}, 10));
}

static void GenWoodTop(Image& img, int t) {
    float cx = 7.5f, cy = 7.5f;
    for (int y = 0; y < TILE_SIZE; y++) {
        for (int x = 0; x < TILE_SIZE; x++) {
            float d = sqrtf((x - cx) * (x - cx) + (y - cy) * (y - cy));
            Color base = ((int)(d * 1.5f) % 2 == 0) ? Color{160, 121, 79, 255} : Color{143, 105, 66, 255};
            Put(img, t, x, y, Noisy(base, 6));
        }
    }
}

static void GenWoodSide(Image& img, int t) {
    for (int x = 0; x < TILE_SIZE; x++) {
        Color base = (x % 3 == 0) ? Color{101, 73, 45, 255} : Color{124, 91, 56, 255};
        for (int y = 0; y < TILE_SIZE; y++) {
            Put(img, t, x, y, Noisy(base, 8));
        }
    }
}

static void GenLeaves(Image& img, int t) {
    for (int y = 0; y < TILE_SIZE; y++)
        for (int x = 0; x < TILE_SIZE; x++)
            Put(img, t, x, y, Noisy(Color{63, 122, 47, 255}, 26));
}

Texture2D LoadBlockAtlas() {
    Image atlas = GenImageColor(TILE_SIZE * ATLAS_TILE_COUNT, TILE_SIZE, WHITE);

    GenGrassTop(atlas, TILE_GRASS_TOP);
    GenGrassSide(atlas, TILE_GRASS_SIDE);
    GenDirt(atlas, TILE_DIRT);
    GenStone(atlas, TILE_STONE);
    GenSand(atlas, TILE_SAND);
    GenWoodTop(atlas, TILE_WOOD_TOP);
    GenWoodSide(atlas, TILE_WOOD_SIDE);
    GenLeaves(atlas, TILE_LEAVES);

    Texture2D tex = LoadTextureFromImage(atlas);
    UnloadImage(atlas);
    SetTextureFilter(tex, TEXTURE_FILTER_POINT);
    return tex;
}

int GetTileIndex(BlockType type, Face face) {
    switch (type) {
        case BlockType::Grass:
            if (face == Face::PosY) return TILE_GRASS_TOP;
            if (face == Face::NegY) return TILE_DIRT;
            return TILE_GRASS_SIDE;
        case BlockType::Dirt:
            return TILE_DIRT;
        case BlockType::Stone:
            return TILE_STONE;
        case BlockType::Sand:
            return TILE_SAND;
        case BlockType::Wood:
            if (face == Face::PosY || face == Face::NegY) return TILE_WOOD_TOP;
            return TILE_WOOD_SIDE;
        case BlockType::Leaves:
            return TILE_LEAVES;
        default:
            return TILE_STONE;
    }
}

Rectangle GetTileUV(int tileIndex) {
    float u0 = (float)tileIndex / ATLAS_TILE_COUNT;
    float w = 1.0f / ATLAS_TILE_COUNT;
    return Rectangle{u0, 0.0f, w, 1.0f};
}
