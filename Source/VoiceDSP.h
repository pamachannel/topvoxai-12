#pragma once

// Moteur audio de TOPVOX VOICE : C++ pur, sans dépendance, temps réel
// (aucune allocation ni verrou dans process()).
//
// Chaîne : gain entrée -> filtre grave -> gate -> de-esser -> EQ 4 bandes
//          -> compresseur -> saturation -> reverb + delay -> largeur
//          -> gain sortie -> limiteur.

#include "ParamSpec.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cmath>
#include <initializer_list>
#include <vector>

namespace topvox
{
constexpr double kPi = 3.14159265358979323846;

inline float dbToLin (float db)  { return std::pow (10.0f, db * 0.05f); }
inline float linToDb (float x)   { return 20.0f * std::log10 (std::max (x, 1.0e-9f)); }
inline float coefFromMs (float fs, float ms) { return std::exp (-1.0f / (0.001f * ms * fs)); }
inline float clampf (float x, float lo, float hi) { return std::min (std::max (x, lo), hi); }

// ---------------------------------------------------------------- Paramètres
struct Params
{
    float v[kNumParams];
    Params() { for (int i = 0; i < kNumParams; ++i) v[i] = spec (i).def; }
    float& operator[] (int i)       { return v[i]; }
    float  operator[] (int i) const { return v[i]; }
};

// ------------------------------------------------------------------- Biquad
struct Biquad
{
    struct C { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };

    C c;
    double z1 = 0, z2 = 0;

    void set (const C& n) { c = n; }
    void reset()          { z1 = z2 = 0; }

    inline float process (float x)
    {
        const double xi = x;
        const double y  = c.b0 * xi + z1;
        z1 = c.b1 * xi - c.a1 * y + z2;
        z2 = c.b2 * xi - c.a2 * y;
        return (float) y;
    }

    static C make (double b0, double b1, double b2, double a0, double a1, double a2)
    {
        C r;
        r.b0 = b0 / a0; r.b1 = b1 / a0; r.b2 = b2 / a0; r.a1 = a1 / a0; r.a2 = a2 / a0;
        return r;
    }

    static C highPass (double fs, double f, double q)
    {
        f = std::min (f, fs * 0.45);
        const double w = 2.0 * kPi * f / fs, cw = std::cos (w), sw = std::sin (w), al = sw / (2.0 * q);
        return make ((1 + cw) / 2, -(1 + cw), (1 + cw) / 2, 1 + al, -2 * cw, 1 - al);
    }
    static C lowPass (double fs, double f, double q)
    {
        f = std::min (f, fs * 0.45);
        const double w = 2.0 * kPi * f / fs, cw = std::cos (w), sw = std::sin (w), al = sw / (2.0 * q);
        return make ((1 - cw) / 2, 1 - cw, (1 - cw) / 2, 1 + al, -2 * cw, 1 - al);
    }
    static C bandPass (double fs, double f, double q)
    {
        f = std::min (f, fs * 0.45);
        const double w = 2.0 * kPi * f / fs, cw = std::cos (w), sw = std::sin (w), al = sw / (2.0 * q);
        return make (al, 0, -al, 1 + al, -2 * cw, 1 - al);
    }
    static C peak (double fs, double f, double q, double gainDb)
    {
        f = std::min (f, fs * 0.45);
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w = 2.0 * kPi * f / fs, cw = std::cos (w), sw = std::sin (w), al = sw / (2.0 * q);
        return make (1 + al * A, -2 * cw, 1 - al * A, 1 + al / A, -2 * cw, 1 - al / A);
    }
    static C lowShelf (double fs, double f, double gainDb)
    {
        f = std::min (f, fs * 0.45);
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w = 2.0 * kPi * f / fs, cw = std::cos (w), sw = std::sin (w);
        const double beta = std::sqrt (2.0 * A) * sw;
        return make (A * ((A + 1) - (A - 1) * cw + beta),
                     2 * A * ((A - 1) - (A + 1) * cw),
                     A * ((A + 1) - (A - 1) * cw - beta),
                     (A + 1) + (A - 1) * cw + beta,
                     -2 * ((A - 1) + (A + 1) * cw),
                     (A + 1) + (A - 1) * cw - beta);
    }
    static C highShelf (double fs, double f, double gainDb)
    {
        f = std::min (f, fs * 0.45);
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w = 2.0 * kPi * f / fs, cw = std::cos (w), sw = std::sin (w);
        const double beta = std::sqrt (2.0 * A) * sw;
        return make (A * ((A + 1) + (A - 1) * cw + beta),
                     -2 * A * ((A - 1) + (A + 1) * cw),
                     A * ((A + 1) + (A - 1) * cw - beta),
                     (A + 1) - (A - 1) * cw + beta,
                     2 * ((A - 1) - (A + 1) * cw),
                     (A + 1) - (A - 1) * cw - beta);
    }
};

// --------------------------------------------------------------------- Gate
struct Gate
{
    float fs = 44100.f, env = 0.f, gain = 1.f;
    int holdCount = 0;
    bool open = false;

