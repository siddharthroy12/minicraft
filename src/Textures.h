#pragma once
#include "raylib.h"
#include "Block.h"

constexpr int TILE_SIZE = 16;
constexpr int ATLAS_TILE_COUNT = 8;

enum TileIndex {
    TILE_GRASS_TOP = 0,
    TILE_GRASS_SIDE,
    TILE_DIRT,
    TILE_STONE,
    TILE_SAND,
    TILE_WOOD_TOP,
    TILE_WOOD_SIDE,
    TILE_LEAVES,
    TILE_COUNT
};

Texture2D LoadBlockAtlas();
int GetTileIndex(BlockType type, Face face);
Rectangle GetTileUV(int tileIndex);
