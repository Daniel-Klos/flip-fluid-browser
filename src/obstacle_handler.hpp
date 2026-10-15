#pragma once
#include "simulation_state.hpp"
#include "obstacle_rendering.hpp"
#include "utils.hpp"

struct ObstacleHandler {
    FluidState &fas;
    ObstacleRenderer &obstacle_renderer;

    int pencilRadius = 1;

    float rightWallPos;
    float floorPos;

    ObstacleHandler(FluidState &fluid_attributes, ObstacleRenderer &ore): fas(fluid_attributes), obstacle_renderer(ore) {
        fas.phi.resize(fas.gridSize);

        rightWallPos = (fas.numX - 1) * fas.cellSpacing;
        floorPos     = (fas.numY - 1) * fas.cellSpacing;
    }

    void SetUpPhi() {
        for (int i = 0; i < fas.gridSize; ++i) {
            if (fas.cellType[i] == fas.SOLID) {
                fas.phi[i] = 1e10f;
            }
            else {
                fas.phi[i] = 0.f;
            }
        }
    }

    void FastSweep() {
        SetUpPhi();

        const int NSweeps = 4;
        // sweep directions { start, end, step }
        const int dirX[NSweeps][3] = { {0, fas.numX - 1,  1}, {fas.numX - 1, 0, -1}, {fas.numX - 1, 0,  -1}, {0, fas.numX - 1,   1} };
        const int dirY[NSweeps][3] = { {0, fas.numY - 1, 1}, {0, fas.numY - 1, 1}, {fas.numY - 1, 0, -1}, {fas.numY - 1, 0, -1} };

        double aa[2];
        double d_new, a, b;
        int s, ix, iy, gridPos;
        const double h = fas.cellSpacing, f = 1.0;

        for (s = 0; s < NSweeps; s++) {
            for (ix = dirX[s][0]; dirX[s][2] * ix <= dirX[s][1]; ix += dirX[s][2]) {
                for (iy = dirY[s][0]; dirY[s][2] * iy <= dirY[s][1]; iy += dirY[s][2]) {
                    gridPos = ix * fas.numY + iy;

                    float center = fas.phi[gridPos];
                    float left   = fas.phi[gridPos - fas.numY];
                    float right  = fas.phi[gridPos + fas.numY];
                    float top    = fas.phi[gridPos - 1];
                    float bottom = fas.phi[gridPos + 1];

                    if (fas.cellType[gridPos] == fas.SOLID) {
                        if (iy == 0 || iy == (fas.numY - 1)) {
                            if (iy == 0) {
                                aa[1] = center < bottom ? center : bottom;
                            }
                                if (iy == (fas.numY - 1)) {
                                aa[1] = top < center ? top : center;
                            }
                        }
                        else {
                            aa[1] = top < bottom ? top : bottom;
                        }
                    
                        if (ix == 0 || ix == (fas.numX - 1)) {
                            if (ix == 0) {
                                aa[0] = center < right ? center : right;
                            }
                            if (ix == (fas.numX - 1)) {
                                aa[0] = left < center ? left : center;
                            }
                        }
                        else {
                            aa[0] = left < right ? left : right;
                        }
                    
                        a = aa[0]; b = aa[1];
                        d_new = (fabs(a - b) < f * h ? (a + b + sqrt(2.0 * f * f * h * h - (a - b) * (a - b))) * 0.5 : std::fminf(a, b) + f * h);
                    
                        fas.phi[gridPos] = center < d_new ? center : d_new;
                    }
                }
            }
        }
    }

    void drawSolids() {
        int localX = static_cast<int>(fas.frame_context.world_mouse_pos.x / fas.cellSpacing);
        int localY = static_cast<int>(fas.frame_context.world_mouse_pos.y / fas.cellSpacing);

        int numObstacles = fas.obstaclePositions.size();

        int numPotentialAddedObstacles = ((2 * pencilRadius + 1) * (2 * pencilRadius + 1));
        fas.obstaclePositions.resize(numObstacles + numPotentialAddedObstacles);

        int numAddedObstacles = 0;
        for (int i = -pencilRadius; i <= pencilRadius; ++i) {
            for (int j = -pencilRadius; j <= pencilRadius; ++j) {
                int x = localX + i;
                int y = localY + j;

                if (x > 0 && y > 0 && x < fas.numX - 1 && y < fas.numY - 1) {

                    int idx = x * fas.numY + y;

                    if (fas.cellType[idx] != fas.SOLID) {
                        fas.cellType[idx] = fas.SOLID;
                        fas.obstaclePositions[numObstacles + numAddedObstacles] = Vector2i{x, y};
                        numAddedObstacles++;
                    }
                }
            }
        }

        fas.obstaclePositions.resize(numObstacles + numAddedObstacles);

        if (numAddedObstacles > 0) {
            obstacle_renderer.ResizeAndUploadNewObstacleMesh(numObstacles + numAddedObstacles);
        }  

        FastSweep();
    }

    void eraseSolids() {
        int localX = static_cast<int>(fas.frame_context.world_mouse_pos.x / fas.cellSpacing);
        int localY = static_cast<int>(fas.frame_context.world_mouse_pos.y / fas.cellSpacing);

        int numObstacles = fas.obstaclePositions.size();
        int numFreedCells = 0;
        for (int i = -pencilRadius; i <= pencilRadius; ++i) {
            for (int j = -pencilRadius; j <= pencilRadius; ++j) {
                int x = localX + i;
                int y = localY + j;

                int idx = x * fas.n + y;
                if (fas.cellType[idx] == fas.SOLID && x > 0 && y > 0 && x < fas.numX - 1 && y < fas.numY - 1) {
                    fas.cellType[idx] = fas.AIR;
                    // find the index of obstacleSet with the position we want to remove
                    // replace that index with the [numObstacles - (numFreedCells + 1)] index of obstacleSet
                    // then just resize the obstacleSet and obstacleVa after this double loop
                    numFreedCells++;

                    int obstacleIdx = find(fas.obstaclePositions, Vector2i{x, y});
                    int endIdx = numObstacles - numFreedCells;

                    fas.obstaclePositions[obstacleIdx] = fas.obstaclePositions[endIdx];

                    obstacle_renderer.replaceObstacleWithEnd(obstacleIdx, endIdx);
                }
            }
        }
        obstacle_renderer.ResizeAndUploadNewObstacleMesh(numObstacles - numFreedCells);
        fas.obstaclePositions.resize(numObstacles - numFreedCells);

        FastSweep();
    }

    void render_objects() {
        if (fas.activeTool == Tool::DrawObject || fas.activeTool == Tool::EraseObject) {
            obstacle_renderer.drawPencil();
        }
    }

    int getPencilRadius() {
        return this->pencilRadius;
    }

    void addToPencilRadius(int add) {
        this->pencilRadius += add;
        obstacle_renderer.changePencilRadius(pencilRadius);
    }

    void setPencilRadius(int set) {
        this->pencilRadius = set;
        obstacle_renderer.changePencilRadius(pencilRadius);
    }

};