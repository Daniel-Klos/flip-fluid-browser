#pragma once
#include <raylib.h>
#include <iostream>
#include <cmath>
#include <vector>
#include <algorithm>
#include <random>
#include <array>

#include "collision_grid.hpp"
#include "pressure_solver.hpp"
#include "density_projection.hpp"
#include "transfer_grid.hpp"
#include "fluid_rendering.hpp"
#include "scene_renderer.hpp"
#include "utils.hpp"

class FluidHandler {
    float moveDist;
    float checkSeparationDist;

public:

    float scalingFactor;
    int32_t scaledWIDTH;
    int32_t scaledHEIGHT;

    CollisionGrid collisionGrid;

    FluidState &fas;

    float objectSimRadius = 250;
    float objectRenderRadius = objectSimRadius;

    float forceObjectStrength = 250.f;

    FluidRenderer &fluid_renderer;
    PressureSolver pressure_solver;
    IDPSolver density_solver;
    TransferGrid transfer_grid;

    std::vector<int> collisions;

    Color objectColor = {255, 255, 255, 255};

    float norm_g = 5500.f / 9.8f;

    FluidHandler(FluidState& fluid_attributes, FluidRenderer &fr): fas(fluid_attributes),
                                                      fluid_renderer(fr),
                                                      pressure_solver(fluid_attributes),
                                                      density_solver(fluid_attributes),
                                                      transfer_grid(fluid_attributes)
    {
        
        this->moveDist = 2 * fas.radius;
        this->checkSeparationDist = moveDist * moveDist;

        this->scalingFactor = 2 * fas.radius;

        this->scaledWIDTH = std::ceil(static_cast<float>(fas.frame_context.WIDTH) / scalingFactor);
        this->scaledHEIGHT = std::ceil(static_cast<float>(fas.frame_context.HEIGHT) / scalingFactor);

        collisionGrid = CollisionGrid(scaledWIDTH, scaledHEIGHT);

        objectRenderRadius = objectSimRadius / fas.frame_context.zoom_amount;

        collisions.resize(fas.num_particles);
    }

    void ApplyBodyForces() {
        if (fas.activeTool == Tool::DragObject) {                                                          // dragging
            includeDragObject();
        } if (fas.activeTool == Tool::RepelObject && fas.frame_context.mouseDown) {  // force pull  attract and repel objects are switched for some reason but who
            includeForceObject(forceObjectStrength * 4);                             // cares this works
        } if (fas.activeTool == Tool::AttractObject && fas.frame_context.mouseDown) { // force push
            includeForceObject(-forceObjectStrength); // pushing, 1000
        }
        if (fluid_renderer.getRenderMode() == 3) {                              // ground->particle thermal conducting. I just lump it in here but it's not an actual force
            heatGround();                                                          // note that particle->particle thermal conducting is handled in solveCollisions()
        }
        if (fas.vorticityStrength != 0) {           // vorticity confinement
            applyVorticityConfinementRedBlack();
        }
    }

    void UpdateEnvironment() {
        Advect();

        SolveCollisions();
        fas.CollideSurfaces();
        MarkAirAndFluidCells();
        // If there's ever a bug where the fluid looks like its popping/unstable, its probably because rest density is not set correctly here
        density_solver.ComputeGridDensityOnly();

        if (fluid_renderer.getRenderMode() == 3 && fas.fireActive) {     // applying buoyant force to high temperature partiles
            makeFire();
        }

        //MarkAirAndFluidCells();
        //density_solver.ProjectDensity();
        //MarkAirAndFluidCells();

        transfer_grid.TransferToGrid();

        ApplyBodyForces();
        
        pressure_solver.SolvePressure();

        transfer_grid.TransferToParticles();
    }

    void Advect() {
        for (int i = 0; i < fas.num_particles; ++i) {
            fas.positions[2 * i] += fas.velocities[2 * i] * fas.frame_context.dt;
            fas.positions[2 * i + 1] += fas.velocities[2 * i + 1] * fas.frame_context.dt;
            fas.velocities[2 * i] += fas.gravityX * norm_g * fas.frame_context.dt;
            fas.velocities[2 * i + 1] += fas.gravityY * norm_g * fas.frame_context.dt;

            fas.particleAges[i] = std::min(fas.age_constant, fas.particleAges[i] + 1);
        }
    }