    void reset() { env = 0.f; gain = 1.f; holdCount = 0; open = false; }

    void process (float* l, float* r, int n, float threshDb)
    {
        if (threshDb <= -79.5f) { gain = 1.f; open = true; return; }   // gate coupé

        const float thrOpen  = dbToLin (threshDb);
        const float thrClose = dbToLin (threshDb - 4.f);               // hystérésis
        const float envRel   = coefFromMs (fs, 30.f);
        const float gAtt     = coefFromMs (fs, 1.5f);
        const float gRel     = coefFromMs (fs, 80.f);
        const float floorG   = dbToLin (-40.f);
        const int   holdSamp = (int) (0.06f * fs);

        for (int i = 0; i < n; ++i)
        {
            const float a = std::max (std::fabs (l[i]), std::fabs (r[i]));
            env = std::max (a, env * envRel);

            if (! open)
            {
                if (env > thrOpen) { open = true; holdCount = holdSamp; }
            }
            else
            {
                if (env > thrClose) holdCount = holdSamp;
                else if (--holdCount <= 0) open = false;
            }

            const float target = open ? 1.f : floorG;
            const float c = target > gain ? gAtt : gRel;
            gain = c * gain + (1.f - c) * target;
            l[i] *= gain;
            r[i] *= gain;
        }
    }
};

// ----------------------------------------------------------------- De-esser
struct DeEsser
{
    float fs = 44100.f, env = 0.f, grDb = 0.f, lastFreq = -1.f;
    Biquad bpL, bpR;

    void reset() { env = 0.f; grDb = 0.f; bpL.reset(); bpR.reset(); lastFreq = -1.f; }

    void process (float* l, float* r, int n, float amount, float freq)
    {
        grDb = 0.f;
        if (amount < 0.01f) return;

        if (freq != lastFreq)
        {
            const auto c = Biquad::bandPass (fs, freq, 1.4);
            bpL.set (c); bpR.set (c);
            lastFreq = freq;
        }

        const float attC  = coefFromMs (fs, 0.4f);
        const float relC  = coefFromMs (fs, 25.f);
        const float thrDb = -16.f - 24.f * amount;
        const float slope = 1.f - 1.f / 4.f;     // ratio 4:1 sur la bande
        float maxGr = 0.f;

        for (int i = 0; i < n; ++i)
        {
            const float bl = bpL.process (l[i]);
            const float br = bpR.process (r[i]);
            const float a  = std::max (std::fabs (bl), std::fabs (br));
            env = (a > env) ? attC * env + (1.f - attC) * a
                            : relC * env + (1.f - relC) * a;

            const float eDb = linToDb (env);
            if (eDb > thrDb)
            {
                const float gr = std::min ((eDb - thrDb) * slope, 15.f);
                const float cut = 1.f - dbToLin (-gr);
                l[i] -= bl * cut;
                r[i] -= br * cut;
                maxGr = std::max (maxGr, gr);
            }
        }
        grDb = maxGr;
    }
};

// --------------------------------------------------------------- Compresseur
struct Compressor
{
    float fs = 44100.f, grState = 0.f, grMax = 0.f;

    void reset() { grState = 0.f; grMax = 0.f; }

