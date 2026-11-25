#pragma once

// Liste unique des paramètres de TOPVOX VOICE.
// Utilisée par le moteur audio, le plugin JUCE et l'interface.

namespace topvox
{
enum P
{
    pInGain = 0, pHpf, pGate,
    pDeess, pDeessFreq,
    pEqLow, pEqMud, pEqPres, pEqAir,
    pCompThr, pCompRatio, pCompAtt, pCompRel, pCompMk,
    pSat,
    pRevMix, pRevSize, pRevPre,
    pDelMix, pDelTime, pDelFb,
    pWidth, pCeil, pOutGain,
    kNumParams
};

struct ParamSpec
{
    const char* id;
    const char* name;   // UTF-8
    float mn, mx, def, step, skew;
    const char* unit;   // UTF-8
    int dec;            // décimales affichées
};

inline const ParamSpec& spec (int i)
{
    static const ParamSpec s[kNumParams] = {
        { "inGain",    "Gain entrée",     -12.f,   12.f,    0.f,   0.1f, 1.0f, " dB",  1 },
        { "hpf",       "Filtre grave",     20.f,  300.f,   80.f,   1.0f, 0.5f, " Hz",  0 },
        { "gate",      "Gate",            -80.f,  -20.f,  -65.f,   0.5f, 1.0f, " dB",  1 },
        { "deess",     "De-esser",          0.f,    1.f,   0.3f,  0.01f, 1.0f, "",     2 },
        { "deessFreq", "Fréq. De-esser", 3000.f,10000.f, 6500.f,  50.0f, 1.0f, " Hz",  0 },
        { "eqLow",     "Chaleur",         -12.f,   12.f,    0.f,   0.1f, 1.0f, " dB",  1 },
        { "eqMud",     "Boue",            -12.f,   12.f,   -2.f,   0.1f, 1.0f, " dB",  1 },
        { "eqPres",    "Présence",        -12.f,   12.f,    2.f,   0.1f, 1.0f, " dB",  1 },
        { "eqAir",     "Air",             -12.f,   12.f,    2.f,   0.1f, 1.0f, " dB",  1 },
        { "compThr",   "Seuil",           -40.f,    0.f,  -18.f,   0.5f, 1.0f, " dB",  1 },
        { "compRatio", "Ratio",             1.f,   10.f,    3.f,   0.1f, 0.6f, ":1",   1 },
        { "compAtt",   "Attaque",          0.5f,   80.f,   10.f,   0.1f, 0.5f, " ms",  1 },
        { "compRel",   "Relâche",          20.f,  500.f,  120.f,   1.0f, 0.5f, " ms",  0 },
        { "compMk",    "Makeup",            0.f,   18.f,    3.f,   0.1f, 1.0f, " dB",  1 },
        { "sat",       "Saturation",        0.f,    1.f,  0.15f,  0.01f, 1.0f, "",     2 },
        { "revMix",    "Reverb",            0.f,    1.f,  0.12f,  0.01f, 1.0f, "",     2 },
        { "revSize",   "Taille",            0.f,    1.f,   0.5f,  0.01f, 1.0f, "",     2 },
        { "revPre",    "Pré-delay",         0.f,  100.f,   20.f,   1.0f, 1.0f, " ms",  0 },
        { "delMix",    "Delay",             0.f,    1.f,    0.f,  0.01f, 1.0f, "",     2 },
        { "delTime",   "Temps",            50.f,  800.f,  250.f,   1.0f, 0.6f, " ms",  0 },
        { "delFb",     "Feedback",          0.f,  0.85f,  0.25f,  0.01f, 1.0f, "",     2 },
        { "width",     "Largeur",           0.f,    2.f,    1.f,  0.01f, 1.0f, "",     2 },
        { "ceil",      "Plafond",         -12.f,    0.f,   -1.f,   0.1f, 1.0f, " dB",  1 },
        { "outGain",   "Gain sortie",     -12.f,   12.f,    0.f,   0.1f, 1.0f, " dB",  1 },
    };
    return s[i];
}
} // namespace topvox
