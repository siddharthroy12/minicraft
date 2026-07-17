#pragma once
#include "raylib.h"
#include "Block.h"

constexpr int TILE_SIZE = 16;

Texture2D LoadBlockAtlas();
Rectangle GetTileUV(BlockType type, Face face);
