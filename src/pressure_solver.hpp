#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
#include "simulation_state.hpp"

/*
 * Pressure projection for the MAC grid in FluidState.
 *
 * Three interchangeable methods (set `method`):
 *   SOR            - your original red/black SOR on the velocities (unchanged).
 *   Multigrid      - stand-alone W-cycles.
 *   MultigridPCG   - conjugate gradient preconditioned by one W-cycle (default, most robust).
 *
 * The multigrid methods solve for an explicit pressure field p with
 *     (n_open * p_c  -  sum_{fluid nbrs} p_nb)  =  target_div_c - div(u)_c
 * where n_open counts non-solid neighbours. AIR cells are Dirichlet (p = 0) and SOLID
 * neighbours are Neumann (the face velocity is left alone). This is the same linear system
 * your SOR iterates on, so the result is a drop-in replacement.
 *
 * Irregular geometry: coarse levels are NOT rebuilt from coarsened cell types. Each fine
 * level stores its operator as face conductances (wR, wB) plus a Dirichlet term, and the
 * coarse operator is the Galerkin product P^T A P with 2x2 piecewise-constant aggregation.
 * For this P the product is again a 5-point operator whose face conductances are just the sums
 * of the fine faces crossing each aggregate boundary. Walls, single solid cells, and free
 * surfaces therefore carry down to every level automatically.
 */
struct PressureSolver {
    FluidState &fas;
    int n;          // row stride (= numY); cell (i, j) lives at i * n + j
    int gridSize;

    enum class Method { SOR, Multigrid, MultigridPCG };
    Method method = Method::MultigridPCG;

    // shared
    float k = 4.f;                 // density-compression stiffness (same meaning as before)

    // SOR (legacy)
    int numPressureIters = 40;
    float overRelaxation = 1.9f;

    // Multigrid
    int max_multigrid_coarseness = 4;   // stop coarsening once both interior dims are <= this
    int smoothSweeps = 2;               // red/black GS sweeps before AND after each coarse correction
    int coarseSweeps = 10;              // extra sweeps on the coarsest level (forward + backward)
    int coarseVisits = 2;               // 1 = V-cycle, 2 = W-cycle (W is ~2x the work, far better on irregular domains)
    float overCorrection = 1.5f;        // scales the coarse-grid correction; keep in (1, 2), see ProlongateAdd
    int numCycles = 4;                  // Method::Multigrid: max cycles
    int numCGIters = 12;                // Method::MultigridPCG: max CG iterations
    float tolerance = 1e-3f;            // stop early when ||r|| <= tolerance * ||b||

    // diagnostics (valid after SolvePressure)
    int lastIterations = 0;
    float lastRelativeResidual = 0.f;

    // pressure from the last multigrid solve (zero in AIR/SOLID cells). Handy for debug drawing.
    std::vector<float> pressure;

    PressureSolver(FluidState &fluid_attributes) : fas(fluid_attributes) {
        Resize();
    }

    // Call again if numX / numY ever change.
    void Resize() {
        n = fas.numY;
        gridSize = fas.gridSize;
        pressure.assign((size_t)fas.numX * fas.numY, 0.f);
        BuildLevelLayout();
    }

    void SolvePressure() {
        switch (method) {
            case Method::SOR:
                SolvePressureSOR();
                return;
            case Method::Multigrid:
            case Method::MultigridPCG:
                break;
        }

        BuildHierarchy();
        BuildRightHandSide();
        RemoveNullSpace();

        if (method == Method::MultigridPCG) RunPCG();
        else                                RunCycles();

        ApplyPressure();
    }


    // ============================================================================================
    // Legacy SOR (unchanged)
    // ============================================================================================

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

    void SolvePressureSOR() {
        for (int iter = 0; iter < numPressureIters; ++iter) {
            passRedBlackSOR(0);
            passRedBlackSOR(1);
        }
    }


    // ============================================================================================
    // Multigrid
    // ============================================================================================

    // One level of the hierarchy. Every level carries a 1-cell ring of inactive cells (level 0's
    // ring is FluidState's solid border), so interior loops never need bounds checks.
    // Layout matches level 0 == FluidState: index = i * ny + j, right = +ny, bottom = +1.
    struct Level {
        int nx = 0, ny = 0;
        std::vector<float> wR, wB;      // conductance to the right (+i) / bottom (+j) neighbour
        std::vector<float> dirichlet;   // conductance to AIR (p = 0)
        std::vector<float> diag;        // A(c,c) = sum of all conductances; 0 => inactive cell
        std::vector<float> invDiag;     // 1 / diag, or 0 for inactive cells
        std::vector<float> x, b, r;     // solution/correction, right-hand side, residual

