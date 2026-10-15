#pragma once
#include <functional>
#include "simulation_state.hpp"

struct PressureSolver {
    FluidState &fas;
    int n;
    int gridSize;
    int numPressureIters = 40;

    float k = 10.f;
    float overRelaxation = 1.9f;

    /*std::vector<double> residual;
    std::vector<double> Adiag;
    std::vector<double> precon;
    std::vector<double> direction;
    std::vector<double> dotProducts;
    std::vector<double> pressure;

    std::vector<uint8_t> neighbors;
    static constexpr uint8_t LEFT = 64;
    static constexpr uint8_t RIGHT = 32;
    static constexpr uint8_t BOTTOM = 16;
    static constexpr uint8_t TOP = 8;
    static constexpr uint8_t CENTER = 7;
    static constexpr std::array<uint8_t, 4> directions{LEFT, RIGHT, BOTTOM, TOP};*/

    // structures to hold all coarse levels (finest levels are already stored in fas)
    std::vector<std::vector<float>> velocityLevels;
    std::vector<std::vector<uint8_t>> cellTypeLevels;
    int max_multigrid_coarseness = 4;

    PressureSolver(FluidState &fluid_attributes): fas(fluid_attributes) {
        n = fas.numY;

        gridSize = fas.gridSize;

        //ResizeLevelArrays();

        /*this->Adiag.resize(gridSize);
        this->neighbors.resize(gridSize);
        this->precon.resize(gridSize);
        this->direction.resize(gridSize);
        this->residual.resize(gridSize);
        this->pressure.resize(gridSize);*/
    }


    // What I'm using currently
    // --------------------------------------------------------------------------------------------------------------------------------------------

    void passRedBlackSOR(bool red) {
        for (int i = 1; i < fas.numX - 1; ++i) {
            for (int j = (i + red) % 2; j < fas.numY - 1; j += 2) {
                int idx = i * n + j;
                if (fas.cellType[idx] != fas.FLUID) continue;

                float leftType = fas.cellType[idx - n] <= fas.AIR ? 1 : 0;
                float rightType = fas.cellType[idx + n] <= fas.AIR ? 1 : 0;
                float topType = fas.cellType[idx - 1] <= fas.AIR ? 1 : 0;
                float bottomType = fas.cellType[idx + 1] <= fas.AIR ? 1 : 0;

                float divideBy = leftType + rightType + topType + bottomType;
            
                if (divideBy == 0.f) continue;

                float divergence = fas.u[idx + n] - fas.u[idx] + fas.v[idx + 1] - fas.v[idx];
                if (fas.particleRestDensity > 0.f) {
                    float compression = fas.cellDensities[idx] - fas.particleRestDensity;
                    if (compression > 0.f) {
                        divergence -= k * compression;
                   }
                }

                float p = divergence / divideBy;
                p *= overRelaxation;

                fas.u[idx]     += leftType * p;
                fas.u[idx + n] -= rightType * p;
                fas.v[idx]     += topType * p;
                fas.v[idx + 1] -= bottomType * p;
            }
        }
    }

    void SolvePressure() {
        for (int iter = 0; iter < numPressureIters; ++iter) {
            passRedBlackSOR(0);
            passRedBlackSOR(1);
        }
    }
    // --------------------------------------------------------------------------------------------------------------------------------------------


    // Soon to be Multigrid code
    // --------------------------------------------------------------------------------------------------------------------------------------------