    void process (float* l, float* r, int n, float thr, float ratio, float attMs, float relMs, float makeupDb)
    {
        const float attC = coefFromMs (fs, attMs);
        const float relC = coefFromMs (fs, relMs);
        const float knee = 6.f;
        const float slope = 1.f - 1.f / std::max (ratio, 1.f);
        const float makeup = dbToLin (makeupDb);
        grMax = 0.f;

        for (int i = 0; i < n; ++i)
        {
            const float det = std::max (std::fabs (l[i]), std::fabs (r[i]));
            const float over = linToDb (det) - thr;

            float gr = 0.f;
            if (2.f * over > knee)              gr = slope * over;
            else if (2.f * over > -knee)        { const float x = over + knee * 0.5f; gr = slope * x * x / (2.f * knee); }

            const float c = (gr > grState) ? attC : relC;
            grState = c * grState + (1.f - c) * gr;
            if (grState < 1.0e-4f) grState = 0.f;
            grMax = std::max (grMax, grState);

            const float g = dbToLin (-grState) * makeup;
            l[i] *= g;
            r[i] *= g;
        }
    }
};

// ---------------------------------------------------------------- Saturation
inline void saturate (float* l, float* r, int n, float drive)
{
    if (drive < 0.005f) return;
    const float k    = 1.f + drive * 3.f;
    const float norm = 1.f / std::tanh (k);
    const float mix  = std::min (1.f, drive * 1.2f);
    const float comp = 1.f / (1.f + (k - 1.f) * mix * 0.6f);
    for (int i = 0; i < n; ++i)
    {
        const float wl = std::tanh (l[i] * k) * norm;
        const float wr = std::tanh (r[i] * k) * norm;
        l[i] = (l[i] + mix * (wl - l[i])) * comp;
        r[i] = (r[i] + mix * (wr - r[i])) * comp;
    }
}

// -------------------------------------------------------------------- Reverb
class Reverb
{
public:
    void prepare (float sampleRate)
    {
        fs = sampleRate;
        static const int combT[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
        static const int apT[4]   = { 556, 441, 341, 225 };
        const float sc = fs / 44100.f;
        for (int i = 0; i < 8; ++i)
        {
            combL[i].init ((int) (combT[i] * sc));
            combR[i].init ((int) ((combT[i] + 23) * sc));
        }
        for (int i = 0; i < 4; ++i)
        {
            apL[i].init ((int) (apT[i] * sc));
            apR[i].init ((int) ((apT[i] + 23) * sc));
        }
        preBuf.assign ((size_t) (0.12f * fs) + 2, 0.f);
        preIdx = 0;
        hp.set (Biquad::highPass (fs, 200.0, 0.7071));
        lp.set (Biquad::lowPass (fs, 8000.0, 0.7071));
        hp.reset(); lp.reset();
    }

    void setParams (float size, float preMs)
    {
        const float fb = 0.70f + 0.27f * clampf (size, 0.f, 1.f);
        for (int i = 0; i < 8; ++i) { combL[i].fb = combR[i].fb = fb; combL[i].damp = combR[i].damp = 0.35f; }
        preSamples = std::min ((int) (preMs * 0.001f * fs), (int) preBuf.size() - 1);
    }

    void process (float in, float& outL, float& outR)
    {
        const int sz = (int) preBuf.size();
        preBuf[(size_t) preIdx] = in;
        int rd = preIdx - preSamples;
        if (rd < 0) rd += sz;
        float x = preBuf[(size_t) rd];
        if (++preIdx >= sz) preIdx = 0;

        x = lp.process (hp.process (x));
        const float inp = x * 0.015f;

        float sl = 0.f, sr = 0.f;
        for (int i = 0; i < 8; ++i) { sl += combL[i].process (inp); sr += combR[i].process (inp); }
        for (int i = 0; i < 4; ++i) { sl = apL[i].process (sl); sr = apR[i].process (sr); }
        outL = sl * 3.f;
        outR = sr * 3.f;
    }

private:
    struct Comb
    {
        std::vector<float> buf; int idx = 0; float store = 0.f, fb = 0.84f, damp = 0.35f;
        void init (int len) { buf.assign ((size_t) std::max (len, 1), 0.f); idx = 0; store = 0.f; }
        inline float process (float in)
        {
            const float out = buf[(size_t) idx];
            store = out * (1.f - damp) + store * damp;
            buf[(size_t) idx] = in + store * fb;
            if (++idx >= (int) buf.size()) idx = 0;
            return out;
        }
    };
    struct Allpass
    {
        std::vector<float> buf; int idx = 0;
        void init (int len) { buf.assign ((size_t) std::max (len, 1), 0.f); idx = 0; }
        inline float process (float in)
        {
            const float b = buf[(size_t) idx];
            const float out = -in + b;
            buf[(size_t) idx] = in + b * 0.5f;
            if (++idx >= (int) buf.size()) idx = 0;
            return out;
        }
    };

    float fs = 44100.f;
    Comb combL[8], combR[8];
    Allpass apL[4], apR[4];
    std::vector<float> preBuf;
    int preIdx = 0, preSamples = 0;
    Biquad hp, lp;
};

// --------------------------------------------------------------------- Delay
class StereoDelay
{
public:
    void prepare (float sampleRate)
    {
        fs = sampleRate;
        n = (int) (1.0f * fs) + 4;
        bufL.assign ((size_t) n, 0.f);
        bufR.assign ((size_t) n, 0.f);
        w = 0; cur = 0.25f * fs; lpL = lpR = 0.f;
        lpA = 1.f - std::exp (-2.f * (float) kPi * 4000.f / fs);
    }