    void MarkAirAndFluidCells() {
        for (int i = 0; i < fas.numX; ++i) {
            for (int j = 0; j < fas.numY; ++j) {
                int idx = i * fas.numY + j;
                if (fas.cellType[idx] != fas.SOLID) {
                    fas.cellType[idx] = fas.AIR;
                }
            }
        }

        fas.num_fluid_cells = 0;
        for (int i = 0; i < fas.num_particles; ++i) {
            float x = fas.positions[2 * i];
            float y = fas.positions[2 * i + 1];

            int xi = clamp(std::floor(x * fas.invSpacing), 0, fas.numX - 1);
            int yi = clamp(std::floor(y * fas.invSpacing), 0, fas.numY - 1);

            int cellNr = xi * fas.numY + yi;
            if (fas.cellType[cellNr] == fas.AIR) {
                fas.cellType[cellNr] = fas.FLUID;
                fas.fluid_cells[fas.num_fluid_cells] = cellNr;
                fas.num_fluid_cells++;
            }
        }
    }

    void heatGround() {
        const float percentRemoved = 0.1f;
        const float leftEdge = fas.frame_context.WIDTH * percentRemoved;
        const float rightEdge = fas.frame_context.WIDTH * (1.0f - percentRemoved);
        for (int i = 0; i < fas.num_particles; ++i) {
            if (fas.positions[2 * i + 1] + fas.radius > fas.frame_context.HEIGHT - 2 * fas.cellSpacing) {
                if ((fas.temperatures[i] < fluid_renderer.tempGradient.size()) || (fas.positions[2 * i] > leftEdge && fas.positions[2 * i] < rightEdge)) {
                    fas.temperatures[i] += fas.groundConductivity * fas.frame_context.dt;
                }
            }
        }
    }

    // loop over all particles
        // calculate an aggregate temperature for each cell (for the cell center) based on lerped particle temperatures
    // loop over all particles again
        // for each particle, lerp a value for surround_temp from surrounding cells
            // remember to offset:
                // int x = pos.x;
                // int y = pos.y;

                // float gx = x / fas.cellSpacing - 0.5f;
                // float gy = y / fas.cellSpacing - 0.5f;
            // temperatures[i] += surround_temp * fas.interConductivity * dt
    void DiffuseHeat() {
        for (int i = 1; i < fas.numX - 1; ++i) {
            for (int j = 1; j < fas.numY - 1; ++j) {
                int idx = i * fas.n + j;
                if (fas.cellType[idx] != fas.FLUID) continue;

                fas.cellTemperatures[idx];

                /*int pIdx = cell.objects[id];

                const float transfer = (fas.temperatures[idx] - fas.temperatures[otherIndex]) * fas.interConductivity * fas.frame_context.dt;
                
                fas.temperatures[index] -= transfer * fas.frame_context.dt;
                fas.temperatures[otherIndex] += transfer * fas.frame_context.dt;*/
            }
        }
    }

    void makeFire() {
        for (int i = 0; i < fas.num_particles; ++i) {
            if (fas.positions[2 * i + 1] < fas.frame_context.HEIGHT - fas.cellSpacing - 10) {
                fas.velocities[2 * i + 1] -= fas.fireStrength * fas.temperatures[i] * fas.frame_context.dt;
                if (fas.temperatures[i] > 0) {
                    fas.temperatures[i] -= fas.tempDiffusion * fas.frame_context.dt;
                }
                if (collisions[i] == 0) {
                    fas.temperatures[i] = 0;
                }
            }
        }
    }

    void FillCollisionGrid()
    {

        collisionGrid.clear();

        const float minX = fas.cellSpacing;
        const float maxX = fas.frame_context.WIDTH - fas.cellSpacing;
        const float minY = fas.cellSpacing;
        const float maxY = fas.frame_context.HEIGHT - fas.cellSpacing;

        uint32_t i = 0;

        for (int32_t index = 0; index < fas.num_particles; ++index) {
            float x = fas.positions[2 * index];
            float y = fas.positions[2 * index + 1];
            if (x > minX && x < maxX && y > minY && y < maxY) {
                int32_t gridX = x / scalingFactor;
                int32_t gridY = y / scalingFactor;
                collisionGrid.addAtom(gridX, gridY, i);
            }
            ++i;
        }
    }

