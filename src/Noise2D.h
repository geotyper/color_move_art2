#pragma once

#include <QtGlobal>
#include <cmath>

// Simplex-style 2D noise with small fractal stack to drive brush variation.
class Noise2D
{
public:
    explicit Noise2D(quint32 seed = 1337u) : m_seed(seed) {}

    void setSeed(quint32 seed) { m_seed = seed; }

    // Single-octave simplex noise in [-1, 1]
    float noise(float x, float y) const
    {
        // Skew/unskew factors for 2D simplex
        const float F2 = 0.366025403784439f;   // (sqrt(3)-1)/2
        const float G2 = 0.211324865405187f;   // (3-sqrt(3))/6

        float s = (x + y) * F2;
        int i = fastFloor(x + s);
        int j = fastFloor(y + s);

        float t = (i + j) * G2;
        float X0 = i - t;
        float Y0 = j - t;
        float x0 = x - X0;
        float y0 = y - Y0;

        // Determine which simplex corner is which
        int i1 = (x0 > y0) ? 1 : 0;
        int j1 = (x0 > y0) ? 0 : 1;

        float x1 = x0 - static_cast<float>(i1) + G2;
        float y1 = y0 - static_cast<float>(j1) + G2;
        float x2 = x0 - 1.0f + 2.0f * G2;
        float y2 = y0 - 1.0f + 2.0f * G2;

        float n0 = corner(i, j, x0, y0);
        float n1 = corner(i + i1, j + j1, x1, y1);
        float n2 = corner(i + 1, j + 1, x2, y2);

        // Scale to roughly [-1,1]
        return 70.0f * (n0 + n1 + n2);
    }

    // Multi-octave fractal noise in [-1, 1]
    float fractal(float x, float y, int octaves = 3, float lacunarity = 2.0f, float gain = 0.5f) const
    {
        float amp = 1.0f;
        float freq = 1.0f;
        float sum = 0.0f;
        float norm = 0.0f;
        for (int o = 0; o < octaves; ++o) {
            sum += amp * noise(x * freq, y * freq);
            norm += amp;
            amp *= gain;
            freq *= lacunarity;
        }
        return norm > 0.0f ? sum / norm : 0.0f;
    }

private:
    static int fastFloor(float v) { return (v >= 0.0f) ? static_cast<int>(v) : static_cast<int>(v) - 1; }

    quint32 hash(int x, int y) const
    {
        quint32 h = m_seed;
        h ^= static_cast<quint32>(x) * 374761393u;
        h ^= static_cast<quint32>(y) * 668265263u;
        h = (h ^ (h >> 13)) * 1274126177u;
        return h ^ (h >> 16);
    }

    float corner(int ix, int iy, float x, float y) const
    {
        float t = 0.5f - x * x - y * y;
        if (t < 0.0f) return 0.0f;

        static const float grads[8][2] = {
            {1.0f, 0.0f}, {-1.0f, 0.0f}, {0.0f, 1.0f}, {0.0f, -1.0f},
            {0.7071f, 0.7071f}, {-0.7071f, 0.7071f}, {0.7071f, -0.7071f}, {-0.7071f, -0.7071f}
        };
        const float *g = grads[hash(ix, iy) & 7u];
        t *= t;
        return t * t * (g[0] * x + g[1] * y);
    }

    quint32 m_seed;
};
