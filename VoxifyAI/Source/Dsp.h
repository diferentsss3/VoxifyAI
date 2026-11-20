#pragma once
#include "Common.h"
#include "Params.h"

namespace vx
{
struct ChainParams
{
    std::array<bool, NumMods>  on {};
    std::array<float, NumMods> amt {};   // 0..1 (уже с учётом AI MIX)
    float tuneSpeed = 0.7f;              // 0..1
    float mudDb = 0, presDb = 0, airDb = 0, deessHz = 6800, hpHz = 80;
    uint16_t scaleMask = 0x0FFF;
    bool smartGain = false, adlib = false, bypass = false;
    float inGainDb = 0, outGainDb = 0, bpm = 120;
    int delayDiv = 1;
    float pitchHz = 0;                   // текущая высота из анализатора (0 = безголосо)
};

// Однополюсный сглаживатель
struct Sm
{
    float v = 0, t = 0, c = 0.003f;
    float next() { v += (t - v) * c; return v; }
};

//==============================================================================
class VocalChain
{
public:
    void prepare (double sampleRate, int maxBlock)
    {
        sr = sampleRate;
        W = 2.0f * std::round (0.016f * (float) sr);
        laSamples = (int) std::round (0.0015 * sr);
        latency = (int) (W * 0.5f) + laSamples;

        for (int c = 0; c < 2; ++c)
        {
            shifter[c].prepare ((int) W + 8);
            dryBuf[c].prepare (latency + 8);
            laBuf[c].prepare (laSamples + 8);
            dlyBuf[c].prepare ((int) (4.0 * sr));
        }
        preDelay.prepare ((int) (0.2 * sr));
        dblBuf.prepare ((int) (0.08 * sr));

        rvL.assign ((size_t) maxBlock, 0.0f);
        rvR.assign ((size_t) maxBlock, 0.0f);
        byL.assign ((size_t) maxBlock, 0.0f);
        byR.assign ((size_t) maxBlock, 0.0f);

        reverb.setSampleRate (sr);
        juce::Reverb::Parameters rp;
        rp.roomSize = 0.62f; rp.damping = 0.55f; rp.wetLevel = 0.33f; rp.dryLevel = 0.0f; rp.width = 1.0f; rp.freezeMode = 0.0f;
        reverb.setParameters (rp);

        const float sc = 1.0f - std::exp (-1.0f / (0.010f * (float) sr));
        for (auto& s : eff) s.c = sc;
        for (auto& s : act) s.c = sc;
        inG.c = outG.c = bypS.c = sgOn.c = tuneMix.c = sc;
        bypS.c = 1.0f - std::exp (-1.0f / (0.004f * (float) sr));

        for (int c = 0; c < 2; ++c)
        {
            exHp[c].setHighPass (sr, 4500.0f, 0.707f);
            exHp2[c].setHighPass (sr, 5000.0f, 0.707f);
            rvHp.setHighPass (sr, 250.0f, 0.707f);
            rvLp.setLowPass (sr, 9000.0f, 0.707f);
            dlyLp[c].setLowPass (sr, 4500.0f, 0.707f);
            dlyHp[c].setHighPass (sr, 250.0f, 0.707f);
        }
        sideHp.setHighPass (sr, 150.0f, 0.707f);

        atkNr = timeCoef (3.0f, sr);  relNr = timeCoef (120.0f, sr);
        atkEnvNr = timeCoef (1.0f, sr); relEnvNr = timeCoef (80.0f, sr);
        atkDs = timeCoef (0.3f, sr);  relDs = timeCoef (25.0f, sr);
        atkC = timeCoef (8.0f, sr);   relC = timeCoef (90.0f, sr);
        limAtk = 1.0f - std::exp (-1.0f / (0.0004f * (float) sr));
        limRel = 1.0f - std::exp (-1.0f / (0.08f * (float) sr));
        reset();
    }

