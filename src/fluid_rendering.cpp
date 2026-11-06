#include "fluid_rendering.hpp"
#include <stdexcept>
#include <cstring>
#include <rlgl.h>
#include <algorithm>
#include <cmath>

/*static const char *kShaderParamNames[FluidRenderer::P_SHADER_COUNT] = {
    "threshold", "edgeWidth", "normalSample", "bevelWidth", "normalStrength",
    "outlineWidth", "outlineDarken",
    "lightX", "lightY", "lightZ",
    "shadeStrength",
    "specExp", "specStrength",
    "brightness"
};

static const float kShaderParamDefaults[FluidRenderer::P_SHADER_COUNT] = {
    0.5f, 1.0f, 2.0f, 8.0f, 0.9f,
    1.5f, 0.45f,
    -0.6f, 0.55f, 0.58f,
    1.0f,
    30.0f, 0.5f,
    1.0f
};*/

FluidRenderer::FluidRenderer(FluidState &fas): fluid_attributes(fas)
{
    int numParticles = fluid_attributes.num_particles;
    n = fluid_attributes.numY;
    radius = fluid_attributes.radius * 1.25f;
    invSpacing = 1.f / fluid_attributes.cellSpacing;

    ResizeAndUpdateMesh(fluid_attributes.num_particles);

    for (int i = 0; i < numParticles; ++i) {
        for (int k = 0; k < 3; ++k) particleDiffusionColors[3 * i + k] = baseColor[k];
    }

    if (!FileExists("white_circle.png")) {
        throw std::runtime_error("white_circle.png does not exist");
    }
    particleTexture = LoadTexture("white_circle.png");
        
    particle_texture_size = {static_cast<float>(particleTexture.width), static_cast<float>(particleTexture.height)};

    particleMaterial = LoadMaterialDefault();
    particleMaterial.maps[MATERIAL_MAP_DIFFUSE].texture = particleTexture;

    initializeColorMaps();

    /*radius = fluid_attributes.radius * radiusMul;   // bigger sprites so blobs overlap and merge

    fieldRT = MakeFieldTarget((int)(GetScreenWidth() * fieldScale), (int)(GetScreenHeight() * fieldScale));

    // metaball kernel stored at full 8-bit range; overall strength comes from the material tint below
    Image img = MakeBlobImage();
    blobTex = LoadTextureFromImage(img);
    UnloadImage(img);
    SetTextureFilter(blobTex, TEXTURE_FILTER_BILINEAR);

    blobMaterial = LoadMaterialDefault();
    blobMaterial.maps[MATERIAL_MAP_DIFFUSE].texture = blobTex;
    blobMaterial.maps[MATERIAL_MAP_DIFFUSE].color = Color{colorOverlap, colorOverlap, colorOverlap, colorOverlap};
    
    compositeShader = LoadShader(0, "fluid_composite.fs");   // default vertex shader
    texelLoc = GetShaderLocation(compositeShader, "texel");
    UpdateTexelUniform();

    for (int id = 0; id < P_SHADER_COUNT; ++id) {
        paramLoc[id] = GetShaderLocation(compositeShader, kShaderParamNames[id]);
        params[id] = kShaderParamDefaults[id];
        SetShaderValue(compositeShader, paramLoc[id], &params[id], SHADER_UNIFORM_FLOAT);
    }

    // non-shader params mirror the member defaults
    params[P_FIELD_SCALE]     = fieldScale;
    params[P_BLOB_POWER]      = blobPower;
    params[P_RESET_RATE]      = resetRate;
    params[P_DIFFUSION_RATIO] = diffusionRatio;
    for (int k = 0; k < 3; ++k) {
        params[P_BASE_R + k] = baseColor[k];
        params[P_FOAM_R + k] = foamColor[k];
    }*/
}


FluidRenderer::~FluidRenderer() {
    UnloadTexture(particleTexture);
    //UnloadRenderTexture(fieldRT);
    //UnloadTexture(blobTex);
    //UnloadShader(compositeShader);
}



