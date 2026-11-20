#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace vx
{
// Строка UTF-8 (русский текст в исходниках)
inline juce::String U (const char* s) { return juce::String::fromUTF8 (s); }

inline float dbToLin (float db) { return std::pow (10.0f, db * 0.05f); }
inline float linToDb (float x)  { return 20.0f * std::log10 (std::max (x, 1.0e-9f)); }

// Коэффициент однополюсного сглаживания по времени (мс)
inline float timeCoef (float ms, double sr)
{
    return std::exp (-1.0f / (std::max (ms, 0.01f) * 0.001f * (float) sr));
}

//==============================================================================
// Циркулярный буфер задержки с линейной интерполяцией.
// read (0) вернёт последний записанный отсчёт, read (N) - отсчёт N сэмплов назад.
class DelayBuffer
{
public:
    void prepare (int maxSamples) { buf.assign ((size_t) maxSamples + 4, 0.0f); w = 0; }
    void clear() { std::fill (buf.begin(), buf.end(), 0.0f); }

    void write (float x)
    {
        buf[(size_t) w] = x;
        if (++w >= (int) buf.size()) w = 0;
    }

    float read (float delay) const
    {
        const int n = (int) buf.size();
        float pos = (float) w - 1.0f - delay;
        while (pos < 0.0f) pos += (float) n;
        int i0 = (int) pos;
        const float fr = pos - (float) i0;
        if (i0 >= n) i0 -= n;
        int i1 = i0 + 1;
        if (i1 >= n) i1 -= n;
        return buf[(size_t) i0] + fr * (buf[(size_t) i1] - buf[(size_t) i0]);
    }

private:
    std::vector<float> buf;
    int w = 0;
};

//==============================================================================
// Бикуад (RBJ cookbook), без аллокаций - можно менять коэффициенты в аудио-потоке.
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    void reset() { z1 = z2 = 0.0f; }

    float process (float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void setLowPass (double sr, float f, float q)
    {
        float c, al; prep (sr, f, q, c, al);
        set ((1 - c) * 0.5f, 1 - c, (1 - c) * 0.5f, 1 + al, -2 * c, 1 - al);
    }
    void setHighPass (double sr, float f, float q)
    {
        float c, al; prep (sr, f, q, c, al);
        set ((1 + c) * 0.5f, -(1 + c), (1 + c) * 0.5f, 1 + al, -2 * c, 1 - al);
    }
    void setBandPass (double sr, float f, float q)
    {
        float c, al; prep (sr, f, q, c, al);
        set (al, 0.0f, -al, 1 + al, -2 * c, 1 - al);
    }
    void setPeak (double sr, float f, float q, float gainDb)
    {
        float c, al; prep (sr, f, q, c, al);
        const float A = std::pow (10.0f, gainDb / 40.0f);
        set (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }
    void setHighShelf (double sr, float f, float gainDb)
    {
        float c, al; prep (sr, f, 0.7071f, c, al);
        const float A = std::pow (10.0f, gainDb / 40.0f);
        const float t = 2.0f * std::sqrt (A) * al;
        set (A * ((A + 1) + (A - 1) * c + t),
             -2 * A * ((A - 1) + (A + 1) * c),
             A * ((A + 1) + (A - 1) * c - t),
             (A + 1) - (A - 1) * c + t,
             2 * ((A - 1) - (A + 1) * c),
             (A + 1) - (A - 1) * c - t);
    }

private:
    static void prep (double sr, float f, float q, float& c, float& al)
    {
        f = std::min (f, 0.45f * (float) sr);
        const float w = juce::MathConstants<float>::twoPi * f / (float) sr;
        c = std::cos (w);
        al = std::sin (w) / (2.0f * q);
    }
    void set (float nb0, float nb1, float nb2, float a0, float na1, float na2)
    {
        b0 = nb0 / a0; b1 = nb1 / a0; b2 = nb2 / a0; a1 = na1 / a0; a2 = na2 / a0;
    }
};
} // namespace vx
