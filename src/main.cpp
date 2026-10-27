#include <raylib.h>
#include <iostream>
#include <emscripten.h>
#include <emscripten/html5.h>

#include "scene_handler.hpp"

static SceneHandler* g_scene_handler = nullptr;

static bool emscripten_loop(double time, void* userData) {
    return g_scene_handler->simulate(time, userData);
}

// export methods to:
//      change size of brushes
//      change strength of force object
//      

extern "C" {
    EMSCRIPTEN_KEEPALIVE
    bool getStop() {
        return g_scene_handler->fluid_attributes.stop;
    }
    EMSCRIPTEN_KEEPALIVE
    void setStop(bool set) {
        g_scene_handler->fluid_attributes.stop = set;
    }
    EMSCRIPTEN_KEEPALIVE
    bool getStep() {
        return g_scene_handler->fluid_attributes.step;
    }
    EMSCRIPTEN_KEEPALIVE
    void requestStep() {
        g_scene_handler->fluid_attributes.step = true;
    }
    EMSCRIPTEN_KEEPALIVE
    void setVorticityStrength(float set) {
        g_scene_handler->fluid_attributes.vorticityStrength = set;
    }
    EMSCRIPTEN_KEEPALIVE
    float getVorticityStrength() {
        return g_scene_handler->fluid_attributes.vorticityStrength;
    }
    EMSCRIPTEN_KEEPALIVE
    float getFlipRatio() {
        return g_scene_handler->fluid_attributes.flipRatio;
    }
    EMSCRIPTEN_KEEPALIVE
    void setFlipRatio(float set) {
        g_scene_handler->fluid_attributes.flipRatio = set;
    }
    EMSCRIPTEN_KEEPALIVE
    float getApicRatio() {
        return g_scene_handler->fluid_attributes.apicRatio;
    }
    EMSCRIPTEN_KEEPALIVE
    void setApicRatio(float set) {
        g_scene_handler->fluid_attributes.apicRatio = set;
    }
    EMSCRIPTEN_KEEPALIVE
    void setGravityX(float set) {
        g_scene_handler->fluid_attributes.gravityX = set;
    }
    EMSCRIPTEN_KEEPALIVE
    float getGravityX() {
        return g_scene_handler->fluid_attributes.gravityX;
    }
    EMSCRIPTEN_KEEPALIVE
    void setGravityY(float set) {
        g_scene_handler->fluid_attributes.gravityY = set;
    }
    EMSCRIPTEN_KEEPALIVE
    float getGravityY() {
        return g_scene_handler->fluid_attributes.gravityY;
    }
    EMSCRIPTEN_KEEPALIVE
    void setFireActive(bool active) {
        g_scene_handler->fluid_attributes.fireActive = active;
    }
    EMSCRIPTEN_KEEPALIVE
    float getFireActive() {
        return g_scene_handler->fluid_attributes.fireActive;
    }
    EMSCRIPTEN_KEEPALIVE
    int getRenderMode() {
        return g_scene_handler->scene_renderer.fluid_renderer.renderMode;
    }
    EMSCRIPTEN_KEEPALIVE
    int setRenderMode(int set) {
        return g_scene_handler->scene_renderer.fluid_renderer.renderMode = set;
    }
    EMSCRIPTEN_KEEPALIVE
    int getActiveObject() {
        return static_cast<int>(g_scene_handler->fluid_attributes.activeTool);
    }
    EMSCRIPTEN_KEEPALIVE
    void setActiveObject(int i) {
        g_scene_handler->fluid_attributes.activeTool = static_cast<Tool>(i);
    }
    EMSCRIPTEN_KEEPALIVE
    void setBrushSize(float v) {
        g_scene_handler->fluid_handler.setObjectRadius(v); 
    }
    EMSCRIPTEN_KEEPALIVE
    float getBrushSize() { 
        return g_scene_handler->fluid_handler.objectSimRadius; 
    }
    EMSCRIPTEN_KEEPALIVE
    void setForceStrength(float v) { 
        g_scene_handler->fluid_handler.setForceObjectStrength(v); 
    }
    EMSCRIPTEN_KEEPALIVE
    float getForceStrength() { 
        return g_scene_handler->fluid_handler.forceObjectStrength; 
    }
}

int main() 
{

    // SETTINGS
    // ---------------------------------------
    // numparticles and gravity are self explanatory
    // gridNumX is how large the horizontal area is
    // flipRatio is the blend between PIC and FLIP, expects a float from [0.f, 1.f].
    // vorticitystrength is how strong vorticity confinement forces are if you choose to include that in the sim 
    // maxFps sets the deltaTime for the simulation and the framerate for the window

    int numParticles = 30000; // 30000 --- start with a kinda large number so that good density sample is taken at the start of the simulation
    float gravityY = 9.8f; // 5500
    float gravityX = 0.f;
    int gridNumX = 258; // gridNumX must be a power of 2, +2
    ///int gridNumY = 180; gridNumX / 2 + 1;  <-- use for when you implement multigrid
    float flipRatio = 0.3f; // 0.9f
    float apicRatio = 1.f;
    float vorticityStrength = 30.f;
    int maxFps = 120;

    SetTraceLogLevel(LOG_NONE);

    int WIDTH = 2400; // 2400, or 2322; // multiple of gridNumX, use for multigrid
    int HEIGHT = 1300; // 1300, or gridNumY * WIDTH / gridNumX; <-- use for when you implement multigrid

    InitWindow(WIDTH, HEIGHT, "Flip Fluid");

    FluidState *fluid_attributes = new FluidState(numParticles, gridNumX, vorticityStrength, flipRatio, apicRatio, gravityX, gravityY, WIDTH, HEIGHT);

    SceneHandler *scene_handler = new SceneHandler(*fluid_attributes, maxFps);

    g_scene_handler = scene_handler;

    #ifdef __EMSCRIPTEN__
        emscripten_request_animation_frame_loop(emscripten_loop, nullptr);
    #else
        while(!WindowShouldClose()) {
            scene_handler->simulate(0, nullptr);
            break;
        }
    #endif

    return 0;
}