float FluidRenderer::lerp(float a, float b, float alpha) {
    return (1.f - alpha) * a + b * alpha;
}



float FluidRenderer::CalculateInterpolation() {
    return fluid_attributes.frame_context.setDT / fluid_attributes.frame_context.trueDT;
}



void FluidRenderer::InterpolatePositions(float alpha) {
    for (int i = 0; i < fluid_attributes.num_particles; ++i) {
        fluid_attributes.renderPositions[2 * i] = lerp(
            fluid_attributes.renderPositions[2 * i],
            fluid_attributes.positions[2 * i],
            alpha
        );
        fluid_attributes.renderPositions[2 * i + 1] = lerp(
            fluid_attributes.renderPositions[2 * i + 1],
            fluid_attributes.positions[2 * i + 1],
            alpha
        );
    }
}



void FluidRenderer::initializeColorMaps() {
    // lerp between the values in colorMap to create a gradient array 
    float num_colors = velColorMap.size() - 1; // number of colors - 1
    float num_steps = 1.f * velGradient.size() / num_colors; //num_steps = 50 * key_range
    int index = 0;
    for (int i = 0; i < num_colors; ++i) {  
        for (int x = 0; x < num_steps; ++x) {
            float t = 1.f * x / num_steps;  // Interpolation factor
            // lerp for r, g, b values between colorMap[i] and colorMap [i+1]
            int r = (int)(velColorMap[i][0] * (1 - t) + velColorMap[i + 1][0] * t);
            int g = (int)(velColorMap[i][1] * (1 - t) + velColorMap[i + 1][1] * t);
            int b = (int)(velColorMap[i][2] * (1 - t) + velColorMap[i + 1][2] * t);
            velGradient[index] = std::array<int, 3>{r, g, b};

            r = (int)(tempMap[i][0] * (1 - t) + tempMap[i + 1][0] * t);
            g = (int)(tempMap[i][1] * (1 - t) + tempMap[i + 1][1] * t);
            b = (int)(tempMap[i][2] * (1 - t) + tempMap[i + 1][2] * t);
            tempGradient[index] = std::array<int, 3>{r, g, b};

            r = (int)(vortColorMap[i][0] * (1 - t) + vortColorMap[i + 1][0] * t);
            g = (int)(vortColorMap[i][1] * (1 - t) + vortColorMap[i + 1][1] * t);
            b = (int)(vortColorMap[i][2] * (1 - t) + vortColorMap[i + 1][2] * t);
            vortGradient[index] = std::array<int, 3>{r, g, b};

            r = (int)(densityMap[i][0] * (1 - t) + densityMap[i + 1][0] * t);
            g = (int)(densityMap[i][1] * (1 - t) + densityMap[i + 1][1] * t);
            b = (int)(densityMap[i][2] * (1 - t) + densityMap[i + 1][2] * t);
            densityGradient[index] = std::array<int, 3>{r, g, b};

            index++;
        }
    }
}



// positions ----------------------------------------------------------------------------------------------------------
void FluidRenderer::writePosition(int vertexIdx, float left, float top, float right, float bottom, std::vector<float>& verts) {
    // top left
    verts[3 * vertexIdx] =  left;
    verts[3 * vertexIdx + 1] =  top;
    verts[3 * vertexIdx + 2] =  0.0f;

    // top right
    verts[3 * (vertexIdx + 1)] = right;
    verts[3 * (vertexIdx + 1) + 1] = top;
    verts[3 * (vertexIdx + 1) + 2] = 0.0f;

    // bottom right
    verts[3 * (vertexIdx + 2)] = right;
    verts[3 * (vertexIdx + 2) + 1] = bottom;
    verts[3 * (vertexIdx + 2) + 2] = 0.0f;

    // bottom left
    verts[3 * (vertexIdx + 3)] = left;
    verts[3 * (vertexIdx + 3) + 1] = bottom;
    verts[3 * (vertexIdx + 3) + 2] = 0.0f;
}



