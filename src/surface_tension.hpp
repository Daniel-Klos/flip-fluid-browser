#pragma once
#include <cmath>
#include <algorithm>
#include "simulation_state.hpp"

class SurfaceTension {
    FluidState &fas;
    int n;
    std::vector<float> c, tmp, gx, gy, nx_, ny_, fx, fy;

    // neighbor lookup with Neumann treatment of solids
    inline float val(const std::vector<float> &b, int nb, int center) const {
        return fas.cellType[nb] == fas.SOLID ? b[center] : b[nb];
    }

    void Blur(std::vector<float> &src, std::vector<float> &dst) {
        dst = src;
        for (int i = 1; i < fas.numX - 1; ++i)
            for (int j = 1; j < fas.numY - 1; ++j) {
                int idx = i * n + j;
                if (fas.cellType[idx] == fas.SOLID) continue;
                float s = 0.f;
                for (int di = -1; di <= 1; ++di)
                    for (int dj = -1; dj <= 1; ++dj)
                        s += val(src, idx + di * n + dj, idx);
                dst[idx] = s * (1.f / 9.f);
            }
    }

public:
    float h  = fas.cellSpacing;
    float strength;
    float sigma;  // strength * gravity * h^2
    int   blurPasses = 2;
    float maxDispPerStep = 0.25f; // cap on velocity change, in cells of motion per step

    SurfaceTension(FluidState &s) :
        fas(s),
        n(s.numY),
        strength(s.surfTenStrength),
        sigma(s.surfTenStrength * fas.gravityMag * h * h)
    {
        size_t N = (size_t)s.numX * s.numY;
        for (auto *v : {&c, &tmp, &gx, &gy, &nx_, &ny_, &fx, &fy}) v->assign(N, 0.f);
    }

    // call after cell types + cellDensities are up to date, before the pressure solve
    void Apply() {
        const float d0 = fas.particleRestDensity;
        if (sigma <= 0.f || d0 <= 0.f) return;

        const int NX = fas.numX, NY = fas.numY;
        const float h = fas.cellSpacing;
        const float invD0 = 1.f / d0;
        const float half_inv_h = 0.5f / h;

        // 1. normalized, smoothed color field
        for (size_t k = 0; k < c.size(); ++k)
            c[k] = clamp(fas.cellDensities[k] * invD0, 0.f, 1.f);
        for (int p = 0; p < blurPasses; ++p) { Blur(c, tmp); c.swap(tmp); }

        // 2. gradient and inward unit normal
        const float eps = 0.05f / h;   // ignore near-flat regions (interior / far air)
        for (int i = 1; i < NX - 1; ++i)
            for (int j = 1; j < NY - 1; ++j) {
                int idx = i * n + j;
                gx[idx] = gy[idx] = nx_[idx] = ny_[idx] = 0.f;
                if (fas.cellType[idx] == fas.SOLID) continue;

                float dx = (val(c, idx + n, idx) - val(c, idx - n, idx)) * half_inv_h;
                float dy = (val(c, idx + 1, idx) - val(c, idx - 1, idx)) * half_inv_h;
                float gl = std::sqrt(dx * dx + dy * dy);
                gx[idx] = dx; gy[idx] = dy;
                if (gl > eps) { 
                    nx_[idx] = dx / gl; ny_[idx] = dy / gl; 
                }
            }

        // 3. curvature and force per cell
        const float kmax = 1.f / h;
        const float maxAccel = maxDispPerStep * h / (fas.frame_context.dt * fas.frame_context.dt);   // keeps dv*dt <= fraction of a cell
        for (int i = 1; i < NX - 1; ++i)
            for (int j = 1; j < NY - 1; ++j) {
                int idx = i * n + j;
                fx[idx] = fy[idx] = 0.f;
                if (fas.cellType[idx] == fas.SOLID) continue;
                if (gx[idx] == 0.f && gy[idx] == 0.f) continue;

                float div = (val(nx_, idx + n, idx) - val(nx_, idx - n, idx) +
                             val(ny_, idx + 1, idx) - val(ny_, idx - 1, idx)) * half_inv_h;
                float kappa = clamp(-div, -kmax, kmax);

                float ax = sigma * kappa * gx[idx];
                float ay = sigma * kappa * gy[idx];
                float a = std::sqrt(ax * ax + ay * ay);
                if (a > maxAccel) { float s = maxAccel / a; ax *= s; ay *= s; }
                fx[idx] = ax; fy[idx] = ay;
            }

        // 4. apply to MAC faces (average the two adjacent cells)
        for (int i = 1; i < NX - 1; ++i)
            for (int j = 1; j < NY - 1; ++j) {
                int idx = i * n + j;
                if (fas.cellType[idx] == fas.SOLID) continue;

                int L = idx - n;   // u face sits between L and idx
                if (fas.cellType[L] != fas.SOLID &&
                    (fas.cellType[L] == fas.FLUID || fas.cellType[idx] == fas.FLUID))
                    fas.u[idx] += fas.frame_context.dt * 0.5f * (fx[L] + fx[idx]);

                int B = idx - 1;   // v face sits between B and idx
                if (fas.cellType[B] != fas.SOLID &&
                    (fas.cellType[B] == fas.FLUID || fas.cellType[idx] == fas.FLUID))
                    fas.v[idx] += fas.frame_context.dt * 0.5f * (fy[B] + fy[idx]);
            }
    }

    void setSurfaceTension(float set) {
        fas.surfTenStrength = set;

        sigma = fas.surfTenStrength * fas.gravityMag * h * h;
    }

    float getSurfaceTension() {
        return fas.surfTenStrength;
    }
};