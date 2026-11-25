// Tests du moteur audio TOPVOX VOICE (sans JUCE).
// Compilation : g++ -std=c++17 -O2 -I../Source dsp_test.cpp -o dsp_test
#include "VoiceDSP.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>

using namespace topvox;

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("  [ECHEC] %s\n", msg); ++failures; } else std::printf("  [ok]    %s\n", msg); } while (0)

static std::vector<float> makeVoice (float fs, float sec, float rmsDb, float sibGain, unsigned seed = 1)
{
    const int n = (int) (fs * sec);
    std::vector<float> x ((size_t) n);
    std::mt19937 rng (seed);
    std::uniform_real_distribution<float> U (-1.f, 1.f);
    Biquad hp, lp9;
    hp.set (Biquad::highPass (fs, 5500.0, 0.7071));
    lp9.set (Biquad::lowPass (fs, 9000.0, 0.7071));
    double ph = 0.0;
    double sumSq = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / (double) fs;
        float syl = 0.5f + 0.5f * (float) std::sin (2.0 * kPi * 3.0 * t);
        syl *= syl;
        const double f0 = 140.0 + 20.0 * std::sin (2.0 * kPi * 0.5 * t);
        ph += 2.0 * kPi * f0 / fs;
        float s = 0.f;
        for (int h = 1; h <= 20; ++h) s += (float) std::sin (h * ph) / (float) h;
        const float vowel = s * syl;
        const float sibEnv = std::max (0.f, 0.3f - syl) / 0.3f;
        const float sib = lp9.process (hp.process (U (rng))) * sibEnv * sibGain;
        x[(size_t) i] = vowel + sib;
        sumSq += (double) x[(size_t) i] * x[(size_t) i];
    }
    const float rms = (float) std::sqrt (sumSq / n);
    const float g = dbToLin (rmsDb) / rms;
    for (float& v : x) v *= g;
    return x;
}

static float rmsDbOf (const std::vector<float>& x, size_t a, size_t b)
{
    double s = 0; for (size_t i = a; i < b; ++i) s += (double) x[i] * x[i];
    return linToDb ((float) std::sqrt (s / (double) (b - a)));
}
// étendue dynamique à court terme : percentile 95 - percentile 30 des niveaux RMS par fenêtres de 50 ms (après 1 s)
static float dynRangeDb (const std::vector<float>& x, float fs)
{
    std::vector<float> lv; const int w = (int) (fs * 0.05f);
    for (size_t o = (size_t) fs; o + (size_t) w < x.size(); o += (size_t) w) lv.push_back (rmsDbOf (x, o, o + (size_t) w));
    std::sort (lv.begin(), lv.end());
    return lv[(size_t) (lv.size() * 0.95)] - lv[(size_t) (lv.size() * 0.30)];
}
static float peakOf (const std::vector<float>& x) { float p = 0; for (float v : x) p = std::max (p, std::fabs (v)); return p; }

static void runChain (VoiceChain& c, std::vector<float>& l, std::vector<float>& r, const Params& p, int block)
{
    for (size_t o = 0; o < l.size(); o += (size_t) block)
    {
        const int n = (int) std::min ((size_t) block, l.size() - o);
        c.process (l.data() + o, r.data() + o, n, p);
    }
}

