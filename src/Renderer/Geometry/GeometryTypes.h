#pragma once

struct Vec3
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Vec2
{
    float x = 0.0f;
    float y = 0.0f;
};

enum class RenderDebugVisualMode : int
{
    Normals = 0,
    Off = 1,
};
