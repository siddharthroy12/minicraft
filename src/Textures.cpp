#include "Textures.h"
#include "raymath.h"
#include <cmath>

// Each block loads a single PNG from assets/textures/<name>.png. The image's
// width (in TILE_SIZE columns) selects how its faces are laid out:
//   1 tile  -> same texture on every face
//   2 tiles -> [top/bottom, side]
//   3 tiles -> [top, side, bottom]
// Missing or malformed files fall back to a procedurally generated texture
// so the game still renders before the user supplies their own art.

struct BlockTiles {
    int top = 0, side = 0, bottom = 0;
};

static BlockTiles tilesByBlock[(int)BlockType::Count];
static int atlasTileCount = 1;

// Water is animated in place (see UpdateWaterAnimation) rather than loaded
// from a file, so it's kept out of the BlockFileName/LoadBlockImage path
// that the other, static block textures go through.
constexpr int WATER_FRAME_COUNT = 8;
constexpr float WATER_FRAME_DURATION = 0.15f;
static Image waterFrames[WATER_FRAME_COUNT];
static Rectangle waterAtlasPixelRect{};

static const char* BlockFileName(BlockType type) {
    switch (type) {
        case BlockType::Grass:  return "assets/textures/grass.png";
        case BlockType::Dirt:   return "assets/textures/dirt.png";
        case BlockType::Stone:  return "assets/textures/stone.png";
        case BlockType::Sand:   return "assets/textures/sand.png";
        case BlockType::Wood:   return "assets/textures/wood.png";
        case BlockType::Leaves: return "assets/textures/leaves.png";
        default: return nullptr;
    }
}

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

// Two overlapping sine ripples whose phases drift with `frame`, giving a
// scrolling-water look when the frames are played back in sequence.
static void GenWaterFrame(Image& img, int frame) {
    float phase = (float)frame / WATER_FRAME_COUNT * 2.0f * PI;
    for (int y = 0; y < TILE_SIZE; y++) {
        for (int x = 0; x < TILE_SIZE; x++) {
            float wave1 = sinf(x * 0.6f + y * 0.4f + phase * 2.0f) * 0.5f + 0.5f;
            float wave2 = sinf(x * 0.35f - y * 0.5f - phase * 3.0f) * 0.5f + 0.5f;
            float brightness = 0.8f + 0.2f * (0.5f * wave1 + 0.5f * wave2);
            Color base = Color{
                (unsigned char)Clamp(45.0f * brightness, 0, 255),
                (unsigned char)Clamp(110.0f * brightness, 0, 255),
                (unsigned char)Clamp(200.0f * brightness, 0, 255),
                255
            };
            ImageDrawPixel(&img, x, y, base);
        }
    }
}

// Builds a fallback strip using the same tile-count convention as a
// user-supplied file, so the atlas-packing code below can't tell the
// difference between the two.
static Image GenerateFallbackImage(BlockType type, int& outTileCount) {
    switch (type) {
        case BlockType::Grass: {
            Image img = GenImageColor(TILE_SIZE * 3, TILE_SIZE, WHITE);
            GenGrassTop(img, 0);
            GenGrassSide(img, 1);
            GenDirt(img, 2);
            outTileCount = 3;
            return img;
        }
        case BlockType::Wood: {
            Image img = GenImageColor(TILE_SIZE * 2, TILE_SIZE, WHITE);
            GenWoodTop(img, 0);
            GenWoodSide(img, 1);
            outTileCount = 2;
            return img;
        }
        case BlockType::Dirt: {
            Image img = GenImageColor(TILE_SIZE, TILE_SIZE, WHITE);
            GenDirt(img, 0);
            outTileCount = 1;
            return img;
        }
        case BlockType::Sand: {
            Image img = GenImageColor(TILE_SIZE, TILE_SIZE, WHITE);
            GenSand(img, 0);
            outTileCount = 1;
            return img;
        }
        case BlockType::Leaves: {
            Image img = GenImageColor(TILE_SIZE, TILE_SIZE, WHITE);
            GenLeaves(img, 0);
            outTileCount = 1;
            return img;
        }
        case BlockType::Stone:
        default: {
            Image img = GenImageColor(TILE_SIZE, TILE_SIZE, WHITE);
            GenStone(img, 0);
            outTileCount = 1;
            return img;
        }
    }
}

