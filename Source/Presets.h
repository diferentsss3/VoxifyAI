#pragma once
#include "Params.h"

namespace vx
{
// ВАЖНО: это стартовые точки "в духе" стиля, подобранные вручную. Они НЕ сняты
// с реальных цепочек артистов. Подстраивайте по слуху.
struct StylePreset
{
    const char* name;
    // Порядок как в enum Mod: noise, tune, eq, comp, deess, sat, exc, dbl, reverb, delay, stereo (0..100)
    float amt[NumMods];
    float tuneSpeed;  // 0..100 (100 = жёсткий "робот")
    float mudDb;      // -8..0
    float presDb;     // -2..8
    float airDb;      // 0..8
    float deessHz;    // 4000..9500
    float hpHz;       // 40..200
    int   delayDiv;   // индекс в delayDivNames
};

inline const StylePreset* stylePresets (int& count)
{
    static const StylePreset p[] = {
        //  name              noi tun  eq cmp des sat exc dbl rev dly str   spd  mud  pres air  deHz  hp div
        { "Clean Vocal",    { 40, 30, 50, 55, 50, 20, 25, 15, 30, 20, 40 },  40, -2.0f, 2.0f, 2.0f, 6800,  80, 1 },
        { "Madk1d",         { 40, 85, 65, 70, 55, 55, 60, 55, 40, 45, 65 },  95, -2.5f, 3.0f, 4.0f, 7200, 100, 3 },
        { "Villian",        { 45, 70, 55, 75, 60, 55, 30, 45, 55, 40, 55 },  80, -3.0f, 1.5f, 1.0f, 6500,  90, 2 },
        { "Morgenshtern",   { 40, 60, 60, 80, 60, 70, 45, 50, 30, 30, 50 },  75, -2.0f, 3.5f, 2.0f, 6500,  90, 1 },
        { "9mice",          { 40, 75, 55, 60, 55, 35, 55, 55, 60, 50, 70 },  85, -2.0f, 2.0f, 4.5f, 7500, 110, 2 },
        { "\xD0\x9F\xD0\xBB\xD0\xB0\xD1\x82\xD0\xB8\xD0\xBD\xD0\xB0", // "Платина"
                            { 40, 65, 60, 65, 65, 30, 50, 40, 45, 30, 55 },  70, -2.5f, 2.5f, 3.0f, 7000,  90, 1 },
        { "Og Buda",        { 45, 80, 45, 70, 45, 70, 20, 40, 40, 55, 45 },  90, -1.5f, 1.0f, 0.5f, 6000,  90, 0 },
    };
    count = (int) (sizeof (p) / sizeof (p[0]));
    return p;
}

inline juce::StringArray styleNames()
{
    int n = 0;
    auto* p = stylePresets (n);
    juce::StringArray s;
    for (int i = 0; i < n; ++i) s.add (U (p[i].name));
    return s;
}
} // namespace vx