    void solveContact(uint32_t index, uint32_t otherIndex)
    {
        constexpr float eps = 0.0001f;
        const float o2_o1X = fas.positions[2 * index] - fas.positions[2 * otherIndex];
        const float o2_o1Y = fas.positions[2 * index + 1] - fas.positions[2 * otherIndex + 1];

        const float dist2 = o2_o1X * o2_o1X + o2_o1Y * o2_o1Y;

        if (dist2 < checkSeparationDist && dist2 > eps) {
            const float dist          = sqrt(dist2);
            const float delta = 0.5f * (moveDist - dist) / dist;
            const float col_vecX = o2_o1X * delta;
            const float col_vecY = o2_o1Y * delta;

            fas.positions[2 * index] += col_vecX;
            fas.positions[2 * index + 1] += col_vecY;

            fas.positions[2 * otherIndex] -= col_vecX;
            fas.positions[2 * otherIndex + 1] -= col_vecY;

            const float transfer = (fas.temperatures[index] - fas.temperatures[otherIndex]) * fas.interConductivity * fas.frame_context.dt;

            fas.temperatures[index] -= transfer * fas.frame_context.dt;
            fas.temperatures[otherIndex] += transfer * fas.frame_context.dt;

            collisions[index]++;
            collisions[otherIndex]++;
        }
    }

    void checkCellCollisions(uint32_t atom_idx, const CollisionCell& c)
    {
        for (uint32_t i = 0; i < c.objects_count; ++i) {
            solveContact(atom_idx, c.objects[i]);
        }
    }

    void processCell(const CollisionCell& c, uint32_t index)
    {
        for (uint32_t i = 0; i < c.objects_count; ++i) {
            const uint32_t idx = c.objects[i];
            for (int32_t side = 0; side < 2; ++side) {
                checkCellCollisions(idx, collisionGrid.data[index + collisionGrid.height + side]);
            }
            for (int32_t side = 0; side < 2; ++side) {
                checkCellCollisions(idx, collisionGrid.data[index + side]);   
            }
            //checkCellCollisions(idx, grid.data[index - grid.height]);
            checkCellCollisions(idx, collisionGrid.data[index - collisionGrid.height + 1]);
        }
    }

    void SolveCollisions()
    {
        if (fas.fireActive) {
            std::fill(begin(collisions), end(collisions), 0);
        }
        FillCollisionGrid();

        for (int32_t idx = 0; idx < collisionGrid.width * collisionGrid.height; ++idx) {
            processCell(collisionGrid.data[idx], idx);
        }
    }

    void calcVorticityConfinement(bool red, int32_t startColumn, int32_t endColumn) {
        for (int32_t i = startColumn; i < endColumn; ++i) {
            for (int32_t j = 1; j < fas.numY - 1; ++j) {
                if (red) {
                    if ((i + j) % 2 != 0) continue; 
                }
                else {
                    if ((i + j) % 2 == 0) continue; 
                }
                float dx = abs(fas.curl(i, j + 1)) - abs(fas.curl(i, j - 1));
                float dy = abs(fas.curl(i + 1, j)) - abs(fas.curl(i - 1, j));

                const float len = std::sqrt(dx * dx + dy * dy);
                const float invLen = 1.f / (len + (len == 0.f)) - (len == 0.f);

                dx *= invLen;
                dy *= invLen;

                const float c = fas.curl(i, j);

                fas.v[i * fas.n + j] += c * dx * fas.frame_context.dt * fas.vorticityStrength;
                fas.u[i * fas.n + j] += c * dy * fas.frame_context.dt * fas.vorticityStrength;
            }
        }
    }

    void applyVorticityConfinementRedBlack() {
        this->calcVorticityConfinement(true, 1, fas.numX - 1);
        this->calcVorticityConfinement(false, 1, fas.numX - 1);
    }

