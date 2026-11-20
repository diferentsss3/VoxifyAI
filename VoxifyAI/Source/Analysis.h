#pragma once
#include "Common.h"
#include "Params.h"
#include <atomic>

namespace vx
{
// Целевой спектральный наклон относительно полосы "body" (дБ).
// НЕ калиброван на реальных вокалах - это стартовые значения, их надо подстроить на слух/по данным.
// Порядок полос: low, mud, body, presence, sibilance, air
inline std::array<float, 6> defaultTargetTilt() { return { { -4.0f, 0.0f, 0.0f, -7.0f, -16.0f, -24.0f } }; }

//==============================================================================
// Детектор основного тона: нормированная автокорреляция на децимированном сигнале.
class PitchDetector
{
public:
    void prepare (double sr)
    {
        dec = juce::jmax (1, (int) std::round (sr / 12000.0));
        dsr = (float) (sr / dec);
        minLag = (int) (dsr / 800.0f);
        maxLag = (int) (dsr / 70.0f) + 1;
        frameLen = win + maxLag + 2;
        ring.assign (ringSize, 0.0f);
        frame.assign ((size_t) frameLen, 0.0f);
        corr.assign ((size_t) maxLag + 2, 0.0f);
        wp = hop = accN = 0;
        acc = 0.0f;
        hz = 0.0f; conf = 0.0f;
    }

    void push (float x)
    {
        acc += x;
        if (++accN >= dec)
        {
            ring[(size_t) wp] = acc / (float) dec;
            wp = (wp + 1) & (ringSize - 1);
            acc = 0.0f; accN = 0;
            if (++hop >= 128) { hop = 0; analyse(); }
        }
    }

    std::atomic<float> hz { 0.0f }, conf { 0.0f };

private:
    void analyse()
    {
        for (int i = 0; i < frameLen; ++i)
            frame[(size_t) i] = ring[(size_t) ((wp - frameLen + i) & (ringSize - 1))];

        float mean = 0.0f;
        for (float v : frame) mean += v;
        mean /= (float) frameLen;
        for (auto& v : frame) v -= mean;

        const float* a = frame.data() + maxLag;
        float e0 = 0.0f;
        for (int i = 0; i < win; ++i) e0 += a[i] * a[i];

        if (std::sqrt (e0 / (float) win) < 0.002f) { hz = 0.0f; conf = 0.0f; return; }

        float best = 0.0f;
        int bestLag = 0;
        for (int lag = minLag; lag <= maxLag; ++lag)
        {
            const float* b = frame.data() + maxLag - lag;
            float r = 0.0f, eb = 0.0f;
            for (int i = 0; i < win; ++i) { r += a[i] * b[i]; eb += b[i] * b[i]; }
            const float c = r / std::sqrt (e0 * eb + 1.0e-12f);
            corr[(size_t) lag] = c;
            if (c > best) { best = c; bestLag = lag; }
        }

        // Берём самый короткий лаг-пик, близкий к максимуму (защита от ошибок на октаву вниз)
        for (int lag = minLag + 1; lag < maxLag; ++lag)
        {
            const float c = corr[(size_t) lag];
            if (c >= 0.9f * best && c >= corr[(size_t) lag - 1] && c >= corr[(size_t) lag + 1]) { bestLag = lag; break; }
        }

        float delta = 0.0f;
        if (bestLag > minLag && bestLag < maxLag)
        {
            const float y0 = corr[(size_t) bestLag - 1], y1 = corr[(size_t) bestLag], y2 = corr[(size_t) bestLag + 1];
            const float den = y0 - 2.0f * y1 + y2;
            if (std::abs (den) > 1.0e-9f) delta = juce::jlimit (-0.5f, 0.5f, 0.5f * (y0 - y2) / den);
        }

        conf = best;
        hz = (best > 0.7f && bestLag > 0) ? dsr / ((float) bestLag + delta) : 0.0f;
    }

    static constexpr int ringSize = 2048;
    static constexpr int win = 512;
    int dec = 4, minLag = 15, maxLag = 172, frameLen = 686, wp = 0, hop = 0, accN = 0;
    float dsr = 12000.0f, acc = 0.0f;
    std::vector<float> ring, frame, corr;
};

//==============================================================================
// Определение тональности: гистограмма нот + профили Krumhansl-Schmuckler.
class KeyDetector
{
public:
    void reset() { chroma.fill (0.0f); }

    void addNote (int pitchClass, float weight = 1.0f)
    {
        for (auto& c : chroma) c *= 0.999f;
        chroma[(size_t) ((pitchClass % 12 + 12) % 12)] += weight;
    }

    struct Result { int tonic = 0; bool minor = false; float strength = 0.0f; };