    void process (float in, float timeMs, float fb, float& outL, float& outR)
    {
        const float target = clampf (timeMs * 0.001f * fs, 2.f, (float) (n - 3));
        cur += (target - cur) * 0.0008f;

        float rd = (float) w - cur;
        if (rd < 0.f) rd += (float) n;
        const int i0 = (int) rd;
        const int i1 = (i0 + 1 >= n) ? 0 : i0 + 1;
        const float fr = rd - (float) i0;

        const float tl = bufL[(size_t) i0] * (1.f - fr) + bufL[(size_t) i1] * fr;
        const float tr = bufR[(size_t) i0] * (1.f - fr) + bufR[(size_t) i1] * fr;

        lpL += lpA * (tl - lpL);
        lpR += lpA * (tr - lpR);

        bufL[(size_t) w] = in + fb * lpR;   // feedback croisé : l'écho passe d'un côté à l'autre
        bufR[(size_t) w] = in + fb * lpL;
        if (++w >= n) w = 0;

        outL = tl;
        outR = tr;
    }

private:
    float fs = 44100.f, cur = 0.f, lpL = 0.f, lpR = 0.f, lpA = 0.5f;
    int n = 1, w = 0;
    std::vector<float> bufL, bufR;
};

// ------------------------------------------------------------------ Limiteur
struct Limiter
{
    float fs = 44100.f, gain = 1.f, grMax = 0.f;

    void reset() { gain = 1.f; grMax = 0.f; }

    void process (float* l, float* r, int n, float ceilDb)
    {
        const float ceil = dbToLin (ceilDb);
        const float rel  = coefFromMs (fs, 90.f);
        grMax = 0.f;
        for (int i = 0; i < n; ++i)
        {
            const float pk = std::max (std::fabs (l[i]), std::fabs (r[i]));
            const float need = (pk > ceil) ? ceil / pk : 1.f;
            gain = (need < gain) ? need : rel * gain + (1.f - rel) * need;
            l[i] = clampf (l[i] * gain, -ceil, ceil);
            r[i] = clampf (r[i] * gain, -ceil, ceil);
            grMax = std::max (grMax, -linToDb (gain));
        }
    }
};

// ----------------------------------------------------------- Chaîne complète
class VoiceChain
{
public:
    std::atomic<float> inPeak { 0.f }, outPeak { 0.f }, compGr { 0.f }, deessGr { 0.f }, limGr { 0.f };

    void prepare (double sampleRate)
    {
        fs = (float) sampleRate;
        gate.fs = deess.fs = comp.fs = lim.fs = fs;
        reverb.prepare (fs);
        delay.prepare (fs);
        reset();
    }

    void reset()
    {
        gate.reset(); deess.reset(); comp.reset(); lim.reset();
        hpL.reset(); hpR.reset();
        for (int b = 0; b < 4; ++b) { eqL[b].reset(); eqR[b].reset(); cachedEq[b] = 1.0e9f; }
        cachedHpf = -1.f;
    }