void FluidRenderer::writePositions(int start, int end) {
    for (int i = start; i < end; ++i) {
        int quadIdx = i;
        int vertexIdx = quadIdx * 4;

        float px = fluid_attributes.renderPositions[2 * i];
        float py = fluid_attributes.renderPositions[2 * i + 1];

        float left = px - radius;
        float right = px + radius;
        float top = py + radius;
        float bottom = py - radius;

        writePosition(vertexIdx, left, top, right, bottom, particleVertices);
    }
}



void FluidRenderer::UpdateParticlePositionsMulti(int start, int end) {
    writePositions(start, end);
}
// ------------------------------------------------------------------------------------------------------------



// colors ------------------------------------------------------------------------------------------------------------
void FluidRenderer::GetParticleDiffusionColor(int particleIdx, int &r, int &g, int &b) {
    // fade each channel toward the base water color
    for (int k = 0; k < 3; ++k) {
        float &ch = particleDiffusionColors[3 * particleIdx + k];
        const float target = baseColor[k];
        if (ch < target)      ch = std::min(ch + resetRate, target);
        else if (ch > target) ch = std::max(ch - resetRate, target);
    }

    const int xi = clamp(std::floor(fluid_attributes.positions[2 * particleIdx] * invSpacing), 1, fluid_attributes.numX - 1);
    const int yi = clamp(std::floor(fluid_attributes.positions[2 * particleIdx + 1] * invSpacing), 1, fluid_attributes.numY - 1);
    const int cellNr = xi * n + yi;

    // this just says, if a particle is near a wall, don't let it become white. Shouldn't need this in a perfect fluid sim
    // (fixes problem of particles always being white next to walls because boundary is pushing them too far out
    //  and making near-solid walls have too little density)
    auto &cellTypes = fluid_attributes.cellType;
    if (cellTypes[cellNr - fluid_attributes.numY] == fluid_attributes.SOLID ||
        cellTypes[cellNr + fluid_attributes.numY] == fluid_attributes.SOLID ||
        cellTypes[cellNr - 1]                     == fluid_attributes.SOLID ||
        cellTypes[cellNr + 1]                     == fluid_attributes.SOLID) 
    {
        r = particleDiffusionColors[3 * particleIdx];
        g = particleDiffusionColors[3 * particleIdx + 1];
        b = particleDiffusionColors[3 * particleIdx + 2];   
        return;
    }

    const float d0 = fluid_attributes.particleRestDensity;
    if (d0 > 0.f && fluid_attributes.particleAges[particleIdx] > fluid_attributes.age_constant - 1) {
        const float relDensity = this->fluid_attributes.cellDensities[cellNr] / d0;
        if (relDensity > 0 && relDensity < diffusionRatio) { 
            for (int k = 0; k < 3; ++k) particleDiffusionColors[3 * particleIdx + k] = foamColor[k];
        }
    }

    r = particleDiffusionColors[3 * particleIdx];
    g = particleDiffusionColors[3 * particleIdx + 1];
    b = particleDiffusionColors[3 * particleIdx + 2];
}



void FluidRenderer::GetParticleVelocityColor(int particleIdx, int &r, int &g, int &b) {
    int vel = static_cast<int>((fluid_attributes.velocities[2 * particleIdx] * fluid_attributes.velocities[2 * particleIdx] + fluid_attributes.velocities[2 * particleIdx + 1] * fluid_attributes.velocities[2 * particleIdx + 1]) / 15000); // 15000
            
    vel = clamp(vel, 0, velGradient.size()); 
            
    r = velGradient[vel][0];
    g = velGradient[vel][1];
    b = velGradient[vel][2];
}



void FluidRenderer::GetParticleVorticityColor(int particleIdx, int &r, int &g, int &b) {
    int cellX = fluid_attributes.positions[2 * particleIdx] / fluid_attributes.cellSpacing;
    int cellY = fluid_attributes.positions[2 * particleIdx + 1] / fluid_attributes.cellSpacing;

    int vort = static_cast<int>(fluid_attributes.calcVorticity(cellX, cellY)); // * 5 for curl

    vort = clamp(vort, 0, vortGradient.size());

    r = vortGradient[vort][0];
    g = vortGradient[vort][1];
    b = vortGradient[vort][2];
}