    void reset()
    {
        for (int c = 0; c < 2; ++c)
        {
            shifter[c].clear(); dryBuf[c].clear(); laBuf[c].clear(); dlyBuf[c].clear();
            eqHp[c].reset(); eqMud[c].reset(); eqPres[c].reset(); eqAir[c].reset();
            dsBp[c].reset(); exHp[c].reset(); exHp2[c].reset(); dlyLp[c].reset(); dlyHp[c].reset();
            dcX[c] = dcY[c] = 0.0f;
        }
        preDelay.clear(); dblBuf.clear(); reverb.reset();
        rvHp.reset(); rvLp.reset(); sideHp.reset();
        phase = 0.0f; ratio = 1.0f;
        nrEnv = 0.0f; nrG = 1.0f; dsEnv = 0.0f; compGr = 0.0f; limG = 1.0f; duckEnv = 0.0f;
        sgPow = 0.0f; sgDb = 0.0f; sgTarget = 0.0f;
        dblPh1 = 0.0f; dblPh2 = 0.25f; dTime = 0.0f;
        for (auto& s : eff) s.v = 0.0f;
        for (auto& s : act) s.v = 0.0f;
        inG.v = outG.v = 1.0f; bypS.v = 0.0f; sgOn.v = 0.0f; tuneMix.v = 0.0f;
    }

    int getLatency() const { return latency; }
    float getCompGrDb() const { return compGr; }