    // Latence : 0 échantillon.
    void process (float* l, float* r, int n, const Params& p)
    {
        // 1) gain d'entrée
        const float inG = dbToLin (p[pInGain]);
        float inPk = 0.f;
        for (int i = 0; i < n; ++i)
        {
            l[i] *= inG; r[i] *= inG;
            inPk = std::max (inPk, std::max (std::fabs (l[i]), std::fabs (r[i])));
        }

        // 2) filtre grave
        if (p[pHpf] != cachedHpf)
        {
            const auto c = Biquad::highPass (fs, p[pHpf], 0.7071);
            hpL.set (c); hpR.set (c);
            cachedHpf = p[pHpf];
        }
        for (int i = 0; i < n; ++i) { l[i] = hpL.process (l[i]); r[i] = hpR.process (r[i]); }

        // 3) gate, 4) de-esser
        gate.process (l, r, n, p[pGate]);
        deess.process (l, r, n, p[pDeess], p[pDeessFreq]);

        // 5) EQ 4 bandes
        const float gains[4] = { p[pEqLow], p[pEqMud], p[pEqPres], p[pEqAir] };
        for (int b = 0; b < 4; ++b)
        {
            if (gains[b] != cachedEq[b])
            {
                Biquad::C c;
                switch (b)
                {
                    case 0:  c = Biquad::lowShelf  (fs, 180.0, gains[b]); break;
                    case 1:  c = Biquad::peak      (fs, 350.0, 1.0, gains[b]); break;
                    case 2:  c = Biquad::peak      (fs, 3200.0, 0.9, gains[b]); break;
                    default: c = Biquad::highShelf (fs, 10000.0, gains[b]); break;
                }
                eqL[b].set (c); eqR[b].set (c);
                cachedEq[b] = gains[b];
            }
            if (std::fabs (gains[b]) >= 0.05f)
                for (int i = 0; i < n; ++i) { l[i] = eqL[b].process (l[i]); r[i] = eqR[b].process (r[i]); }
        }

        // 6) compresseur, 7) saturation
        comp.process (l, r, n, p[pCompThr], p[pCompRatio], p[pCompAtt], p[pCompRel], p[pCompMk]);
        saturate (l, r, n, p[pSat]);

        // 8) reverb + delay (envoi), 9) largeur, 10) gain de sortie
        reverb.setParams (p[pRevSize], p[pRevPre]);
        const float rMix = p[pRevMix], dMix = p[pDelMix], width = p[pWidth];
        const float outG = dbToLin (p[pOutGain]);
        for (int i = 0; i < n; ++i)
        {
            const float mono = 0.5f * (l[i] + r[i]);
            float ra, rb, da, db;
            reverb.process (mono, ra, rb);
            delay.process (mono, p[pDelTime], p[pDelFb], da, db);

            const float yl = l[i] + rMix * ra + dMix * da;
            const float yr = r[i] + rMix * rb + dMix * db;
            const float m = 0.5f * (yl + yr);
            const float s = 0.5f * (yl - yr) * width;
            l[i] = (m + s) * outG;
            r[i] = (m - s) * outG;
        }

        // 11) limiteur
        lim.process (l, r, n, p[pCeil]);

        float outPk = 0.f;
        for (int i = 0; i < n; ++i) outPk = std::max (outPk, std::max (std::fabs (l[i]), std::fabs (r[i])));

        // mesures pour l'interface (le thread de l'interface remet à zéro avec exchange(0))
        inPeak.store  (std::max (inPeak.load(),  inPk));
        outPeak.store (std::max (outPeak.load(), outPk));
        compGr.store  (std::max (compGr.load(),  comp.grMax));
        deessGr.store (std::max (deessGr.load(), deess.grDb));
        limGr.store   (std::max (limGr.load(),   lim.grMax));
    }

private:
    float fs = 44100.f;
    Gate gate; DeEsser deess; Compressor comp; Limiter lim;
    Reverb reverb; StereoDelay delay;
    Biquad hpL, hpR, eqL[4], eqR[4];
    float cachedHpf = -1.f, cachedEq[4] = { 1e9f, 1e9f, 1e9f, 1e9f };
};

// ------------------------------------------------------- Analyse de la voix
struct VoiceReport
{
    bool  valid = false;          // assez de voix analysée (>= 2 s)
    float rmsDb = -100.f, peakDb = -100.f, crestDb = 0.f;
    float low = 0.f, mid = 0.f, pres = 0.f, sib = 0.f;   // part d'énergie par zone (somme = 1)
    float activeSec = 0.f;
};

class VoiceAnalyzer
{
public:
    void prepare (double sampleRate)
    {
        fs = (float) sampleRate;
        lp1.set (Biquad::lowPass  (fs, 250.0, 0.7071));
        hp2.set (Biquad::highPass (fs, 250.0, 0.7071));
        lp2.set (Biquad::lowPass  (fs, 2000.0, 0.7071));
        hp3.set (Biquad::highPass (fs, 2000.0, 0.7071));
        lp3.set (Biquad::lowPass  (fs, 5000.0, 0.7071));
        hp4.set (Biquad::highPass (fs, 5000.0, 0.7071));
        clear();
    }