    void includeForceObject(const int32_t strength) {
        const float extend = (20 * fas.cellSpacing) / fas.frame_context.zoom_amount;

        int forceObjectX = fas.frame_context.world_mouse_pos.x;
        int forceObjectY = fas.frame_context.world_mouse_pos.y;

        int objectCellX = forceObjectX / fas.cellSpacing;
        int objectCellY = forceObjectY / fas.cellSpacing;
        int objectCellRadius = std::ceil(objectSimRadius / fas.cellSpacing);

        for (int i = objectCellX - objectCellRadius; i < objectCellX + objectCellRadius; i++) {
            for (int j = objectCellY - objectCellRadius; j < objectCellY + objectCellRadius; j++) {
                int cellNr = i * fas.n + j;
                bool notInBounds = i < 0 || i >= fas.numX || j < 0 || j >= fas.numY;
                if (notInBounds || (fas.cellType[i * fas.n + j] == fas.SOLID)) continue;
                float dx = (i + 0.5) * fas.cellSpacing - forceObjectX;
                float dy = (j + 0.5) * fas.cellSpacing - forceObjectY;
                float d2 = dx * dx + dy * dy;

                if (d2 > objectSimRadius * objectSimRadius + extend || d2 == 0.0f) continue;

                float d = std::sqrt(d2);

                float edgeT = d / objectSimRadius;
            
                float centerT = 1 - edgeT;
                    
                if (fas.cellType[cellNr - fas.n] != fas.SOLID) {
                    fas.u[cellNr] += (dx * strength - fas.u[cellNr]) * centerT * fas.frame_context.dt;
                }
                if (fas.cellType[cellNr + fas.n] != fas.SOLID) {
                    fas.u[cellNr + fas.n] += (dx * strength - fas.u[cellNr + fas.n]) * centerT * fas.frame_context.dt;
                }
                if (fas.cellType[cellNr - 1] != fas.SOLID) {
                    fas.v[cellNr] += (dy * strength - fas.v[cellNr]) * centerT * fas.frame_context.dt;
                }
                if (fas.cellType[cellNr + 1] != fas.SOLID) {
                    fas.v[cellNr + 1] += (dy * strength - fas.v[cellNr + 1]) * centerT * fas.frame_context.dt;
                }
            }
        }
    }

    void includeDragObject() {
        // Bug when you hold force object attract then switch to drag object while holding mouse down
        const float extend = (20 * fas.cellSpacing) / fas.frame_context.zoom_amount;
        const float invDt = 1.f / fas.frame_context.dt;
        if (fas.frame_context.mouseDown) {
            float objectX = fas.frame_context.world_mouse_pos.x;
            float objectY = fas.frame_context.world_mouse_pos.y;
            float objectPrevX = fas.frame_context.prev_world_mouse_pos.x;
            float objectPrevY = fas.frame_context.prev_world_mouse_pos.y;
            float vx = (objectX - objectPrevX) * invDt * !fas.frame_context.justPressed;
            float vy = (objectY - objectPrevY) * invDt * !fas.frame_context.justPressed;
            int objectCellX = objectX / fas.cellSpacing;
            int objectCellY = objectY / fas.cellSpacing;
            int objectCellRadius = std::ceil(objectSimRadius / fas.cellSpacing);
            for (int i = objectCellX - objectCellRadius; i < objectCellX + objectCellRadius; i++) {
                for (int j = objectCellY - objectCellRadius; j < objectCellY + objectCellRadius; j++) {
                    int cellNr = i * fas.n + j;
                    bool notInBounds = i < 0 || i >= fas.numX || j < 0 || j >= fas.numY;
                    if (notInBounds || (fas.cellType[i * fas.n + j] == fas.SOLID)) continue;
                    float dx = (i + 0.5) * fas.cellSpacing - objectX;
                    float dy = (j + 0.5) * fas.cellSpacing - objectY;

                    if (dx * dx + dy * dy < objectSimRadius * objectSimRadius + extend) {
                        if (fas.cellType[cellNr - fas.n] != fas.SOLID) {
                            fas.u[cellNr] = vx;
                        }
                        if (fas.cellType[cellNr + fas.n] != fas.SOLID) {
                            fas.u[cellNr + fas.n] = vx;
                        }
                        if (fas.cellType[cellNr - 1] != fas.SOLID) {
                            fas.v[cellNr] = vy;
                        }
                        if (fas.cellType[cellNr + 1] != fas.SOLID) {
                            fas.v[cellNr + 1] = vy;
                        }
                    }
                }
            }
        }
    }

