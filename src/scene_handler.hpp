#pragma once
#include <sstream>
#include <iomanip>
#include <unordered_map>
#include "simulation_state.hpp"
#include "fluid_handler.hpp"
#include "obstacle_handler.hpp"
#include "scene_renderer.hpp"

#include <rlgl.h>

struct SceneHandler {
    FluidState &fluid_attributes;

    SceneRenderer scene_renderer;
    ObstacleHandler obstacle_handler;
    FluidHandler fluid_handler;

    int frame = 0;
    int fps;

    std::string showControlsStr = "Show Controls";
    std::string hideControlsStr = "Hide Controls";

    int showControlsX;
    int showControlsWIDTH;
    int showControlsY;
    int showControlsHEIGHT;

    int hideControlsX;  
    int hideControlsWIDTH;
    int hideControlsY;
    int hideControlsHEIGHT;

    bool showControls = false;

    float fontSize;

    SceneHandler(FluidState &fas, int maxFps)
        : fluid_attributes(fas),
          scene_renderer(fas), 
          obstacle_handler(fas, scene_renderer.obstacle_renderer), 
          fluid_handler(fas, scene_renderer.fluid_renderer)
    {
        fluid_attributes.frame_context.maxFps = maxFps;
        fluid_attributes.frame_context.setDT = 1.f / maxFps;
        //SetTargetFPS(maxFps);

        // initialize obstacle positions
        // ----------------------------------------------------------------------------------------------------------------------------
        
        size_t numInitObstacles = 2 * fluid_attributes.numX + 2 * (fluid_attributes.numY - 2);
        fluid_attributes.obstaclePositions.resize(numInitObstacles);
        int idx = 0;
        for (int i = 0; i < fluid_attributes.numX; ++i) {
            fluid_attributes.cellType[i * fluid_attributes.n] = fluid_attributes.SOLID;
            fluid_attributes.obstaclePositions[idx] = Vector2i{i, 0};
            ++idx;
        }
        for (int i = 0; i < fluid_attributes.numX; ++i) {
            fluid_attributes.cellType[i * fluid_attributes.n + fluid_attributes.numY - 1] = fluid_attributes.SOLID;
            fluid_attributes.obstaclePositions[idx] = Vector2i{i, fluid_attributes.numY - 1};
            ++idx;
        }
        for (int j = 1; j < fluid_attributes.numY - 1; ++j) {
            fluid_attributes.cellType[j] = fluid_attributes.SOLID;
            fluid_attributes.obstaclePositions[idx] = Vector2i{0, j};
            ++idx;
        }
        for (int j = 1; j < fluid_attributes.numY - 1; ++j) {
            fluid_attributes.cellType[fluid_attributes.numX * fluid_attributes.numY + j - fluid_attributes.n] = fluid_attributes.SOLID;
            fluid_attributes.obstaclePositions[idx] = Vector2i{fluid_attributes.numX - 1, j};
            ++idx;
        }

        obstacle_handler.FastSweep();
        // ----------------------------------------------------------------------------------------------------------------------------


        
        // initialize obstacle_renderer
        // ----------------------------------------------------------------------------------------------------------------------------
        scene_renderer.obstacle_renderer.ResizeAndUploadNewObstacleMesh(numInitObstacles);
        // ----------------------------------------------------------------------------------------------------------------------------

        float WIDTH = fluid_attributes.frame_context.WIDTH;

        fontSize = GetFractionOf(WIDTH, 0.02f);

        showControlsWIDTH = MeasureText(showControlsStr.c_str(), fontSize);
        showControlsHEIGHT = fontSize;
        showControlsX = WIDTH - showControlsWIDTH - 2 * fluid_attributes.cellSpacing;
        showControlsY = showControlsHEIGHT;

        hideControlsWIDTH = MeasureText(hideControlsStr.c_str(), fontSize);
        hideControlsHEIGHT = fontSize;
        hideControlsX = WIDTH - hideControlsWIDTH - 2 * fluid_attributes.cellSpacing;
        hideControlsY = 14 * hideControlsHEIGHT;
    }