    void requestReset() { resetReq.store (true); }

    // Thread audio : analyse l'entrée, ne retient que les passages où la voix est présente.
    void feed (const float* l, const float* r, int n)
    {
        if (resetReq.exchange (false)) clear();

        double sumSq = 0.0, e[4] = { 0, 0, 0, 0 };
        float pk = 0.f;
        for (int i = 0; i < n; ++i)
        {
            const float m = 0.5f * (l[i] + r[i]);
            sumSq += (double) m * m;
            pk = std::max (pk, std::fabs (m));
            const float b0 = lp1.process (m);
            const float b1 = lp2.process (hp2.process (m));
            const float b2 = lp3.process (hp3.process (m));
            const float b3 = hp4.process (m);
            e[0] += (double) b0 * b0; e[1] += (double) b1 * b1;
            e[2] += (double) b2 * b2; e[3] += (double) b3 * b3;
        }
        if (n <= 0) return;

        const float blockRmsDb = linToDb ((float) std::sqrt (sumSq / n));
        if (blockRmsDb > -50.f)
        {
            act += n; sq += sumSq; peak = std::max (peak, pk);
            for (int k = 0; k < 4; ++k) E[k] += e[k];
            publish();
        }
    }

    VoiceReport report() const
    {
        VoiceReport r;
        r.activeSec = aSec.load();
        r.rmsDb = aRms.load(); r.peakDb = aPeak.load(); r.crestDb = aCrest.load();
        r.low = aLow.load(); r.mid = aMid.load(); r.pres = aPres.load(); r.sib = aSib.load();
        r.valid = r.activeSec >= 2.0f;
        return r;
    }

private:
    void clear()
    {
        act = 0; sq = 0.0; peak = 0.f;
        for (double& x : E) x = 0.0;
        aSec = 0.f; aRms = -100.f; aPeak = -100.f; aCrest = 0.f; aLow = aMid = aPres = aSib = 0.f;
        lp1.reset(); hp2.reset(); lp2.reset(); hp3.reset(); lp3.reset(); hp4.reset();
    }

    void publish()
    {
        const float rms = linToDb ((float) std::sqrt (sq / (double) act));
        const float pkDb = linToDb (peak);
        const double tot = E[0] + E[1] + E[2] + E[3] + 1.0e-20;
        aSec = (float) act / fs; aRms = rms; aPeak = pkDb; aCrest = pkDb - rms;
        aLow = (float) (E[0] / tot); aMid = (float) (E[1] / tot);
        aPres = (float) (E[2] / tot); aSib = (float) (E[3] / tot);
    }

