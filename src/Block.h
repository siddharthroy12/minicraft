#pragma once
#include "raylib.h"
#include "raymath.h"

enum class BlockType : unsigned char {
    Air = 0,
    Grass,
    Dirt,
    Stone,
    Sand,
    Wood,
    Leaves,
    Water,
    Count
};

enum class Face { PosX, NegX, PosY, NegY, PosZ, NegZ };

// Per-vertex tint multiplied with the atlas texture to fake directional lighting.
inline Color GetFaceShade(Face face) {
    float mul = 1.0f;
    switch (face) {
        case Face::PosY: mul = 1.0f; break;
        case Face::NegY: mul = 0.55f; break;
        case Face::PosX: case Face::NegX: mul = 0.85f; break;
        case Face::PosZ: case Face::NegZ: mul = 0.7f; break;
    }
    unsigned char v = (unsigned char)Clamp(255.0f * mul, 0, 255);
    return Color{v, v, v, 255};
}