int main()
{
    std::printf("TOPVOX VOICE - tests du moteur audio\n");

    // 1) Tous les styles, plusieurs fréquences d'échantillonnage : pas de NaN, jamais au-dessus du plafond
    for (float fs : { 44100.f, 48000.f, 96000.f })
    {
        for (int st = 0; st < 5; ++st)
        {
            VoiceChain c; c.prepare (fs);
            auto l = makeVoice (fs, 6.f, -12.f, 1.0f);
            auto r = l;
            Params p = stylePreset (st);
            runChain (c, l, r, p, 256);
            bool ok = true;
            for (size_t i = 0; i < l.size(); ++i)
                if (! std::isfinite (l[i]) || ! std::isfinite (r[i])) { ok = false; break; }
            const float ceil = dbToLin (p[pCeil]) + 1.0e-5f;
            char msg[160];
            std::snprintf (msg, sizeof msg, "%s @ %.0f Hz : signal fini et <= plafond (crete %.2f dBFS)", styleName (st), fs, linToDb (peakOf (l)));
            CHECK (ok && peakOf (l) <= ceil && peakOf (r) <= ceil, msg);
        }
    }

    // 2) Limiteur : signal trop fort, réglages extrêmes
    {
        VoiceChain c; c.prepare (44100.f);
        auto l = makeVoice (44100.f, 4.f, -2.f, 3.0f); auto r = l;
        Params p = stylePreset (4);
        p[pInGain] = 12; p[pCompMk] = 18; p[pSat] = 1; p[pRevMix] = 1; p[pDelMix] = 1; p[pDelFb] = 0.85f; p[pWidth] = 2; p[pCeil] = -3;
        runChain (c, l, r, p, 512);
        CHECK (peakOf (l) <= dbToLin (-3.f) + 1e-5f && peakOf (r) <= dbToLin (-3.f) + 1e-5f, "réglages extrêmes : le limiteur tient le plafond (-3 dB)");
    }

    // 3) Gate : bruit de fond seul (-70 dB) fortement atténué, voix conservée
    {
        const float fs = 44100.f;
        VoiceChain c; c.prepare (fs);
        std::mt19937 rng (7); std::uniform_real_distribution<float> U (-1.f, 1.f);
        std::vector<float> l ((size_t) (fs * 4));
        for (auto& v : l) v = 0.00045f * U (rng);                          // ~ -70 dBFS
        auto voice = makeVoice (fs, 1.5f, -14.f, 0.2f);
        for (size_t i = 0; i < voice.size(); ++i) l[(size_t) (fs * 1.5f) + i] += voice[i];  // voix entre 1.5 s et 3 s
        auto r = l;
        const float before = rmsDbOf (l, (size_t) (fs * 3.4f), (size_t) (fs * 3.9f));
        Params p; for (int i = 0; i < kNumParams; ++i) p[i] = (i == pGate) ? -50.f : (i == pRevMix || i == pDelMix || i == pSat || i == pDeess ? 0.f : (i == pEqLow || i == pEqMud || i == pEqPres || i == pEqAir || i == pInGain || i == pOutGain ? 0.f : p[i]));
        p[pCompThr] = 0.f; p[pCompMk] = 0.f;
        runChain (c, l, r, p, 128);
        const float after = rmsDbOf (l, (size_t) (fs * 3.4f), (size_t) (fs * 3.9f));
        char msg[160]; std::snprintf (msg, sizeof msg, "gate : bruit %.1f dB -> %.1f dB (>= 25 dB de réduction)", before, after);
        CHECK (before - after > 25.f, msg);
        const float vAfter = rmsDbOf (l, (size_t) (fs * 1.7f), (size_t) (fs * 2.8f));
        std::snprintf (msg, sizeof msg, "gate : la voix reste présente (%.1f dBFS)", vAfter);
        CHECK (vAfter > -24.f, msg);
    }

    // 4) Compresseur : le facteur de crête diminue
    {
        const float fs = 44100.f;
        VoiceChain c; c.prepare (fs);
        auto l = makeVoice (fs, 6.f, -20.f, 0.f); auto r = l;
        const float rangeBefore = dynRangeDb (l, fs);
        Params p; for (int i = 0; i < kNumParams; ++i) if (i != pCompThr && i != pCompRatio && i != pCompAtt && i != pCompRel && i != pHpf && i != pCeil) p[i] = (i == pDeessFreq || i == pDelTime || i == pRevSize || i == pRevPre || i == pWidth) ? p[i] : 0.f;
        p[pWidth] = 1.f; p[pGate] = -80.f; p[pCompThr] = -30.f; p[pCompRatio] = 6.f; p[pCompAtt] = 5.f; p[pCompRel] = 100.f; p[pCompMk] = 0.f; p[pCeil] = 0.f;
        runChain (c, l, r, p, 256);
        const float rangeAfter = dynRangeDb (l, fs);
        char msg[160]; std::snprintf (msg, sizeof msg, "compresseur : étendue dynamique %.1f dB -> %.1f dB", rangeBefore, rangeAfter);
        CHECK (rangeAfter < rangeBefore - 4.f, msg);
    }

    // 5) De-esser : l'énergie de sibilance (>5 kHz) diminue, le grave est préservé
    {
        const float fs = 44100.f;
        auto base = makeVoice (fs, 6.f, -18.f, 4.0f);
        auto run = [&] (float amount) {
            VoiceChain c; c.prepare (fs);
            auto l = base; auto r = base;
            Params p; for (int i = 0; i < kNumParams; ++i) p[i] = 0.f;
            p[pHpf] = 20; p[pGate] = -80; p[pDeess] = amount; p[pDeessFreq] = 6500; p[pCompThr] = 0; p[pCompRatio] = 1;
            p[pCompAtt] = 10; p[pCompRel] = 100; p[pWidth] = 1; p[pCeil] = 0; p[pRevSize] = 0.5f; p[pDelTime] = 250;
            runChain (c, l, r, p, 256);
            Biquad hp, lp9; hp.set (Biquad::highPass (fs, 5500.0, 0.7071)); lp9.set (Biquad::lowPass (fs, 9000.0, 0.7071));
            std::vector<float> hf (l.size()); for (size_t i = 0; i < l.size(); ++i) hf[i] = lp9.process (hp.process (l[i]));
            Biquad lp; lp.set (Biquad::lowPass (fs, 1000.0, 0.7071));
            std::vector<float> lo (l.size()); for (size_t i = 0; i < l.size(); ++i) lo[i] = lp.process (l[i]);
            return std::make_pair (rmsDbOf (hf, (size_t) fs, hf.size()), rmsDbOf (lo, (size_t) fs, lo.size()));
        };
        auto off = run (0.f), on = run (0.8f);
        char msg[160];
        std::snprintf (msg, sizeof msg, "de-esser : sibilances 5,5-9 kHz %.1f dB -> %.1f dB", off.first, on.first);
        CHECK (off.first - on.first > 3.f, msg);
        std::snprintf (msg, sizeof msg, "de-esser : grave préservé (%.1f dB -> %.1f dB)", off.second, on.second);
        CHECK (std::fabs (off.second - on.second) < 1.0f, msg);
    }

    // 6) Reverb / delay : une impulsion produit une queue puis le silence revient (pas d'emballement)
    {
        const float fs = 44100.f;
        VoiceChain c; c.prepare (fs);
        std::vector<float> l ((size_t) (fs * 12)); l[1000] = 0.5f; auto r = l;
        Params p = stylePreset (1); p[pRevMix] = 0.5f; p[pDelMix] = 0.5f; p[pDelFb] = 0.5f; p[pGate] = -80;
        runChain (c, l, r, p, 512);
        const float tail1 = rmsDbOf (l, (size_t) (fs * 0.2f), (size_t) (fs * 0.6f));
        const float tailEnd = rmsDbOf (l, (size_t) (fs * 10.f), (size_t) (fs * 12.f));
        char msg[160]; std::snprintf (msg, sizeof msg, "reverb+delay : queue %.1f dB puis %.1f dB après 10 s", tail1, tailEnd);
        CHECK (tail1 > -90.f && tailEnd < tail1 - 30.f, msg);
    }

    // 7) Analyse + Auto Mix
    {
        const float fs = 44100.f;
        VoiceAnalyzer a; a.prepare (fs);
        auto quiet = makeVoice (fs, 5.f, -30.f, 6.0f); auto q2 = quiet;
        for (size_t o = 0; o < quiet.size(); o += 256)
            a.feed (quiet.data() + o, q2.data() + o, (int) std::min ((size_t) 256, quiet.size() - o));
        auto rep = a.report();
        char msg[200];
        std::snprintf (msg, sizeof msg, "analyse : voix détectée (%.1f s), RMS %.1f dB, crête %.1f dB, sib %.0f%%", rep.activeSec, rep.rmsDb, rep.peakDb, rep.sib * 100.f);
        CHECK (rep.valid && std::fabs (rep.rmsDb + 30.f) < 3.f, msg);
        const Params base = stylePreset (1);
        const Params am = autoMix (rep, 1);
        std::snprintf (msg, sizeof msg, "auto mix : gain d'entrée remonté (+%.1f dB) pour une voix faible", am[pInGain]);
        CHECK (am[pInGain] > 8.f, msg);
        std::snprintf (msg, sizeof msg, "auto mix : de-esser renforcé (%.2f -> %.2f) pour une voix sifflante", base[pDeess], am[pDeess]);
        CHECK (am[pDeess] > base[pDeess], msg);

        // silence : pas d'analyse valide
        VoiceAnalyzer b; b.prepare (fs);
        std::vector<float> z (44100, 0.f), z2 = z;
        b.feed (z.data(), z2.data(), (int) z.size());
        CHECK (! b.report().valid, "analyse : le silence n'est pas pris pour de la voix");

        // reset
        a.requestReset();
        a.feed (z.data(), z2.data(), 256);
        CHECK (a.report().activeSec == 0.f, "analyse : remise à zéro");
    }

    // 8) Presets : 24 valeurs, dans les bornes
    {
        bool ok = true;
        for (int st = 0; st < 5; ++st)
        {
            Params p = stylePreset (st);
            for (int i = 0; i < kNumParams; ++i)
                if (p[i] < spec (i).mn - 1e-4f || p[i] > spec (i).mx + 1e-4f) { ok = false; std::printf ("     hors bornes : style %d param %s = %f\n", st, spec (i).id, p[i]); }
        }
        CHECK (ok, "presets : toutes les valeurs sont dans les bornes des paramètres");
    }

    // 9) Performance (cible : PC Intel i3 / 4 Go) : 30 s stéréo à 44,1 kHz, blocs de 128
    {
        const float fs = 44100.f;
        VoiceChain c; c.prepare (fs);
        auto l = makeVoice (fs, 30.f, -14.f, 1.0f); auto r = l;
        Params p = stylePreset (4); p[pDelMix] = 0.3f;
        const auto t0 = std::chrono::steady_clock::now();
        runChain (c, l, r, p, 128);
        const double sec = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        const double cpu = sec / 30.0 * 100.0;
        char msg[160]; std::snprintf (msg, sizeof msg, "performance : %.2f %% d'un coeur de ce serveur (30 s traitées en %.3f s)", cpu, sec);
        CHECK (cpu < 15.0, msg);
    }

    std::printf ("\n%s (%d échec(s))\n", failures == 0 ? "TOUS LES TESTS PASSENT" : "ECHECS", failures);
    return failures == 0 ? 0 : 1;
}