        void allocate(int nx_, int ny_) {
            nx = nx_;
            ny = ny_;
            const size_t sz = (size_t)nx * ny;
            for (std::vector<float> *v : {&wR, &wB, &dirichlet, &diag, &invDiag, &x, &b, &r}) {
                v->assign(sz, 0.f);
            }
        }
    };

    std::vector<Level> levels;
    std::vector<float> cgR, cgZ, cgD, cgQ;          // PCG work vectors
    std::vector<uint8_t> visited;                   // null-space flood fill scratch
    std::vector<int> floodStack, floodCells;
    std::vector<int> sealedCells;                   // cells of every sealed (air-free) fluid blob, blob after blob
    std::vector<int> sealedStarts;                  // blob b = sealedCells[sealedStarts[b] .. sealedStarts[b+1])

    void BuildLevelLayout() {
        levels.clear();
        int nx = fas.numX, ny = fas.numY;
        while (true) {
            levels.emplace_back();
            levels.back().allocate(nx, ny);

            const int ix = nx - 2, iy = ny - 2;     // interior sizes
            if (ix <= max_multigrid_coarseness && iy <= max_multigrid_coarseness) break;
            nx = (ix + 1) / 2 + 2;                  // ceil(interior / 2) + ring
            ny = (iy + 1) / 2 + 2;
        }

        const size_t sz = (size_t)fas.numX * fas.numY;
        cgR.assign(sz, 0.f);
        cgZ.assign(sz, 0.f);
        cgD.assign(sz, 0.f);
        cgQ.assign(sz, 0.f);
        visited.assign(sz, 0);
    }

    static void FinalizeLevel(Level &L) {
        for (int i = 1; i < L.nx - 1; ++i) {
            for (int j = 1; j < L.ny - 1; ++j) {
                const int idx = i * L.ny + j;
                const float d = L.wR[idx] + L.wR[idx - L.ny] + L.wB[idx] + L.wB[idx - 1] + L.dirichlet[idx];
                L.diag[idx] = d;
                L.invDiag[idx] = d > 0.f ? 1.f / d : 0.f;
            }
        }
    }

    // Level 0 straight from the cell types. Only FLUID cells are unknowns.
    void BuildFineLevel() {
        Level &L = levels[0];
        std::fill(L.wR.begin(), L.wR.end(), 0.f);
        std::fill(L.wB.begin(), L.wB.end(), 0.f);
        std::fill(L.dirichlet.begin(), L.dirichlet.end(), 0.f);

        for (int i = 1; i < fas.numX - 1; ++i) {
            for (int j = 1; j < fas.numY - 1; ++j) {
                const int idx = i * n + j;
                if (fas.cellType[idx] != fas.FLUID) continue;

                const auto cl = fas.cellType[idx - n];
                const auto cr = fas.cellType[idx + n];
                const auto ct = fas.cellType[idx - 1];
                const auto cb = fas.cellType[idx + 1];

                // fluid-fluid faces couple pressures; the left/top conductance of a cell is read
                // from its neighbour's wR/wB, so the operator is symmetric by construction
                L.wR[idx] = (cr == fas.FLUID) ? 1.f : 0.f;
                L.wB[idx] = (cb == fas.FLUID) ? 1.f : 0.f;

                // fluid-air faces are Dirichlet (p = 0): diagonal only. Solid faces: nothing.
                L.dirichlet[idx] = float((cl == fas.AIR) + (cr == fas.AIR) + (ct == fas.AIR) + (cb == fas.AIR));
            }
        }
        FinalizeLevel(L);
    }