    // constructor
    /*void ResizeLevelArrays() {
        int minRes = std::min(fas.numX, fas.numY);
        int numLevels = sqrt(minRes) - sqrt(max_multigrid_coarseness);
    
        velocityLevels.resize(numLevels);
        cellTypeLevels.resize(numLevels);

        int levelFinenessX = fas.numX;
        int levelFinenessY = fas.numY;
        for (int i = 0; i < numLevels; ++i) {
            levelFinenessX /= 2;
            levelFinenessY /= 2;
            int coarseGridSize = levelFinenessX * levelFinenessY;

            velocityLevels[i].resize(coarseGridSize, 0.f);
            cellTypeLevels[i].resize(coarseGridSize, 0.f);
        }
    }

    void ReconstructCellTypeLevels() {
        int minRes = std::min(fas.numX, fas.numY);
        int numLevels = sqrt(minRes) - sqrt(max_multigrid_coarseness);
    
        // subtract 2 to eliminate explicit solid boundary
        int coarseNumX = fas.numX - 2;
        int coarseNumY = fas.numY - 2;
        for (int i = 0; i < numLevels; ++i) {
            int fineNumX = coarseNumX + 2 * (i == 0);
            int fineNumY = coarseNumY + 2 * (i == 0);
            coarseNumX /= 2;
            coarseNumY /= 2;

            // coarse grids don't have an explicit layer of boundary cells
            for (int fi = 0; fi < coarseNumX; ++fi) {
                for (int fj = 0; fj < coarseNumY; ++fj) {
                    int coarseIdx = fi * coarseNumX + fj;

                    bool finestCoarse = i == 0;

                    std::vector<uint8_t> &fineGrid = i > 0 ? cellTypeLevels[i - 1] : fas.cellType;

                    int dirichlet = 0, interior = 0;
                    
                    // Add one to fineX/Y if the previous grid is the original (original grid has explicit layer of boundary cells)
                    int fineX = 2 * fi + finestCoarse;
                    int fineY = 2 * fi + finestCoarse;
                    for (int childX = fineX; childX < fineX + 2; ++childX) {
                        for (int childY = fineY; childY < fineY + 2; ++childY) {
                            int fineIdx = childX * fineNumY + childY;
                            int cellType = fineGrid[fineIdx];
                            if (cellType == fas.AIR) {
                                dirichlet++;
                            }
                            else if (cellType == fas.FLUID) {
                                interior++;
                            }
                        }
                    }

                    if (dirichlet > 0) {
                        cellTypeLevels[i][coarseIdx] = fas.AIR;
                    }
                    else if (dirichlet == 0 && interior > 0) {
                        cellTypeLevels[i][coarseIdx] = fas.FLUID;
                    }
                    else {
                        cellTypeLevels[i][coarseIdx] = fas.SOLID;
                    }
                }
            }
        }
    }

    void restrict() {
        int minRes = std::min(fas.numX, fas.numY);
        int numLevels = sqrt(minRes) - sqrt(max_multigrid_coarseness);
    
        velocityLevels.resize(numLevels);
        cellTypeLevels.resize(numLevels);

        int levelFinenessX = fas.numX;
        int levelFinenessY = fas.numY;
        for (int i = 0; i < numLevels; ++i) {
            levelFinenessX /= 2;
            levelFinenessY /= 2;
            int coarseGridSize = levelFinenessX * levelFinenessY;

            velocityLevels[i].resize(coarseGridSize, 0.f);
            cellTypeLevels[i].resize(coarseGridSize, 0.f);
        }
    }

    void Prolongate() {
        int minRes = std::min(fas.numX, fas.numY);
        int numLevels = sqrt(minRes) - sqrt(max_multigrid_coarseness);

        // store stencil and iterate over surrounding 16 cells
        float corner = 1.f / 64;
        float edge   = 3.f / 64;
        float inside = 9.f / 64;
        float stencil[16] = {
                            corner, edge, edge, corner,
                            edge, inside, inside, edge,
                            edge, inside, inside, edge,
                            corner, edge, edge, corner
                            };
    
        // subtract 2 to eliminate explicit solid boundary
        int coarseNumX = fas.numX - 2;
        int coarseNumY = fas.numY - 2;
        for (int i = 0; i < numLevels; ++i) {
            int fineNumX = coarseNumX + 2 * (i == 0);
            int fineNumY = coarseNumY + 2 * (i == 0);
            coarseNumX /= 2;
            coarseNumY /= 2;

            // coarse grids don't have an explicit layer of boundary cells
            for (int fi = 0; fi < coarseNumX; ++fi) {
                for (int fj = 0; fj < coarseNumY; ++fj) {
                    int coarseIdx = fi * coarseNumX + fj;

                    bool finestCoarse = i == 0;

                    std::vector<uint8_t> &fineGrid = i > 0 ? cellTypeLevels[i - 1] : fas.cellType;

                    int dirichlet = 0, interior = 0;
                    
                    // Add one to fineX/Y if the previous grid is the original (original grid has explicit layer of boundary cells)
                    int fineX = 2 * fi + finestCoarse;
                    int fineY = 2 * fi + finestCoarse;
                    for (int childX = fineX; childX < fineX + 2; ++childX) {
                        for (int childY = fineY; childY < fineY + 2; ++childY) {
                            int fineIdx = childX * fineNumY + childY;
                            int cellType = fineGrid[fineIdx];
                            if (cellType == fas.AIR) {
                                dirichlet++;
                            }
                            else if (cellType == fas.FLUID) {
                                interior++;
                            }
                        }
                    }

                    if (dirichlet > 0) {
                        cellTypeLevels[i][coarseIdx] = fas.AIR;
                    }
                    else if (dirichlet == 0 && interior > 0) {
                        cellTypeLevels[i][coarseIdx] = fas.FLUID;
                    }
                    else {
                        cellTypeLevels[i][coarseIdx] = fas.SOLID;
                    }
                }
            }
        }
    }

    void prolongate() {

    }

    void projectVCycle() {

    }*/
    // --------------------------------------------------------------------------------------------------------------------------------------------