    void generate() {
        float separation = fas.radius * 2.f;
        int wideNum = std::floor((2 * objectSimRadius) / (separation));
        int highNum = wideNum;
        int numPotentiallyAdded = wideNum * highNum;

        float generatorRightBound = fas.frame_context.world_mouse_pos.x + objectSimRadius;
        float generatorBottomBound = fas.frame_context.world_mouse_pos.y + objectSimRadius;

        float starting_px = std::max(fas.frame_context.world_mouse_pos.x - objectSimRadius + fas.radius, fas.cellSpacing + fas.radius);
        float starting_py = std::max(fas.frame_context.world_mouse_pos.y - objectSimRadius + fas.radius, fas.cellSpacing + fas.radius);
        float px = starting_px;
        float py = starting_py;
        bool offset = true;

        int originalParticleCount = fas.num_particles;
        int maxPossibleParticles = originalParticleCount + numPotentiallyAdded;

        fas.positions.resize(2 * maxPossibleParticles);
        fas.renderPositions.resize(2 * maxPossibleParticles);
        int addedTo = 0;

        for (int i = 0; i < numPotentiallyAdded; ++i) {
            float prevPx = px;
            float prevPy = py;
            if (prevPy > generatorBottomBound || prevPy + fas.radius > fas.frame_context.HEIGHT - fas.cellSpacing) {
                break;
            }

            int cellX = px / fas.cellSpacing;
            int cellY = py / fas.cellSpacing;
            int cellNr = cellX * fas.n + cellY;

            px += separation;
            if (px > generatorRightBound) {
                px = starting_px;
                /*if (offset) {
                    px += 0.5 * separation;
                }*/
                py += separation;
                offset = !offset;
            }

            if (cellX > fas.numX || (fas.cellType[cellNr] != fas.AIR || prevPx - fas.radius < fas.cellSpacing || prevPx + fas.radius > fas.frame_context.WIDTH - fas.cellSpacing || prevPy - fas.radius < fas.cellSpacing)) {
                continue;
            }


            fas.positions[2 * (fas.num_particles + addedTo)] = prevPx;
            fas.positions[2 * (fas.num_particles + addedTo) + 1] = prevPy;
            
            fas.renderPositions[2 * (fas.num_particles + addedTo)] = prevPx;
            fas.renderPositions[2 * (fas.num_particles + addedTo) + 1] = prevPy;

            addedTo++;
        }

        fas.num_particles += addedTo;

        fas.positions.resize(2 * fas.num_particles);
        fas.renderPositions.resize(2 * fas.num_particles);
        fas.velocities.resize(2 * fas.num_particles);
        fluid_renderer.particleDiffusionColors.resize(3 * fas.num_particles);

        fas.particleAges.resize(fas.num_particles);
        //fas.debug.resize(fas.num_particles);

        int start = fas.num_particles - addedTo;

        for (int i = start; i < fas.num_particles; ++i) {
            int idx1 = 2 * i;
            int idx2 = 3 * i;

            fas.velocities[idx1] = 0.f;
            fas.velocities[idx1 + 1] = 0.f;

            fluid_renderer.particleDiffusionColors[idx2] = 0;
            fluid_renderer.particleDiffusionColors[idx2 + 1] = 0;
            fluid_renderer.particleDiffusionColors[idx2 + 2] = 255;

            fas.particleAges[i] = 0;
        }

        // make Resize(num_particles) methods within each struct. this is too messy

        this->collisions.resize(fas.num_particles);
        fas.temperatures.resize(fas.num_particles);

        transfer_grid.Resize(fas.num_particles);

        fluid_renderer.ResizeAndUpdateMesh(fas.num_particles);
    }

