#pragma once
#include "raylib.h"
#include "Block.h"

constexpr int TILE_SIZE = 16;

Texture2D LoadBlockAtlas();
Rectangle GetTileUV(BlockType type, Face face);

// Advances the water tile's animation by dt and, if the frame changed,
// patches its region of the atlas in place via UpdateTextureRec.
void UpdateWaterAnimation(Texture2D atlas, float dt);