    // conjugate gradient code
    // --------------------------------------------------------------------------------------------------------------------------------------------

    /*void setUpResidual() {
        for (int32_t i = 1; i < fas.numX - 1; ++i) {
            for (int32_t j = 1; j < fas.numY - 1; ++j) {
                int32_t idx = i * n + j;
                if (fas.cellType[idx] != FLUID)  {
                    continue;
                }

                float divergence = -(fas.u[idx + n] - fas.u[idx] + fas.v[idx + 1] - fas.v[idx]);

                if (fas.particleRestDensity > 0.f) {
                    float compression = fas.cellDensities[idx] - fas.particleRestDensity;
                    divergence += (compression > 0.f) * this->k * compression;
                }

                residual[idx] = divergence;

            }
        }
    }

    void ScaledAdd(std::vector<double> &a, std::vector<double> &b, double c) {
        for (int i = 1; i < fas.numX - 1; ++i) {
            if (fas.cellType[i] == FLUID) {
                a[i] += b[i] * c;
            }
        }
    }

    double Dot(std::vector<double> &a, std::vector<double> &b) {
        double res = 0.0;
        for (int i = 1; i < fas.numX - 1; ++i) {
            if (fas.cellType[i] == FLUID) {
                res += a[i] * b[i];
            }
        }
        return res;
    }

    void EqualsPlusTimes(std::vector<double> &a, std::vector<double> &b, double c) {
        for (int i = 1; i < fas.numX - 1; ++i) {
            if (fas.cellType[i] == FLUID) {
                a[i] = b[i] + a[i] * c;
            }
        }
    }

    void applyPressure() {
        //const float density = 1000.f;
        //const float scale = dt / (density * cellSpacing);
    
        for (int i = 1; i < fas.numX - 1; ++i) {
            for (int j = 1; j < fas.numY - 1; ++j) {
                int idx = i * n + j;
                int leftIdx =  idx - n;
                int upIdx = idx - 1;

                if ((fas.cellType[idx] == FLUID || fas.cellType[leftIdx] == FLUID) &&
                    fas.cellType[idx] != SOLID && fas.cellType[leftIdx] != SOLID) {
                    float p = pressure[idx];
                    float pLeft = pressure[leftIdx];
                    fas.u[idx] -= 1 * (p - pLeft);
                }
    
                if ((fas.cellType[idx] == FLUID || fas.cellType[upIdx] == FLUID) &&
                    fas.cellType[idx] != SOLID && fas.cellType[upIdx] != SOLID) {
                    float p = pressure[idx];
                    float pTop = pressure[upIdx];
                    fas.v[idx] -= 1 * (p - pTop);
                }
            }
        }
    }

    uint8_t getMaterialFromDirection(uint8_t direction, int idx) {
        switch (direction) {
            case LEFT:
                return fas.cellType[idx - n];
            case RIGHT:
                return fas.cellType[idx + n];
            case BOTTOM:
                return fas.cellType[idx + 1];
            case TOP:
                return fas.cellType[idx - 1];
        }

        return SOLID; // should never get executed
    }

    uint8_t updateNbrFromNeighbor(uint8_t material, uint8_t nbr_info, uint8_t dir) {

        // if neighbor cell is fluid or air, add to the first 3 bits
        if (material != SOLID) {
            nbr_info++;
        }

        // if neighbor cell is not fluid, end here
        if (material != FLUID) {
            return nbr_info;
        }

        // else, add the direction onto the last 4 bits of nbr_info. Using OR here because we don't wanna touch the first 3 bits
        return nbr_info | dir;
    }

    void setUpNeighbors() {
        for (int i = 1; i < fas.numX - 1; ++i) {
            for (int j = 1; j < fas.numY - 1; ++j) {
                int idx = i * n + j;
                if (fas.cellType[idx] != FLUID) continue;

                uint8_t nbr_info = 0u;
                for (uint8_t dir : directions) {
                    uint8_t material = getMaterialFromDirection(dir, idx);
                    nbr_info = updateNbrFromNeighbor(material, nbr_info, dir);
                }

                neighbors[idx] = nbr_info;
            }
        }
    }

    void MatVec() {
        for (int i = 1; i < fas.numX - 1; ++i) {
            for (int j = 1; j < fas.numY - 1; ++j) {
                int idx = i * n + j;

                uint8_t nbrs = neighbors[idx];
                if (!nbrs) { // if a cell has no neighbors continue
                    Adiag[idx] = 0.0;
                    continue;
                }

                // all of these & statements are just looking at different bits in nbrs
                // nbrs & CENTER just extracts the first 3 bits of nbrs; if nbrs is 1001 010, then nbrs & center = 1001 010 & 0000 111 = 0000 010
                Adiag[idx] = 
                    ((nbrs & CENTER) * direction[idx]) -
                    ((nbrs & LEFT) ? direction[idx - n] : 0) -
                    ((nbrs & RIGHT) ? direction[idx + n] : 0) -
                    ((nbrs & BOTTOM) ? direction[idx + 1] : 0) -
                    ((nbrs & TOP) ? direction[idx - 1] : 0);
            }
        }
    }

    // conjugate gradient
    void projectCG() {

        setUpNeighbors();

        std::fill(begin(pressure), end(pressure), 0.0);
        std::fill(begin(Adiag), end(Adiag), 0.0);

        setUpResidual();

        std::copy(begin(residual), end(residual), begin(direction));

        double sigma = DotMulti(residual, residual);
        
        for (int iter = 0; iter < numPressureIters && sigma > 0; ++iter) {
            MatVecMulti();
            
            double alpha = sigma / DotMulti(direction, Adiag);
            ScaledAddMulti(pressure, direction, alpha);
            ScaledAddMulti(residual, Adiag, -alpha);
            
            double sigmaOld = sigma;
            sigma = DotMulti(residual, residual);
            EqualsPlusTimesMulti(direction, residual, sigma / sigmaOld);
        }

        applyPressureMulti();
    }

    // general preconditioned conjugate gradient (using matrixless GS/Jacobi methods, so no z needed)
    void projectPCG(std::function<void()> precondition) {

        setUpNeighbors();

        std::fill(begin(pressure), end(pressure), 0.0);
        std::fill(begin(Adiag), end(Adiag), 0.0);

        setUpResidual();

        precondition();

        std::copy(begin(residual), end(residual), begin(direction));

        double sigma = DotMulti(residual, residual);
        
        for (int iter = 0; iter < numPressureIters && sigma > 0; ++iter) {
            MatVecMulti();
            
            double alpha = sigma / DotMulti(direction, Adiag);
            ScaledAddMulti(pressure, direction, alpha);
            ScaledAddMulti(residual, Adiag, -alpha);

            precondition();
            
            double sigmaOld = sigma;
            sigma = DotMulti(residual, residual);
            EqualsPlusTimesMulti(direction, residual, sigma / sigmaOld);
        }

        applyPressureMulti();
    }*/
    // --------------------------------------------------------------------------------------------------------------------------------------------
};