    void remove() {
        float spacing = fas.cellSpacing;
        const int32_t numCovered = std::ceil(objectSimRadius / spacing);
        const int32_t mouseColumn = std::floor(fas.frame_context.world_mouse_pos.x / spacing);
        const int32_t mouseRow = std::floor(fas.frame_context.world_mouse_pos.y / spacing);
        const size_t len = fas.num_particles;
        const size_t double_len = 2 * len;
        const size_t triple_len = 3 * len;

        size_t remove = 0;
        
        for (int32_t i = -numCovered; i < numCovered + 1; ++i) {
            for (int32_t j = -numCovered; j < numCovered + 1; ++j) {
                if (mouseRow + j < 1 || mouseRow + j > fas.numY - 1 || mouseColumn + i < 1 || mouseColumn + i > fas.numX - 1)
                    continue;
    
                const auto &cell = fas.cellOccupants.data[mouseRow + j + fas.cellOccupants.height * (mouseColumn + i)];
    
                for (uint32_t id{0}; id < cell.objects_count; ++id) {
                    const uint32_t particleIndex = cell.objects[id];

                    size_t doubleid = 2 * particleIndex;
                    size_t tripleid = 3 * particleIndex;

                    size_t doubleremove = 2 * remove;
                    size_t tripleremove = 3 * remove;
    
                    if (doubleid + 2 < double_len) {
                        /*fas.particle_densities[particleIndex] = fas.particle_densities[particleIndex - 1 - remove];
                        fas.densities[particleIndex] = fas.particle_densities[particleIndex - 1 - remove];*/

                        fas.temperatures[particleIndex] = fas.temperatures[len - 1 - remove];

                        fluid_renderer.replaceParticleWithEnd(particleIndex, len - 1 - remove);

                        fas.positions[doubleid] = fas.positions[double_len - 2 - doubleremove];
                        fas.positions[doubleid + 1] = fas.positions[double_len - 1 - doubleremove];

                        fas.renderPositions[doubleid] = fas.renderPositions[double_len - 2 - doubleremove];
                        fas.renderPositions[doubleid + 1] = fas.renderPositions[double_len - 1 - doubleremove];

                        fas.velocities[doubleid] = fas.velocities[double_len - 2 - doubleremove];
                        fas.velocities[doubleid + 1] = fas.velocities[double_len - 1 - doubleremove];

                        fluid_renderer.particleDiffusionColors[tripleid] = fluid_renderer.particleDiffusionColors[triple_len - 3 - tripleremove];
                        fluid_renderer.particleDiffusionColors[tripleid + 1] = fluid_renderer.particleDiffusionColors[triple_len - 2 - tripleremove];
                        fluid_renderer.particleDiffusionColors[tripleid + 2] = fluid_renderer.particleDiffusionColors[triple_len - 1 - tripleremove];
                    }
                    remove++;
                }
            }
        }
        fas.num_particles -= remove;
    
        fas.positions.resize(2 * fas.num_particles);
        fas.renderPositions.resize(2 * fas.num_particles);
        fas.velocities.resize(2 * fas.num_particles);
        //fas.debug.resize(fas.num_particles);
        this->collisions.resize(fas.num_particles);
    
        fas.temperatures.resize(fas.num_particles);

        transfer_grid.Resize(fas.num_particles);

        fas.particleAges.resize(fas.num_particles);

        fluid_renderer.ResizeAndUpdateMesh(fas.num_particles);
    }

    void drawCircleObject() {
        DrawCircleLinesV(fas.frame_context.screen_mouse_pos, objectRenderRadius, objectColor);
    }

    void drawSquareObject() {
        DrawRectangleLines(fas.frame_context.screen_mouse_pos.x - objectRenderRadius, fas.frame_context.screen_mouse_pos.y - objectRenderRadius, 2 * objectRenderRadius, 2 * objectRenderRadius, objectColor);
    }

    void render_objects() {
        if ((fas.activeTool == Tool::AttractObject) ||  (fas.activeTool == Tool::RepelObject) ||  (fas.activeTool == Tool::DragObject)) {
            drawCircleObject();
        }
        else if (fas.activeTool == Tool::SpawnObject) {
            drawSquareObject();
        }
    }

    void addToObjectRadius(float add) {
        if (objectRenderRadius + add > 0) {
            objectRenderRadius += add;

            objectSimRadius = objectRenderRadius / fas.frame_context.zoom_amount;
        }
    }

    void setObjectRadius(float set) {
        if (set > 0) {
            objectRenderRadius = set;

            objectSimRadius = objectRenderRadius / fas.frame_context.zoom_amount;
        }
    }

    void setForceObjectStrength(float set) {
        forceObjectStrength = set;
    }

};