// Loads assets/textures/<name>.png for the block, or generates a fallback
// strip if the file is missing or its dimensions don't fit the convention.
static Image LoadBlockImage(BlockType type, int& outTileCount) {
    const char* path = BlockFileName(type);
    if (path && FileExists(path)) {
        Image img = LoadImage(path);
        if (img.data && img.height == TILE_SIZE && img.width % TILE_SIZE == 0) {
            int tileCount = img.width / TILE_SIZE;
            if (tileCount >= 1 && tileCount <= 3) {
                outTileCount = tileCount;
                return img;
            }
        }
        TraceLog(LOG_WARNING, "Textures: '%s' is %dx%d, expected height %d and width a multiple of %d (1-3 tiles); using generated texture instead",
                  path, img.width, img.height, TILE_SIZE, TILE_SIZE);
        if (img.data) UnloadImage(img);
    }
    return GenerateFallbackImage(type, outTileCount);
}

Texture2D LoadBlockAtlas() {
    Image images[(int)BlockType::Count]{};
    int tileCounts[(int)BlockType::Count]{};
    int totalTiles = 0;

    for (int i = 1; i < (int)BlockType::Count; i++) {
        BlockType type = (BlockType)i;
        if (type == BlockType::Water) {
            // Animated in place afterwards; keep the frames resident so
            // UpdateWaterAnimation can patch the atlas with them later.
            for (int f = 0; f < WATER_FRAME_COUNT; f++) {
                waterFrames[f] = GenImageColor(TILE_SIZE, TILE_SIZE, WHITE);
                GenWaterFrame(waterFrames[f], f);
            }
            images[i] = ImageCopy(waterFrames[0]);
            tileCounts[i] = 1;
        } else {
            images[i] = LoadBlockImage(type, tileCounts[i]);
        }
        totalTiles += tileCounts[i];
    }

    Image atlas = GenImageColor(TILE_SIZE * totalTiles, TILE_SIZE, WHITE);
    int cursor = 0;
    for (int i = 1; i < (int)BlockType::Count; i++) {
        int tileCount = tileCounts[i];
        Rectangle srcRec = {0, 0, (float)(TILE_SIZE * tileCount), (float)TILE_SIZE};
        Vector2 dstPos = {(float)(cursor * TILE_SIZE), 0};
        ImageDrawImageRec(&atlas, images[i], srcRec, dstPos, WHITE);
        UnloadImage(images[i]);

        BlockTiles bt{};
        if (tileCount == 1) {
            bt.top = bt.side = bt.bottom = cursor;
        } else if (tileCount == 2) {
            bt.top = bt.bottom = cursor;
            bt.side = cursor + 1;
        } else {
            bt.top = cursor;
            bt.side = cursor + 1;
            bt.bottom = cursor + 2;
        }
        tilesByBlock[i] = bt;

        if ((BlockType)i == BlockType::Water) {
            waterAtlasPixelRect = { (float)(cursor * TILE_SIZE), 0.0f, (float)TILE_SIZE, (float)TILE_SIZE };
        }

        cursor += tileCount;
    }
    atlasTileCount = totalTiles > 0 ? totalTiles : 1;

    Texture2D tex = LoadTextureFromImage(atlas);
    UnloadImage(atlas);
    SetTextureFilter(tex, TEXTURE_FILTER_POINT);
    return tex;
}

Rectangle GetTileUV(BlockType type, Face face) {
    const BlockTiles& bt = tilesByBlock[(int)type];
    int tileIndex = bt.side;
    if (face == Face::PosY) tileIndex = bt.top;
    else if (face == Face::NegY) tileIndex = bt.bottom;

    float u0 = (float)tileIndex / atlasTileCount;
    float w = 1.0f / atlasTileCount;
    return Rectangle{u0, 0.0f, w, 1.0f};
}

void UpdateWaterAnimation(Texture2D atlas, float dt) {
    static float accum = 0.0f;
    static int frame = 0;
    accum += dt;
    bool changed = false;
    while (accum >= WATER_FRAME_DURATION) {
        accum -= WATER_FRAME_DURATION;
        frame = (frame + 1) % WATER_FRAME_COUNT;
        changed = true;
    }
    if (changed) {
        UpdateTextureRec(atlas, waterAtlasPixelRect, waterFrames[frame].data);
    }
}