    void process (float* l, float* r, int n, const ChainParams& p)
    {
        n = juce::jmin (n, (int) rvL.size());
        const float fs = (float) sr;

        //---------------- блочные настройки ----------------
        for (int m = 0; m < NumMods; ++m)
        {
            eff[m].t = p.on[(size_t) m] ? p.amt[(size_t) m] : 0.0f;
            act[m].t = p.on[(size_t) m] ? 1.0f : 0.0f;
        }
        inG.t  = dbToLin (p.inGainDb);
        outG.t = dbToLin (p.outGainDb);
        bypS.t = p.bypass ? 1.0f : 0.0f;
        sgOn.t = p.smartGain ? 1.0f : 0.0f;

        // EQ
        const float eqA = p.amt[EQ];
        const float hp  = p.adlib ? std::max (p.hpHz, 300.0f) : p.hpHz;
        for (int c = 0; c < 2; ++c)
        {
            eqHp[c].setHighPass (sr, hp, 0.707f);
            eqMud[c].setPeak (sr, 320.0f, 1.0f, p.mudDb * eqA);
            eqPres[c].setPeak (sr, 3200.0f, 0.9f, p.presDb * eqA);
            eqAir[c].setHighShelf (sr, 11000.0f, p.airDb * eqA);
            dsBp[c].setBandPass (sr, p.deessHz, 1.2f);
        }

        // Компрессор
        const float ca = p.amt[Comp];
        compThr = -10.0f - 18.0f * ca;
        compRatio = 1.5f + 4.5f * ca;
        compMakeup = -compThr * (1.0f - 1.0f / compRatio) * 0.4f;
        act[Comp].t = p.on[Comp] ? std::min (1.0f, ca * 5.0f) : 0.0f;
        act[DeEss].t = p.on[DeEss] ? std::min (1.0f, p.amt[DeEss] * 5.0f) : 0.0f;
        dsThr = -18.0f - 10.0f * p.amt[DeEss];

        // Auto-Tune: целевое отношение частот
        bool voiced = false;
        float ratioTarget = 1.0f;
        const float tuneAmt = p.amt[Tune];
        if (p.on[Tune] && p.pitchHz > 0.0f && tuneAmt > 0.01f)
        {
            const float midi = 69.0f + 12.0f * std::log2 (p.pitchHz / 440.0f);
            const int rr = (int) std::lround (midi);
            float best = 1.0e9f; int bestN = rr;
            for (int k = -6; k <= 6; ++k)
            {
                const int nn = rr + k;
                if ((p.scaleMask >> (((nn % 12) + 12) % 12)) & 1)
                {
                    const float d = std::abs ((float) nn - midi);
                    if (d < best) { best = d; bestN = nn; }
                }
            }
            ratioTarget = std::pow (2.0f, ((float) bestN - midi) * tuneAmt / 12.0f);
            voiced = true;
        }
        tuneMix.t = voiced ? 1.0f : 0.0f;
        const float sp = 1.0f - p.tuneSpeed;
        const float tuneTau = 0.003f + 0.2f * sp * sp;
        const float tuneCoef = 1.0f - std::exp (-1.0f / (tuneTau * fs));

        // Delay
        const float beatSec = 60.0f / juce::jlimit (40.0f, 300.0f, p.bpm);
        const float dTarget = juce::jlimit (64.0f, (float) (3.9 * sr), beatSec * delayDivBeats (p.delayDiv) * fs);
        if (dTime <= 0.0f) dTime = dTarget;

        const float satAmtB = eff[Sat].t;
        juce::ignoreUnused (satAmtB);

        //---------------- цикл 1: до отправок ----------------
        for (int i = 0; i < n; ++i)
        {
            float xl = l[i], xr = r[i];

            // сухой сигнал для Before/After (с компенсацией задержки)
            dryBuf[0].write (xl); dryBuf[1].write (xr);
            byL[(size_t) i] = dryBuf[0].read ((float) latency);
            byR[(size_t) i] = dryBuf[1].read ((float) latency);

            const float g = inG.next();
            xl *= g; xr *= g;

            // Smart Gain (медленный райдер громкости к ~ -18 dBFS RMS)
            {
                const float pw = 0.5f * (xl * xl + xr * xr);
                sgPow += (pw - sgPow) * 0.0001f;
                const float lv = 10.0f * std::log10 (sgPow + 1.0e-12f);
                if (lv > -50.0f) sgTarget = juce::jlimit (-9.0f, 9.0f, -18.0f - lv);
                sgDb += (sgTarget - sgDb) * 0.00003f;
                const float so = sgOn.next();
                const float sg = dbToLin (sgDb * so);
                xl *= sg; xr *= sg;
            }

            // Noise Reduction (даунвард-экспандер)
            {
                const float e = eff[Noise].next();
                const float lvl = std::max (std::abs (xl), std::abs (xr));
                nrEnv = lvl > nrEnv ? lvl + atkEnvNr * (nrEnv - lvl) : lvl + relEnvNr * (nrEnv - lvl);
                const float thr = -80.0f + 40.0f * e;
                const float red = juce::jlimit (0.0f, 20.0f, thr - linToDb (nrEnv));
                const float tg = dbToLin (-red);
                nrG = tg < nrG ? tg + atkNr * (nrG - tg) : tg + relNr * (nrG - tg);
                xl *= nrG; xr *= nrG;
            }

            // Auto-Tune (двухтактный pitch shifter)
            {
                ratio += (ratioTarget - ratio) * tuneCoef;
                phase += (1.0f - ratio) / W;
                phase -= std::floor (phase);
                float p2 = phase + 0.5f; if (p2 >= 1.0f) p2 -= 1.0f;
                const float w1 = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * phase);
                const float w2 = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * p2);
                const float d1 = phase * W, d2 = p2 * W;
                shifter[0].write (xl); shifter[1].write (xr);
                const float s0 = w1 * shifter[0].read (d1) + w2 * shifter[0].read (d2);
                const float s1 = w1 * shifter[1].read (d1) + w2 * shifter[1].read (d2);
                const float y0 = shifter[0].read (W * 0.5f), y1 = shifter[1].read (W * 0.5f);
                const float tm = tuneMix.next();
                xl = y0 + tm * (s0 - y0);
                xr = y1 + tm * (s1 - y1);
            }
            eff[Tune].next(); act[Tune].next();

            // EQ
            {
                const float m = act[EQ].next();
                float yl = eqHp[0].process (xl), yr = eqHp[1].process (xr);
                yl = eqMud[0].process (yl);  yr = eqMud[1].process (yr);
                yl = eqPres[0].process (yl); yr = eqPres[1].process (yr);
                yl = eqAir[0].process (yl);  yr = eqAir[1].process (yr);
                xl += m * (yl - xl); xr += m * (yr - xr);
                eff[EQ].next();
            }

            // Compressor
            {
                const float m = act[Comp].next();
                eff[Comp].next();
                const float lvl = linToDb (std::max (std::abs (xl), std::abs (xr)));
                const float over = lvl - compThr;
                const float knee = 6.0f;
                float grT = 0.0f;
                const float slope = 1.0f - 1.0f / compRatio;
                if (2.0f * over > knee)               grT = over * slope;
                else if (2.0f * over > -knee)         grT = slope * (over + knee * 0.5f) * (over + knee * 0.5f) / (2.0f * knee);
                compGr = grT > compGr ? grT + atkC * (compGr - grT) : grT + relC * (compGr - grT);
                const float gc = dbToLin (-compGr + compMakeup);
                const float gm = 1.0f + m * (gc - 1.0f);
                xl *= gm; xr *= gm;
            }