    bool simulate(double time, void* userData) { 
        fluid_attributes.frame_context.trueDT = GetFrameTime();

        Vector2 mouse_position = GetMousePosition();

        fluid_attributes.frame_context.screen_mouse_pos = Vector2{static_cast<float>(mouse_position.x), static_cast<float>(mouse_position.y)};
        fluid_attributes.frame_context.world_mouse_pos = scene_renderer.screenToWorld(fluid_attributes.frame_context.screen_mouse_pos);

        fluid_attributes.frame_context.dt = std::max(1e-3f, std::min(fluid_attributes.frame_context.trueDT, fluid_attributes.frame_context.setDT));

        track_mouse_events();

        HandleUserInteraction();
            
        if (!fluid_attributes.stop || fluid_attributes.step) {
            std::cout << "true" << std::endl;
            std::copy(begin(fluid_attributes.positions), end(fluid_attributes.positions), begin(fluid_attributes.renderPositions));
            fluid_handler.UpdateEnvironment();
            fluid_attributes.step = false;
        }

        handle_zoom();

        BeginDrawing();

        ClearBackground(BLACK);

        scene_renderer.render_scene();

        render_interaction_objects();

        if (frame == 0) {
            fps = std::min(120.f, std::abs(1 / fluid_attributes.frame_context.trueDT));
            frame = 20;
        }
        frame--;
        float WIDTH = fluid_attributes.frame_context.WIDTH;
        float HEIGHT = fluid_attributes.frame_context.HEIGHT;

        int fpsX = GetFractionOf(WIDTH, 0.0077f);
        int fpsY = GetFractionOf(HEIGHT, 0.0087f);

        std::string fps_str = std::to_string(fps);
        if (!fluid_attributes.getStop()) {
            DrawText(fps_str.c_str(), fpsX, fpsY, fontSize, WHITE);
        }
        else {
            DrawText(fps_str.c_str(), fpsX, fpsY, fontSize, RED);
        }

        EndDrawing();

        fluid_attributes.frame_context.prev_screen_mouse_pos = fluid_attributes.frame_context.screen_mouse_pos;
        fluid_attributes.frame_context.prev_world_mouse_pos = fluid_attributes.frame_context.world_mouse_pos;

        fluid_attributes.frame_context.zooming = false;
        
        return true;
    }

    void HandleUserInteraction() {
        if (fluid_attributes.activeTool == Tool::DrawObject && fluid_attributes.frame_context.mouseDown) {
            obstacle_handler.drawSolids();
        } else if (fluid_attributes.activeTool == Tool::EraseObject && fluid_attributes.frame_context.mouseDown) {
            obstacle_handler.eraseSolids();
        }

        if (fluid_attributes.activeTool == Tool::SpawnObject && fluid_attributes.frame_context.mouseDown) {
            fluid_handler.generate();
        } else if (fluid_attributes.activeTool == Tool::DeleteObject && fluid_attributes.frame_context.mouseDown) {
            fluid_attributes.FillCellOccupants();
            fluid_handler.remove();
        }
    }

    void handle_zoom() {
        if (fluid_attributes.frame_context.mouseDown && fluid_attributes.activeTool == Tool::ZoomObject) {
            scene_renderer.dragCamera();
        }
        if (fluid_attributes.frame_context.zooming) {
            float zoom = fluid_attributes.frame_context.zoom_amount;
            fluid_handler.objectSimRadius = fluid_handler.objectRenderRadius / zoom;
        }
    }

    void render_interaction_objects() {
        fluid_handler.render_objects();
        obstacle_handler.render_objects();
    }


    void track_mouse_events() {

        fluid_attributes.frame_context.mouseDown = IsMouseButtonDown(MOUSE_BUTTON_LEFT) || IsMouseButtonDown(MOUSE_BUTTON_RIGHT);
        fluid_attributes.frame_context.justPressed = IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || IsMouseButtonPressed(MOUSE_BUTTON_RIGHT);
        
        float mouseWheelDelta = GetMouseWheelMove();

        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
            if (!showControls && PointIsInsideRectangle(fluid_attributes.frame_context.screen_mouse_pos.x, fluid_attributes.frame_context.screen_mouse_pos.y, showControlsX, showControlsY, showControlsWIDTH, showControlsHEIGHT)) {
                showControls = true;
            }
            else if (showControls && PointIsInsideRectangle(fluid_attributes.frame_context.screen_mouse_pos.x, fluid_attributes.frame_context.screen_mouse_pos.y, hideControlsX, hideControlsY, hideControlsWIDTH, hideControlsHEIGHT)) {
                showControls = false;
            }
        }

        if (mouseWheelDelta) {
            if (fluid_attributes.activeTool == Tool::DragObject    ||
                fluid_attributes.activeTool == Tool::AttractObject ||
                fluid_attributes.activeTool == Tool::RepelObject   ||
                fluid_attributes.activeTool == Tool::SpawnObject   ||
                fluid_attributes.activeTool == Tool::DeleteObject) {
                fluid_handler.addToObjectRadius(20 * mouseWheelDelta);
            }
            
            else if (fluid_attributes.activeTool == Tool::DrawObject  ||
                     fluid_attributes.activeTool == Tool::EraseObject) {
                int pencilRadius = obstacle_handler.getPencilRadius();
                bool limit_size = (pencilRadius > fluid_attributes.getNumX() / 2 && mouseWheelDelta > 0);
                if (!limit_size) {
                    if (pencilRadius + mouseWheelDelta < 0) {
                        obstacle_handler.setPencilRadius(0);
                    }
                    else {
                        obstacle_handler.addToPencilRadius(mouseWheelDelta);
                    }
                }
            }

            else if (fluid_attributes.activeTool == Tool::ZoomObject) {
                scene_renderer.wheelZoom(mouseWheelDelta);
            }
        }
    }

    int DrawTextNearRightWall(std::string &text, float yPos) {
        int strWIDTH = MeasureText(text.c_str(), fontSize);
        int strHEIGHT = MeasureText(text.c_str(), fontSize);
        DrawText(text.c_str(), fluid_attributes.frame_context.WIDTH - strWIDTH - 2 * fluid_attributes.cellSpacing, yPos, fontSize, WHITE);
        return strHEIGHT;
    }

};