    // Galerkin coarse operator P^T A P for 2x2 piecewise-constant aggregation.
    // A coarse face is the sum of the fine faces that cross the aggregate boundary; faces inside
    // an aggregate vanish; Dirichlet conductance is summed.
    void BuildCoarseLevel(const Level &F, Level &C) {
        std::fill(C.wR.begin(), C.wR.end(), 0.f);
        std::fill(C.wB.begin(), C.wB.end(), 0.f);
        std::fill(C.dirichlet.begin(), C.dirichlet.end(), 0.f);

        for (int i = 1; i < F.nx - 1; ++i) {
            const int I = ((i - 1) >> 1) + 1;
            const bool rightEdge = ((i - 1) & 1) != 0;      // right neighbour is in the next aggregate
            for (int j = 1; j < F.ny - 1; ++j) {
                const int J = ((j - 1) >> 1) + 1;
                const bool bottomEdge = ((j - 1) & 1) != 0;

                const int fi = i * F.ny + j;
                const int ci = I * C.ny + J;
                C.dirichlet[ci] += F.dirichlet[fi];
                if (rightEdge)  C.wR[ci] += F.wR[fi];
                if (bottomEdge) C.wB[ci] += F.wB[fi];
            }
        }
        FinalizeLevel(C);
    }

    void BuildHierarchy() {
        BuildFineLevel();
        for (size_t l = 0; l + 1 < levels.size(); ++l) {
            BuildCoarseLevel(levels[l], levels[l + 1]);
        }
    }

    // b = (target divergence) - (current divergence)
    void BuildRightHandSide() {
        Level &L = levels[0];
        const bool useDensity = fas.particleRestDensity > 0.f;

        for (int i = 1; i < fas.numX - 1; ++i) {
            for (int j = 1; j < fas.numY - 1; ++j) {
                const int idx = i * n + j;
                if (L.invDiag[idx] == 0.f) {
                    L.b[idx] = 0.f;
                    continue;
                }

                const float divergence = fas.u[idx + n] - fas.u[idx] + fas.v[idx + 1] - fas.v[idx];

                float target = 0.f;
                if (useDensity) {
                    const float compression = fas.cellDensities[idx] - fas.particleRestDensity;
                    if (compression > 0.f) target = k * compression;
                }
                L.b[idx] = target - divergence;
            }
        }
    }

    // A connected blob of fluid with no AIR contact (a full sealed chamber) has a singular
    // matrix: it is only solvable if its right-hand side sums to zero, and CG can blow up if
    // it doesn't. Find every such blob and subtract the mean over it from b, so the system is
    // always consistent. (Physically: net compression is spread evenly instead of being
    // unsatisfiable.) The blob lists are kept so PCG can re-project its residual each
    // iteration; otherwise rounding error leaks into the constant mode and gets amplified.
    void RemoveNullSpace() {
        Level &L = levels[0];
        std::fill(visited.begin(), visited.end(), 0);
        sealedCells.clear();
        sealedStarts.clear();

        for (int i = 1; i < L.nx - 1; ++i) {
            for (int j = 1; j < L.ny - 1; ++j) {
                const int start = i * L.ny + j;
                if (L.invDiag[start] == 0.f || visited[start]) continue;

                floodCells.clear();
                floodStack.clear();
                floodStack.push_back(start);
                visited[start] = 1;

                bool touchesAir = false;
                while (!floodStack.empty()) {
                    const int c = floodStack.back();
                    floodStack.pop_back();
                    floodCells.push_back(c);
                    if (L.dirichlet[c] > 0.f) touchesAir = true;

                    const int nbr[4] = {c + L.ny, c - L.ny, c + 1, c - 1};
                    const float w[4] = {L.wR[c], L.wR[c - L.ny], L.wB[c], L.wB[c - 1]};
                    for (int d = 0; d < 4; ++d) {
                        if (w[d] > 0.f && !visited[nbr[d]]) {
                            visited[nbr[d]] = 1;
                            floodStack.push_back(nbr[d]);
                        }
                    }
                }

                if (!touchesAir) {
                    sealedStarts.push_back((int)sealedCells.size());
                    sealedCells.insert(sealedCells.end(), floodCells.begin(), floodCells.end());
                }
            }
        }
        sealedStarts.push_back((int)sealedCells.size());    // sentinel

        ProjectNullSpace(L.b);
    }

    // subtract the per-blob mean of v over every sealed blob
    void ProjectNullSpace(std::vector<float> &v) const {
        for (size_t blob = 0; blob + 1 < sealedStarts.size(); ++blob) {
            const int a = sealedStarts[blob], e = sealedStarts[blob + 1];
            double sum = 0.0;
            for (int q = a; q < e; ++q) sum += v[sealedCells[q]];
            const float mean = float(sum / double(e - a));
            for (int q = a; q < e; ++q) v[sealedCells[q]] -= mean;
        }
    }

    // ---- level operations -------------------------------------------------------------------

