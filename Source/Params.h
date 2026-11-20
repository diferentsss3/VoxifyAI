#pragma once
#include "Common.h"

namespace vx
{
// Порядок = порядок в цепочке обработки
enum Mod { Noise, Tune, EQ, Comp, DeEss, Sat, Exc, Dbl, Reverb, Delay, Stereo, NumMods };

inline const char* modId (int m)
{
    static const char* s[] = { "noise", "tune", "eq", "comp", "deess", "sat", "exc", "dbl", "reverb", "delay", "stereo" };
    return s[m];
}
inline const char* modName (int m)
{
    static const char* s[] = { "NOISE RED.", "AUTO-TUNE", "EQ", "COMPRESSOR", "DE-ESSER", "SATURATION",
                               "EXCITER", "DOUBLER", "REVERB", "DELAY", "STEREO" };
    return s[m];
}
inline juce::String onId (int m) { return juce::String ("on_") + modId (m); }

inline juce::StringArray keyNames()   { return { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }; }
inline juce::StringArray scaleNames() { return { "Major", "Minor", "Chromatic" }; }
inline juce::StringArray delayDivNames() { return { "1/4", "1/8", "1/8 dotted", "1/16", "1/2" }; }
inline float delayDivBeats (int i) { static const float b[] = { 1.0f, 0.5f, 0.75f, 0.25f, 2.0f }; return b[juce::jlimit (0, 4, i)]; }

// Маска разрешённых нот (бит k = высота k, 0 = C) для тоники и лада
inline uint16_t makeScaleMask (int tonic, int scale)
{
    static const int major[] = { 0, 2, 4, 5, 7, 9, 11 };
    static const int minor[] = { 0, 2, 3, 5, 7, 8, 10 };
    if (scale >= 2) return 0x0FFF;
    uint16_t m = 0;
    for (int i = 0; i < 7; ++i)
        m |= (uint16_t) (1u << ((tonic + (scale == 0 ? major[i] : minor[i])) % 12));
    return m;
}
} // namespace vx