            // De-Esser (вычитание сжатой полосы сибилянтов)
            {
                const float m = act[DeEss].next();
                eff[DeEss].next();
                const float bl = dsBp[0].process (xl), br = dsBp[1].process (xr);
                const float lvl = std::max (std::abs (bl), std::abs (br));
                dsEnv = lvl > dsEnv ? lvl + atkDs * (dsEnv - lvl) : lvl + relDs * (dsEnv - lvl);
                const float red = juce::jlimit (0.0f, 12.0f, (linToDb (dsEnv) - dsThr) * 0.7f) * m;
                const float k = 1.0f - dbToLin (-red);
                xl -= bl * k; xr -= br * k;
            }

            // Saturation
            {
                const float a = eff[Sat].next();
                act[Sat].next();
                if (a > 1.0e-4f)
                {
                    const float d = 1.0f + 5.0f * a;
                    const float b = 0.12f * a;
                    const float tb = std::tanh (b);
                    const float nrm = 1.0f / std::sqrt (d);
                    const float mix = std::min (1.0f, a * 1.5f);
                    float in[2] = { xl, xr };
                    for (int c = 0; c < 2; ++c)
                    {
                        const float s = (std::tanh (d * in[c] + b) - tb) * nrm;
                        const float y = in[c] + mix * (s - in[c]);
                        const float o = y - dcX[c] + 0.995f * dcY[c];
                        dcX[c] = y; dcY[c] = o;
                        in[c] = o;
                    }
                    xl = in[0]; xr = in[1];
                }
            }

            // Exciter
            {
                const float a = eff[Exc].next();
                act[Exc].next();
                if (a > 1.0e-4f)
                {
                    const float hl = exHp[0].process (xl), hr = exHp[1].process (xr);
                    const float el = exHp2[0].process (std::tanh (4.0f * hl));
                    const float er = exHp2[1].process (std::tanh (4.0f * hr));
                    xl += 0.6f * a * el; xr += 0.6f * a * er;
                }
            }

            // Vocal Doubler (две модулированные копии)
            {
                const float a = eff[Dbl].next();
                act[Dbl].next();
                const float mono = 0.5f * (xl + xr);
                dblBuf.write (mono);
                dblPh1 += 0.31f / fs; dblPh1 -= std::floor (dblPh1);
                dblPh2 += 0.47f / fs; dblPh2 -= std::floor (dblPh2);
                if (a > 1.0e-4f)
                {
                    const float dA = 0.018f * fs + std::sin (juce::MathConstants<float>::twoPi * dblPh1) * 0.0012f * fs;
                    const float dB = 0.027f * fs + std::sin (juce::MathConstants<float>::twoPi * dblPh2) * 0.0014f * fs;
                    const float cA = dblBuf.read (dA), cB = dblBuf.read (dB);
                    const float gd = 0.55f * a;
                    xl += gd * (0.9f * cA + 0.3f * cB);
                    xr += gd * (0.9f * cB + 0.3f * cA);
                }
            }

            l[i] = xl; r[i] = xr;

            // Отправка в Reverb: pre-delay 25 мс + HP/LP
            const float send = 0.5f * (xl + xr);
            preDelay.write (send);
            const float ps = rvLp.process (rvHp.process (preDelay.read (0.025f * fs)));
            rvL[(size_t) i] = ps;
            rvR[(size_t) i] = ps;
        }

        reverb.processStereo (rvL.data(), rvR.data(), n);

        //---------------- цикл 2: пространство, ширина, лимитер ----------------
        const float rvGain = 0.55f * (p.adlib ? 1.5f : 1.0f);
        const float dlGain = 0.6f * (p.adlib ? 1.4f : 1.0f);
        const float fb = 0.38f;
        const float dtCoef = 0.0005f;
        const float duckA = timeCoef (5.0f, sr), duckR = timeCoef (150.0f, sr);
        const float ceiling = dbToLin (-1.0f);