    Result estimate() const
    {
        static const float maj[12] = { 6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f };
        static const float min[12] = { 6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f };
        Result best;
        float bestC = -2.0f;
        float total = 0.0f;
        for (float c : chroma) total += c;
        if (total < 5.0f) return best; // мало данных

        for (int t = 0; t < 12; ++t)
            for (int m = 0; m < 2; ++m)
            {
                float v[12];
                for (int i = 0; i < 12; ++i) v[i] = chroma[(size_t) ((t + i) % 12)];
                const float c = pearson (v, m ? min : maj);
                if (c > bestC) { bestC = c; best.tonic = t; best.minor = (m == 1); }
            }
        best.strength = juce::jlimit (0.0f, 1.0f, bestC);
        return best;
    }

private:
    static float pearson (const float* x, const float* y)
    {
        float mx = 0, my = 0;
        for (int i = 0; i < 12; ++i) { mx += x[i]; my += y[i]; }
        mx /= 12.0f; my /= 12.0f;
        float sxy = 0, sxx = 0, syy = 0;
        for (int i = 0; i < 12; ++i)
        {
            sxy += (x[i] - mx) * (y[i] - my);
            sxx += (x[i] - mx) * (x[i] - mx);
            syy += (y[i] - my) * (y[i] - my);
        }
        return sxy / std::sqrt (sxx * syy + 1.0e-12f);
    }

    std::array<float, 12> chroma {};
};

//==============================================================================
// Анализатор вокала: уровни, шум, спектральные полосы, высота, тональность.
// Вызывается из аудио-потока; результаты читаются из GUI через atomics.
class VoiceAnalyzer
{
public:
    VoiceAnalyzer() { for (auto& b : bandDb) b.store (-100.0f); }

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        pitch.prepare (sr);
        bands[0].setLowPass  (sr, 150.0f, 0.707f);
        bands[1].setBandPass (sr, 300.0f, 1.0f);
        bands[2].setBandPass (sr, 700.0f, 0.7f);
        bands[3].setBandPass (sr, 3500.0f, 0.7f);
        bands[4].setBandPass (sr, 7000.0f, 1.0f);
        bands[5].setHighPass (sr, 10000.0f, 0.707f);
        reset();
    }

    void reset()
    {
        for (auto& b : bands) b.reset();
        bandPow.fill (0.0f);
        levelPow = 0.0f;
        peakDbV = -100.0f;
        nfInit = false;
        voicedTime = 0.0f;
        keyDet.reset();
        levelDb = -100.0f; peakDb = -100.0f; noiseFloorDb = -100.0f; pitchErrCents = 0.0f;
        voicedSeconds = 0.0f; keyStrength = 0.0f; pitchHz = 0.0f;
        for (auto& b : bandDb) b.store (-100.0f);
    }

    void setScaleMask (uint16_t m) { mask = m; }

    void process (const float* mono, int n)
    {
        if (n <= 0) return;
        double sum = 0.0;
        float pk = 0.0f;
        std::array<double, 6> pw {};
        for (int i = 0; i < n; ++i)
        {
            const float x = mono[i];
            sum += (double) x * x;
            pk = std::max (pk, std::abs (x));
            pitch.push (x);
            for (size_t b = 0; b < 6; ++b) { const float y = bands[b].process (x); pw[b] += (double) y * y; }
        }

        const float blockPow = (float) (sum / n);
        const float blockDb = juce::jmax (-90.0f, 10.0f * std::log10 (blockPow + 1.0e-12f));
        const float dt = (float) n / (float) sr;

        if (! nfInit) { nfDb = blockDb; nfInit = true; }
        if (blockDb < nfDb) nfDb = blockDb; else nfDb += 0.0005f * (blockDb - nfDb);
        noiseFloorDb = nfDb;

        if (blockDb > -55.0f)
        {
            voicedTime += dt;
            const float k = juce::jmin (1.0f, dt / 3.0f);
            levelPow += k * (blockPow - levelPow);
            for (size_t b = 0; b < 6; ++b)
            {
                const float p = (float) (pw[b] / n);
                bandPow[b] += k * (p - bandPow[b]);
                bandDb[b] = juce::jmax (-100.0f, 10.0f * std::log10 (bandPow[b] + 1.0e-12f));
            }
            peakDbV = std::max (linToDb (pk), peakDbV - 1.0f * dt);
            levelDb = 10.0f * std::log10 (levelPow + 1.0e-12f);
            peakDb = peakDbV;
            voicedSeconds = voicedTime;

            const float hz = pitch.hz;
            pitchHz = hz;
            if (hz > 0.0f)
            {
                const float midi = 69.0f + 12.0f * std::log2 (hz / 440.0f);
                keyDet.addNote ((int) std::lround (midi));
                const auto kr = keyDet.estimate();
                keyTonic = kr.tonic; keyMinor = kr.minor; keyStrength = kr.strength;

                // Отклонение от ближайшей разрешённой ноты (в центах)
                const int r = (int) std::lround (midi);
                float best = 1.0e9f;
                for (int d = -6; d <= 6; ++d)
                {
                    const int nn = r + d;
                    if ((mask >> (((nn % 12) + 12) % 12)) & 1)
                        best = std::min (best, std::abs ((float) nn - midi));
                }
                if (best < 1.0e8f)
                {
                    const float cur = pitchErrCents.load();
                    pitchErrCents.store (cur + 0.05f * (best * 100.0f - cur));
                }
            }
        }
        else
        {
            pitchHz = 0.0f;
        }
    }