    // One red (color 0) or black (color 1) Gauss-Seidel half-sweep. Inactive cells have
    // invDiag == 0 so they stay exactly 0.
    static void ColorSweep(Level &L, int color) {
        const int ny = L.ny;
        float *x = L.x.data();
        const float *b = L.b.data();
        const float *wR = L.wR.data();
        const float *wB = L.wB.data();
        const float *inv = L.invDiag.data();

        for (int i = 1; i < L.nx - 1; ++i) {
            for (int j = 2 - ((i + color) & 1); j < ny - 1; j += 2) {
                const int idx = i * ny + j;
                const float s = b[idx]
                              + wR[idx]      * x[idx + ny]
                              + wR[idx - ny] * x[idx - ny]
                              + wB[idx]      * x[idx + 1]
                              + wB[idx - 1]  * x[idx - 1];
                x[idx] = s * inv[idx];
            }
        }
    }

    // forward = red then black, backward = black then red.
    // pre-smoothing uses forward, post-smoothing uses backward: that keeps the whole cycle
    // symmetric, which CG requires of its preconditioner.
    static void Smooth(Level &L, bool forward) {
        ColorSweep(L, forward ? 0 : 1);
        ColorSweep(L, forward ? 1 : 0);
    }

    // out = A * in
    static void ApplyA(const Level &L, const std::vector<float> &in, std::vector<float> &out) {
        const int ny = L.ny;
        for (int i = 1; i < L.nx - 1; ++i) {
            for (int j = 1; j < ny - 1; ++j) {
                const int idx = i * ny + j;
                out[idx] = L.diag[idx] * in[idx]
                         - L.wR[idx]      * in[idx + ny]
                         - L.wR[idx - ny] * in[idx - ny]
                         - L.wB[idx]      * in[idx + 1]
                         - L.wB[idx - 1]  * in[idx - 1];
            }
        }
    }

    static void ComputeResidual(Level &L) {
        ApplyA(L, L.x, L.r);
        for (size_t c = 0; c < L.r.size(); ++c) L.r[c] = L.b[c] - L.r[c];
    }

    // coarse.b = P^T fine.r  (sum over each 2x2 aggregate), coarse.x = 0
    static void Restrict(const Level &F, Level &C) {
        std::fill(C.b.begin(), C.b.end(), 0.f);
        std::fill(C.x.begin(), C.x.end(), 0.f);
        for (int i = 1; i < F.nx - 1; ++i) {
            const int I = ((i - 1) >> 1) + 1;
            for (int j = 1; j < F.ny - 1; ++j) {
                const int J = ((j - 1) >> 1) + 1;
                C.b[I * C.ny + J] += F.r[i * F.ny + j];
            }
        }
    }

    // fine.x += scale * P coarse.x  (inactive fine cells must stay at exactly 0: AIR is p = 0)
    //
    // Piecewise-constant interpolation under-corrects smooth error: for 2x2 aggregates in 2D the
    // Galerkin coarse operator is ~2x too stiff, so the coarse solution comes out ~2x too small.
    // Scaling the correction by `overCorrection` (known as over-correction) wins most of that
    // back. It must stay below 2; at 2 the cycle can become indefinite and stop being a valid
    // CG preconditioner.
    static void ProlongateAdd(const Level &C, Level &F, float scale) {
        for (int i = 1; i < F.nx - 1; ++i) {
            const int I = ((i - 1) >> 1) + 1;
            for (int j = 1; j < F.ny - 1; ++j) {
                const int fi = i * F.ny + j;
                if (F.invDiag[fi] == 0.f) continue;
                const int J = ((j - 1) >> 1) + 1;
                F.x[fi] += scale * C.x[I * C.ny + J];
            }
        }
    }

    // One multigrid cycle starting at level l (coarseVisits = 1: V, 2: W). Pre-smoothing is
    // red-black, post-smoothing black-red, both with the same sweep count, which makes the whole
    // cycle a symmetric operator as long as x starts at 0 (what CG needs from a preconditioner).
    void Cycle(int l) {
        Level &L = levels[l];

        if (l == (int)levels.size() - 1) {
            for (int s = 0; s < coarseSweeps; ++s) Smooth(L, true);
            for (int s = 0; s < coarseSweeps; ++s) Smooth(L, false);
            return;
        }

        for (int s = 0; s < smoothSweeps; ++s) Smooth(L, true);

        ComputeResidual(L);
        Level &C = levels[l + 1];
        Restrict(L, C);
        for (int v = 0; v < coarseVisits; ++v) Cycle(l + 1);
        ProlongateAdd(C, L, overCorrection);

        for (int s = 0; s < smoothSweeps; ++s) Smooth(L, false);
    }