        for (int i = 0; i < n; ++i)
        {
            float xl = l[i], xr = r[i];
            const float mono = 0.5f * (xl + xr);

            // Reverb с даккингом (хвост опускается, пока вокал звучит)
            const float am = std::abs (mono);
            duckEnv = am > duckEnv ? am + duckA * (duckEnv - am) : am + duckR * (duckEnv - am);
            const float duck = 1.0f - 0.6f * std::min (1.0f, duckEnv * 6.0f);
            const float rv = eff[Reverb].next() * rvGain * duck;
            act[Reverb].next();
            xl += rvL[(size_t) i] * rv;
            xr += rvR[(size_t) i] * rv;

            // Ping-pong Delay (синхронизация с темпом хоста)
            {
                const float a = eff[Delay].next() * dlGain;
                act[Delay].next();
                dTime += (dTarget - dTime) * dtCoef;
                const float eL = dlyBuf[0].read (dTime), eR = dlyBuf[1].read (dTime);
                dlyBuf[0].write (mono + fb * dlyLp[0].process (eR));
                dlyBuf[1].write (eL);
                xl += a * dlyHp[0].process (eL);
                xr += a * dlyHp[1].process (eR);
            }

            // Stereo Width (M/S, бас остаётся в моно)
            {
                const float a = eff[Stereo].next();
                act[Stereo].next();
                const float M = 0.5f * (xl + xr);
                const float S = 0.5f * (xl - xr);
                const float Sh = sideHp.process (S);
                const float w = 1.0f + 1.3f * a + (p.adlib ? 0.4f : 0.0f);
                const float mix = std::min (1.0f, a * 5.0f);
                const float S2 = S + mix * (Sh * w - S);
                xl = M + S2; xr = M - S2;
            }

            // Output gain + лимитер с look-ahead
            const float og = outG.next();
            xl *= og; xr *= og;
            const float pk = std::max (std::abs (xl), std::abs (xr));
            const float tg = pk > ceiling ? ceiling / pk : 1.0f;
            if (tg < limG) limG += (tg - limG) * limAtk;
            else           limG += (1.0f - limG) * limRel;
            laBuf[0].write (xl); laBuf[1].write (xr);
            float ol = laBuf[0].read ((float) laSamples) * limG;
            float orr = laBuf[1].read ((float) laSamples) * limG;
            ol = juce::jlimit (-0.999f, 0.999f, ol);
            orr = juce::jlimit (-0.999f, 0.999f, orr);

            // Before/After
            const float b = bypS.next();
            l[i] = ol + b * (byL[(size_t) i] - ol);
            r[i] = orr + b * (byR[(size_t) i] - orr);
        }
    }

private:
    double sr = 48000.0;
    float W = 1536.0f;
    int laSamples = 72, latency = 840;

    DelayBuffer shifter[2], dryBuf[2], laBuf[2], dlyBuf[2], preDelay, dblBuf;
    Biquad eqHp[2], eqMud[2], eqPres[2], eqAir[2], dsBp[2], exHp[2], exHp2[2], dlyLp[2], dlyHp[2];
    Biquad rvHp, rvLp, sideHp;
    juce::Reverb reverb;

    std::vector<float> rvL, rvR, byL, byR;
    Sm eff[NumMods], act[NumMods], inG, outG, bypS, sgOn, tuneMix;

    float phase = 0, ratio = 1;
    float nrEnv = 0, nrG = 1, atkNr = 0, relNr = 0, atkEnvNr = 0, relEnvNr = 0;
    float dsEnv = 0, atkDs = 0, relDs = 0, dsThr = -24;
    float compGr = 0, compThr = -20, compRatio = 3, compMakeup = 0, atkC = 0, relC = 0;
    float limG = 1, limAtk = 0.05f, limRel = 0.0003f, duckEnv = 0;
    float sgPow = 0, sgDb = 0, sgTarget = 0;
    float dblPh1 = 0, dblPh2 = 0.25f, dTime = 0;
    float dcX[2] = { 0, 0 }, dcY[2] = { 0, 0 };
};
} // namespace vx