void FluidRenderer::GetParticleTemperatureColor(int particleIdx, int &r, int &g, int &b) {
    float temp = std::abs(fluid_attributes.temperatures[particleIdx]);

    if (temp < 30 && fluid_attributes.fireActive) {
        temp = 0.f;
    }

    size_t tempidx = std::min(tempGradient.size() - 1, static_cast<size_t>(temp));

    r = tempGradient[tempidx][0];
    g = tempGradient[tempidx][1];
    b = tempGradient[tempidx][2];
}



void FluidRenderer::GetParticleDensityColor(int particleIdx, int &r, int &g, int &b) {
    int cellX = fluid_attributes.positions[2 * particleIdx] / fluid_attributes.cellSpacing;
    int cellY = fluid_attributes.positions[2 * particleIdx + 1] / fluid_attributes.cellSpacing;

    // normal
    int densityIdx = static_cast<int>(fluid_attributes.cellDensities[cellX * fluid_attributes.n + cellY]) * 25;

    // density projection
    //int halfIdx = densityGradient.size() / 2;
    //int offset = 1.f - fluid_attributes.cellDensities[cellX * fluid_attributes.n + cellY];
    //int densityIdx = halfIdx - offset * 35;

    int idx = clamp(densityIdx, 0, densityGradient.size() - 1);

    r = densityGradient[idx][0];
    g = densityGradient[idx][1];
    b = densityGradient[idx][2];
}


/*void FluidRenderer::GetParticleDebugColor(int particleIdx, int &r, int &g, int &b) {
    if (fluid_attributes.debug[particleIdx]) {
        r = 0;
        g = 255;
        b = 0;
    }
    else {
        r = 255;
        g = 0;
        b = 0;
    }
}*/


void FluidRenderer::writeColor(int vertexIdx, int r, int g, int b) {
    for (int v = 0; v < 4; ++v) {
        particleColors[4 * (vertexIdx + v)] = r;
        particleColors[4 * (vertexIdx + v) + 1] = g;
        particleColors[4 * (vertexIdx + v) + 2] = b;
        particleColors[4 * (vertexIdx + v) + 3] = 255;
    }
}



void FluidRenderer::writeColors(int start, int end) {
    for (int i = start; i < end; ++i) {
        int quadIdx = i;
        int vertexIdx = quadIdx * 4;

        int r = 0;
        int g = 0;
        int b = 0;
        switch (renderMode) {
            case 0:
                GetParticleDiffusionColor(i, r, g, b);
                break;
            case 1:
                GetParticleVelocityColor(i, r, g, b);
                break;
            case 2:
                GetParticleVorticityColor(i, r, g, b);
                break;
            case 3:
                GetParticleTemperatureColor(i, r, g, b);
                break;
            case 4:
                GetParticleDensityColor(i, r, g, b);
                break;
            /*case 5:
                GetParticleDebugColor(i, r, g, b);
                break;*/
        }

        writeColor(vertexIdx, r, g, b);
    }
}



void FluidRenderer::UpdateParticleColorsMulti(int start, int end) {
    writeColors(start, end);
}
// ------------------------------------------------------------------------------------------------------------



void FluidRenderer::ResizeAndUpdateMesh(int newCount) {

    int oldCount = particleVertices.size() / 12;

    if (oldCount == newCount) return;

    particleVertices.resize(newCount * 4 * 3);   // 4 vertices per Quad, 3 floats per vertex
    particleTexCoords.resize(newCount * 4 * 2);
    particleColors.resize(newCount * 4 * 4);
    particleIndices.resize(newCount * 6);

    particleDiffusionColors.resize(3 * newCount);
    // debug_condition.resize(newCount);


    // takes care of info for ALL MESHES; particleIndices, particleTexCoords, particlePositions, and ParticleColors exist outside of the master mesh structure
    InitializeQuadsMulti(oldCount, newCount, particleIndices, particleTexCoords);
    UpdateParticlePositionsMulti(oldCount, newCount);
    UpdateParticleColorsMulti(oldCount, newCount);

    // take care of remaking the meshes structure and updating all counts
    particleMesh.ResizeAndUpdateMesh(oldCount, newCount, particleVertices, particleTexCoords, particleIndices, particleColors);
}