    // Полосы относительно "body" (дБ)
    std::array<float, 6> relBands() const
    {
        std::array<float, 6> r {};
        const float body = bandDb[2];
        for (size_t i = 0; i < 6; ++i) r[i] = bandDb[i] - body;
        return r;
    }

    std::atomic<float> pitchHz { 0.0f }, levelDb { -100.0f }, peakDb { -100.0f }, noiseFloorDb { -100.0f },
                       pitchErrCents { 0.0f }, voicedSeconds { 0.0f }, keyStrength { 0.0f };
    std::atomic<int>   keyTonic { 0 };
    std::atomic<bool>  keyMinor { false };
    std::array<std::atomic<float>, 6> bandDb;

private:
    double sr = 48000.0;
    PitchDetector pitch;
    KeyDetector keyDet;
    std::array<Biquad, 6> bands;
    std::array<float, 6> bandPow {};
    float levelPow = 0.0f, peakDbV = -100.0f, nfDb = -90.0f, voicedTime = 0.0f;
    bool nfInit = false;
    uint16_t mask = 0x0FFF;
};

//==============================================================================
// "AI Vocal Coach": текстовые подсказки на основе анализа (эвристики, не нейросеть).
inline juce::StringArray makeCoachReport (const VoiceAnalyzer& a, const std::array<float, 6>& targetTilt,
                                          bool beatPresent, float vocalMinusBeatDb)
{
    juce::StringArray out;
    if (a.voicedSeconds.load() < 2.0f)
    {
        out.add (U ("Нет данных: проиграйте вокал 5-10 секунд, затем нажмите ANALYZE VOCAL."));
        return out;
    }

    const float peak = a.peakDb, level = a.levelDb, nf = a.noiseFloorDb;
    const auto rel = a.relBands();

    if (peak > -1.0f)  out.add (U ("Пики у 0 дБ: снизьте входной уровень (Input), иначе возможен клиппинг."));
    if (level < -32.0f) out.add (U ("Запись очень тихая (RMS ") + juce::String (level, 1) + U (" дБ): поднимите Input или Smart Gain."));
    if (nf > -55.0f)   out.add (U ("Фоновый шум около ") + juce::String (nf, 0) + U (" дБ: усильте Noise Reduction."));
    if (peak - level > 15.0f) out.add (U ("Большая разница пик/RMS (") + juce::String (peak - level, 0) + U (" дБ): добавьте компрессии."));
    if (rel[1] - targetTilt[1] > 3.0f) out.add (U ("Много 'коробки' 200-400 Гц: усильте срез мутности в EQ."));
    if (rel[4] - targetTilt[4] > 3.0f) out.add (U ("Резкие С/Ш: увеличьте De-Esser."));
    if (rel[5] - targetTilt[5] < -6.0f) out.add (U ("Не хватает воздуха выше 10 кГц: добавьте Exciter / Air."));
    if (rel[3] - targetTilt[3] < -4.0f) out.add (U ("Вокалу не хватает разборчивости (2-5 кГц): поднимите Presence в EQ."));
    if (a.pitchErrCents > 35.0f) out.add (U ("Среднее отклонение от строя ~") + juce::String (a.pitchErrCents, 0) + U (" центов: усильте Auto-Tune или проверьте тональность."));

    if (beatPresent)
    {
        if (vocalMinusBeatDb < -1.0f) out.add (U ("Вокал тише бита на ") + juce::String (-vocalMinusBeatDb, 1) + U (" дБ (ориентир: от -1 до +4 дБ над RMS бита)."));
        else if (vocalMinusBeatDb > 4.0f) out.add (U ("Вокал громче бита на ") + juce::String (vocalMinusBeatDb, 1) + U (" дБ - возможно, слишком."));
        else out.add (U ("Баланс вокал/бит в пределах ориентира."));
    }

    if (out.isEmpty()) out.add (U ("Явных проблем не найдено. Настройте стиль и количество эффектов на слух."));
    return out;
}
} // namespace vx