    float fs = 44100.f;
    Biquad lp1, hp2, lp2, hp3, lp3, hp4;
    long long act = 0; double sq = 0.0, E[4] = { 0, 0, 0, 0 }; float peak = 0.f;
    std::atomic<bool> resetReq { false };
    std::atomic<float> aSec { 0.f }, aRms { -100.f }, aPeak { -100.f }, aCrest { 0.f },
                       aLow { 0.f }, aMid { 0.f }, aPres { 0.f }, aSib { 0.f };
};

// ----------------------------------------------------------- Styles & Auto Mix
inline const char* styleName (int i)
{
    static const char* n[5] = { "Rap", "Chant", "Voix homme", "Voix femme", "Coupé-décalé" };
    return n[std::min (std::max (i, 0), 4)];
}

inline Params stylePreset (int style)
{
    Params p;
    int count = 0;
    auto set = [&] (std::initializer_list<float> v) { int i = 0; for (float x : v) p[i++] = x; count = i; };

    //      gain hpf gate  deess dFq   low  mud  pres air  thr ratio att rel mk  sat   rev   size pre del   dTime fb   wid  ceil out
    switch (style)
    {
        case 0:  set ({ 0, 90, -55, .40f, 6500, -1, -2.5f, 3, 2.5f, -20, 4, 6, 90, 4, .25f, .08f, .35f, 15, .10f, 280, .25f, 1.1f, -1, 0 }); break; // Rap
        case 1:  set ({ 0, 80, -60, .35f, 6500, 0, -2, 2, 3, -18, 3, 15, 140, 3, .12f, .22f, .60f, 25, .12f, 350, .30f, 1.3f, -1, 0 });     break; // Chant
        case 2:  set ({ 0, 90, -58, .30f, 6000, -1.5f, -3, 2, 1.5f, -18, 3, 12, 120, 3, .15f, .10f, .40f, 20, .05f, 300, .20f, 1.0f, -1, 0 }); break; // Voix homme
        case 3:  set ({ 0, 120, -58, .50f, 7500, -1, -1.5f, 1.5f, 2, -17, 3, 10, 110, 3, .10f, .14f, .45f, 20, .06f, 300, .20f, 1.1f, -1, 0 }); break; // Voix femme
        default: set ({ 0, 100, -52, .45f, 6500, 1, -3, 3.5f, 3, -22, 5, 4, 70, 5, .30f, .12f, .40f, 10, .15f, 220, .30f, 1.2f, -1, 0 });   break; // Coupé-décalé
    }
    assert (count == kNumParams);
    (void) count;
    return p;
}

// Auto Mix local : part du preset du style choisi, puis corrige selon l'analyse de la voix.
inline Params autoMix (const VoiceReport& r, int style)
{
    Params p = stylePreset (style);
    if (! r.valid) return p;

    // niveau : amener la voix vers -18 dBFS RMS
    p[pInGain] = clampf (-18.f - r.rmsDb, -12.f, 12.f);

    // dynamique : voix très dynamique -> compression plus ferme
    if (r.crestDb > 16.f)
    {
        const float extra = clampf ((r.crestDb - 16.f) * 0.5f, 0.f, 4.f);
        p[pCompThr]   = clampf (p[pCompThr] - 2.f * extra, -40.f, 0.f);
        p[pCompRatio] = clampf (p[pCompRatio] + 0.5f * extra, 1.f, 10.f);
    }

    // sibilances : rapport sibilance / présence
    const float sibRatio = r.sib / (r.pres + 1.0e-6f);
    if (sibRatio > 0.6f)       p[pDeess] = clampf (p[pDeess] + (sibRatio - 0.6f) * 1.0f, 0.f, 0.9f);
    else if (sibRatio < 0.2f)  p[pDeess] = clampf (p[pDeess] - 0.15f, 0.1f, 0.9f);

    // équilibre des graves : trop de grave -> alléger, voix fine -> réchauffer
    if (r.low > 0.45f)
    {
        p[pEqLow] = clampf (p[pEqLow] - 2.f, -12.f, 12.f);
        p[pEqMud] = clampf (p[pEqMud] - 2.f, -12.f, 12.f);
    }
    else if (r.low < 0.15f)
        p[pEqLow] = clampf (p[pEqLow] + 2.f, -12.f, 12.f);

    // présence
    if (r.pres < 0.12f)       p[pEqPres] = clampf (p[pEqPres] + 2.f, -12.f, 12.f);
    else if (r.pres > 0.35f)  p[pEqPres] = clampf (p[pEqPres] - 1.5f, -12.f, 12.f);

    return p;
}
} // namespace topvox