void FluidRenderer::replaceParticleWithEnd(int removeIdx, int endIdx) {
    float replacingPx = fluid_attributes.positions[2 * endIdx];
    float replacingPy = fluid_attributes.positions[2 * endIdx + 1];

    int replacedVertexIdx = removeIdx * 4;

    float left = replacingPx - radius;
    float right = replacingPx + radius;
    float top = replacingPy + radius;
    float bottom = replacingPy - radius;

    writePosition(replacedVertexIdx, left, top, right, bottom, particleVertices);
}



void FluidRenderer::DrawParticles() {
    particleMesh.DrawQuads(particleMaterial, fluid_attributes.frame_context.transform);
}



void FluidRenderer::DrawFluidSmooth() {
    // same transform you already use, scaled to the smaller target
    Matrix m = MatrixMultiply(fluid_attributes.frame_context.transform,
                              MatrixScale(fieldScale, fieldScale, 1.f));

    BeginTextureMode(fieldRT);
        ClearBackground(BLANK);
        rlSetBlendFactors(RL_ONE, RL_ONE, RL_FUNC_ADD);   // pure additive, including alpha
        BeginBlendMode(BLEND_CUSTOM);
            particleMesh.DrawQuads(blobMaterial, m);
        EndBlendMode();
    EndTextureMode();

    BeginShaderMode(compositeShader);
        DrawTexturePro(fieldRT.texture,
                       {0, 0, (float)fieldRT.texture.width, -(float)fieldRT.texture.height},
                       {0, 0, (float)GetScreenWidth(), (float)GetScreenHeight()},
                       {0, 0}, 0.f, WHITE);
    EndShaderMode();
}



void FluidRenderer::render_fluid() {
    float alpha = CalculateInterpolation();
    InterpolatePositions(alpha);

    UpdateParticlePositionsMulti(0, fluid_attributes.num_particles);
    UpdateParticleColorsMulti(0, fluid_attributes.num_particles);

    // update mesh buffers
    particleMesh.UpdatePositionMeshBuffers(particleVertices);
    particleMesh.UpdateColorMeshBuffers(particleColors);

    DrawParticles();
    //DrawFluidSmooth();

    std::copy(begin(fluid_attributes.positions), end(fluid_attributes.positions), begin(fluid_attributes.renderPositions));
    //std::fill(begin(fluid_attributes.debug), end(fluid_attributes.debug), false);
}



void FluidRenderer::setRenderMode(int newMode) {
    renderMode = newMode;
}

int FluidRenderer::getRenderMode() {
    return renderMode;
}

int FluidRenderer::getColorOverlap() {
    return colorOverlap;
}

void FluidRenderer::setColorOverlap(int set) {
    // strength is a tint, applied in float math by the shader, so low values keep full precision
    colorOverlap = (unsigned char)clamp(set, 1, 255);
    blobMaterial.maps[MATERIAL_MAP_DIFFUSE].color = Color{colorOverlap, colorOverlap, colorOverlap, colorOverlap};
}

// Smooth metaball kernel k(r) = (1 - r^2)^power, written to rgb and alpha at full range.
// It reaches exactly zero at the sprite edge, so neighboring blobs merge with smooth necks.
Image FluidRenderer::MakeBlobImage() {
    const int S = 64;
    Image img = GenImageColor(S, S, BLANK);
    Color *px = (Color *)img.data;
    for (int y = 0; y < S; ++y) {
        for (int x = 0; x < S; ++x) {
            const float fx = (x + 0.5f) / S * 2.f - 1.f;
            const float fy = (y + 0.5f) / S * 2.f - 1.f;
            const float r2 = fx * fx + fy * fy;
            const float k = r2 < 1.f ? std::pow(1.f - r2, blobPower) : 0.f;
            const unsigned char v = (unsigned char)std::lround(k * 255.f);
            px[y * S + x] = Color{v, v, v, v};
        }
    }
    return img;
}