    // ---- drivers ----------------------------------------------------------------------------

    static double Dot(const std::vector<float> &a, const std::vector<float> &b) {
        double s = 0.0;
        for (size_t c = 0; c < a.size(); ++c) s += (double)a[c] * b[c];
        return s;
    }

    // z = M^-1 r, with M one symmetric cycle started from zero
    void ApplyPreconditioner(const std::vector<float> &r, std::vector<float> &z) {
        Level &L = levels[0];
        std::copy(r.begin(), r.end(), L.b.begin());
        std::fill(L.x.begin(), L.x.end(), 0.f);
        Cycle(0);
        std::copy(L.x.begin(), L.x.end(), z.begin());
    }

    void RunCycles() {
        Level &L = levels[0];
        std::fill(L.x.begin(), L.x.end(), 0.f);
        lastIterations = 0;
        lastRelativeResidual = 0.f;

        const double bb = Dot(L.b, L.b);
        if (bb > 0.0) {
            for (int it = 0; it < numCycles; ++it) {
                Cycle(0);
                ComputeResidual(L);
                lastIterations = it + 1;
                lastRelativeResidual = float(std::sqrt(Dot(L.r, L.r) / bb));
                if (lastRelativeResidual <= tolerance) break;
            }
        }
        std::copy(L.x.begin(), L.x.end(), pressure.begin());
    }

    void RunPCG() {
        Level &L = levels[0];
        std::fill(pressure.begin(), pressure.end(), 0.f);
        lastIterations = 0;
        lastRelativeResidual = 0.f;

        std::copy(L.b.begin(), L.b.end(), cgR.begin());     // x = 0  =>  r = b
        const double rr0 = Dot(cgR, cgR);
        if (rr0 <= 0.0) return;

        ApplyPreconditioner(cgR, cgZ);
        cgD = cgZ;
        double sigma = Dot(cgR, cgZ);

        for (int it = 0; it < numCGIters; ++it) {
            ApplyA(L, cgD, cgQ);
            const double dq = Dot(cgD, cgQ);
            if (!(dq > 0.0) || !(sigma > 0.0)) break;

            const float alpha = float(sigma / dq);
            for (size_t c = 0; c < pressure.size(); ++c) {
                pressure[c] += alpha * cgD[c];
                cgR[c]      -= alpha * cgQ[c];
            }
            ProjectNullSpace(cgR);
            lastIterations = it + 1;

            const double rr = Dot(cgR, cgR);
            lastRelativeResidual = float(std::sqrt(rr / rr0));
            if (lastRelativeResidual <= std::max(tolerance, 1e-6f)) break;   // 1e-6: float noise floor

            ApplyPreconditioner(cgR, cgZ);
            const double sigmaNew = Dot(cgR, cgZ);
            const float beta = float(sigmaNew / sigma);
            sigma = sigmaNew;
            for (size_t c = 0; c < cgD.size(); ++c) cgD[c] = cgZ[c] + beta * cgD[c];
        }
    }

    // u_face -= (p_next - p_prev) on every face between two non-solid cells where at least one
    // is FLUID. These are exactly the faces your SOR touches. Each face is applied once: a cell
    // owns its left/top face always, and its right/bottom face only when that neighbour is not
    // FLUID (a FLUID neighbour owns it as its own left/top face). `pressure` is 0 outside FLUID.
    void ApplyPressure() {
        auto open = [&](auto t) { return t == fas.FLUID || t == fas.AIR; };

        for (int i = 1; i < fas.numX - 1; ++i) {
            for (int j = 1; j < fas.numY - 1; ++j) {
                const int idx = i * n + j;
                if (fas.cellType[idx] != fas.FLUID) continue;

                const float p = pressure[idx];

                const auto cl = fas.cellType[idx - n];
                const auto cr = fas.cellType[idx + n];
                const auto ct = fas.cellType[idx - 1];
                const auto cb = fas.cellType[idx + 1];

                if (open(cl))                          fas.u[idx]     -= p - pressure[idx - n];
                if (open(cr) && cr != fas.FLUID)       fas.u[idx + n] += p;
                if (open(ct))                          fas.v[idx]     -= p - pressure[idx - 1];
                if (open(cb) && cb != fas.FLUID)       fas.v[idx + 1] += p;
            }
        }
    }
};