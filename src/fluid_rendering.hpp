#pragma once
#include <vector>
#include <numeric>
#include <iostream>
#include <raylib.h>
#include <raymath.h>

#include "multi_mesh_renderer.hpp"
#include "simulation_state.hpp"

struct FluidRenderer{
public:

    // Live-tunable settings. Shader uniforms come first so that id < P_SHADER_COUNT means "uniform of the
    // same name in fluid_composite.fs". KEEP THIS ORDER IN SYNC with the P table in Controls.tsx.
    enum RenderParam {
        P_THRESHOLD = 0,    // field value where the surface sits (~half of the interior field value)
        P_EDGE_WIDTH,       // anti-aliasing band at the edge, in field texels (smaller = sharper)
        P_NORMAL_SAMPLE,    // gradient step in texels (larger = less noisy bevel direction)
        P_BEVEL_WIDTH,      // width of the raised rim, in texels
        P_NORMAL_STRENGTH,  // rim steepness (0 = flat)
        P_OUTLINE_WIDTH,    // dark outline width, in texels
        P_OUTLINE_DARKEN,   // how dark the outline is
        P_LIGHT_X, P_LIGHT_Y, P_LIGHT_Z,
        P_SHADE_STRENGTH,   // how strongly the rim is lit/shadowed
        P_SPEC_EXP, P_SPEC_STRENGTH,
        P_BRIGHTNESS,
        P_SHADER_COUNT,

        P_FIELD_SCALE = P_SHADER_COUNT,  // field render-target resolution (1 = pixel-exact)
        P_BLOB_POWER,       // blob kernel (1-r^2)^power: higher = tighter, smaller blobs
        P_RESET_RATE,       // diffusion mode: how fast foam fades back to base color
        P_DIFFUSION_RATIO,  // diffusion mode: relative density below which foam appears
        P_BASE_R, P_BASE_G, P_BASE_B,
        P_FOAM_R, P_FOAM_G, P_FOAM_B,
        P_COUNT
    };

    void setParam(int id, float v);
    float getParam(int id) const;
    void RegenerateBlobTexture();
    Image MakeBlobImage();
    RenderTexture2D MakeFieldTarget(int w, int h);
    void ResizeField(float scale);
    void UpdateTexelUniform();

    int GetNumMeshesFrom(int numParticles);

    float lerp(float a, float b, float alpha);

    float CalculateInterpolation();

    void InterpolatePositions(float alpha);

    FluidRenderer(FluidState &fas);
    ~FluidRenderer();

    void initializeColorMaps();

    void writePosition(int vertexIdx, float left, float top, float right, float bottom, std::vector<float>& verts);

    void writePositions(int start, int end);

    void UpdateParticlePositionsMulti(int start, int end);

    void GetParticleDiffusionColor(int particleIdx, int &r, int &g, int &b);

    void GetParticleVelocityColor(int particleIdx, int &r, int &g, int &b);

    void GetParticleVorticityColor(int particleIdx, int &r, int &g, int &b);

    void GetParticleTemperatureColor(int particleIdx, int &r, int &g, int &b);

    void GetParticleDensityColor(int particleIdx, int &r, int &g, int &b);

    void GetParticleDebugColor(int particleIdx, int &r, int &g, int &b);

    void writeColor(int vertexIdx, int r, int g, int b);

    void writeColors(int start, int end);

    void UpdateParticleColorsMulti(int start, int end);

    void ResizeAndUpdateMesh(int newCount);

    void replaceParticleWithEnd(int removeIdx, int endIdx);

    void DrawParticles();

    void DrawFluidSmooth();

    void render_fluid();

    void setRenderMode(int newMode);

    int getRenderMode();

    int getColorOverlap();
    void setColorOverlap(int set);

    float getRadiusMul();
    void setRadiusMult(float set);



    FluidState &fluid_attributes;
    int n;
    float radius;
    float invSpacing;
    int32_t renderMode = 0;

    float resetRate = 2.f;
    float diffusionRatio = 0.71f; // 0.75

    std::array<int, 3> baseColor {0, 0, 255};
    std::array<int, 3> foamColor {204, 204, 255};
    
    Material particleMaterial = {0};
    Texture2D particleTexture = {0};
    Vector2 particle_texture_size;

    MultiMeshRenderer particleMesh;

    std::vector<float> particleVertices = {0};
    std::vector<float> particleTexCoords = {0};
    std::vector<unsigned char> particleColors = {0};
    std::vector<unsigned short> particleIndices = {0};

    std::vector<float> particleDiffusionColors;

    std::array<std::array<int, 3>, 100> velGradient;
    std::array<std::array<int, 3>, 4> velColorMap {{{50, 0, 255}, {225, 0, 225}, {255, 225, 100}, {255, 255, 125}}};

    std::array<std::array<int, 3>, 100> vortGradient;
    std::array<std::array<int, 3>, 4> vortColorMap {{{50, 0, 255}, {200, 0, 200}, {255, 200, 80}, {255, 255, 100}}};

    std::array<std::array<int, 3>, 100> tempGradient;
    std::array<std::array<int, 3>, 4> tempMap{{{0, 0, 0}, {204, 51, 0}, {255, 102, 0}, {255, 255, 102}}};

    std::array<std::array<int, 3>, 100> densityGradient;
    std::array<std::array<int, 3>, 4> densityMap{{{255, 255, 0}, {150, 255, 0}, {80, 255, 0}, {255, 0, 0}}};
        // some nice gradients to put into these colorMaps:
        // velocity:
            // sunset: {50, 0, 255}, {225, 0, 225}, {255, 225, 100}, {255, 255, 125}
            // scientific: {0, 150, 255}, {0, 255, 0}, {255, 255, 0}, {255, 0, 0}
            // night ocean: {0, 0, 100},{0, 180, 255}, {100, 255, 255}, {255, 255, 255}
            // ocean: {0, 60, 130}, {0, 180, 255}, {180, 255, 255}, {240, 240, 240}
            // bright ocean: {100, 150, 255}, {100, 255, 255}, {255, 255, 255}, {255, 255, 255}
            // plasma: {30, 0, 70}, {120, 0, 255}, {255, 150, 255}, {255, 255, 255}
            // plasma neon: {0, 0, 100}, {128, 0, 255}, {255, 0, 128}, {255, 255, 255}
            // bright plasma neon: {80, 0, 225}, {128, 0, 255}, {255, 0, 128}, {255, 255, 255}
            // deep ocean: {0, 10, 60}, {0, 80, 180}, {0, 180, 220}, {200, 255, 255}
        // vorticity:
            // sunset: {50, 0, 255}, {200, 0, 200}, {255, 200, 80}, {255, 255, 100}
        // temperature:
            // fire: {0, 0, 0}, {204, 51, 0}, {255, 102, 0}, {255, 255, 102}
            // sunset: {50, 0, 255}, {200, 0, 200}, {255, 200, 80}, {255, 255, 100}

    RenderTexture2D fieldRT = {0};
    Texture2D blobTex = {0};
    Material blobMaterial = {0};
    Shader compositeShader = {0};
    float fieldScale = 1.f; // 0.5
    int texelLoc;
    float blobPower = 2.f;
    bool floatField = false;   // true if the field target is half-float (set by MakeFieldTarget)
    float params[P_COUNT] = {0};
    int paramLoc[P_SHADER_COUNT] = {0};

    unsigned char colorOverlap = 75;
    float radiusMul = 4.4f;

};