// Rebuilds the kernel pixels in place (same size/format, so no new GPU texture is allocated).
void FluidRenderer::RegenerateBlobTexture() {
    Image img = MakeBlobImage();
    UpdateTexture(blobTex, img.data);
    UnloadImage(img);
}

// Field render target. Prefers a half-float color buffer so the accumulated field can exceed 1.0
// (no clipped interiors or color shifts) and weak per-particle contributions aren't rounded away.
// Falls back to the normal 8-bit target if the platform can't render to it.
RenderTexture2D FluidRenderer::MakeFieldTarget(int w, int h) {
    RenderTexture2D rt = LoadRenderTexture(w, h);

    unsigned int fid = rlLoadTexture(NULL, w, h, RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16A16, 1);
    if (fid != 0) {
        rlFramebufferAttach(rt.id, fid, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
        if (rlFramebufferComplete(rt.id)) {
            rlUnloadTexture(rt.texture.id);   // drop the original RGBA8 buffer
            rt.texture.id = fid;
            rt.texture.format = RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16A16;
            floatField = true;
            SetTextureFilter(rt.texture, TEXTURE_FILTER_BILINEAR);
            return rt;
        }
        // not renderable here: reattach the 8-bit buffer and carry on
        rlFramebufferAttach(rt.id, rt.texture.id, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
        rlUnloadTexture(fid);
    }
    floatField = false;
    SetTextureFilter(rt.texture, TEXTURE_FILTER_BILINEAR);
    return rt;
}

void FluidRenderer::UpdateTexelUniform() {
    Vector2 texel = {1.f / fieldRT.texture.width, 1.f / fieldRT.texture.height};
    SetShaderValue(compositeShader, texelLoc, &texel, SHADER_UNIFORM_VEC2);
}

// Recreates the field render target at a new resolution (scale of the screen size).
void FluidRenderer::ResizeField(float scale) {
    scale = clamp(scale, 0.1f, 1.f);
    params[P_FIELD_SCALE] = scale;
    if (scale == fieldScale) return;
    fieldScale = scale;

    UnloadRenderTexture(fieldRT);
    fieldRT = MakeFieldTarget((int)(GetScreenWidth() * fieldScale), (int)(GetScreenHeight() * fieldScale));
    UpdateTexelUniform();   // texel size depends on the target size
}

void FluidRenderer::setParam(int id, float v) {
    if (id < 0 || id >= P_COUNT || !std::isfinite(v)) return;
    params[id] = v;

    if (id < P_SHADER_COUNT) {
        SetShaderValue(compositeShader, paramLoc[id], &v, SHADER_UNIFORM_FLOAT);
        return;
    }

    switch (id) {
        case P_FIELD_SCALE:     ResizeField(v); break;
        case P_BLOB_POWER:
            blobPower = clamp(v, 0.5f, 8.f);
            params[id] = blobPower;
            RegenerateBlobTexture();
            break;
        case P_RESET_RATE:      resetRate = v; break;
        case P_DIFFUSION_RATIO: diffusionRatio = v; break;
        case P_BASE_R: case P_BASE_G: case P_BASE_B: baseColor[id - P_BASE_R] = v; break;
        case P_FOAM_R: case P_FOAM_G: case P_FOAM_B: foamColor[id - P_FOAM_R] = v; break;
        default: break;
    }
}

float FluidRenderer::getParam(int id) const {
    if (id < 0 || id >= P_COUNT) return 0.f;
    return params[id];
}

float FluidRenderer::getRadiusMul() {
    return radiusMul;
}

void FluidRenderer::setRadiusMult(float set) {
    radius = radius / radiusMul;
    radiusMul = set;
    radius = radius * radiusMul;
}