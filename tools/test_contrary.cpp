// Offline verification for gz::scale::ContraryLine (反行ハモリ) — no JUCE needed.
#include "../VocalGzzio/Source/PitchDetector.h"
#include <cstdio>
#include <vector>
#include <random>

using namespace gz::scale;

static bool inScale (float note, int key, int sc)
{
    int pc = ((int) std::lround (note) - key) % 12; if (pc < 0) pc += 12;
    return (mask (sc) & (1 << pc)) != 0;
}

int main()
{
    std::mt19937 rng (20260725);
    int total = 0, badScale = 0, badIv = 0, contraryOk = 0, moves = 0, runaway = 0;
    // sweep: 12 keys x 8 non-chromatic scales x 40 random walks x 64 steps
    for (int key = 0; key < 12; ++key)
    for (int sc = 1; sc < Count; ++sc)
    for (int trial = 0; trial < 40; ++trial)
    {
        ContraryLine cl;
        std::uniform_int_distribution<int> mv (-3, 3);
        float mel = 60.0f + (float) (rng() % 12);           // start around C4
        float prevMelSnap = 0, prevHarm = 0;
        for (int i = 0; i < 64; ++i)
        {
            mel = snap (mel, key, sc) ;
            mel = step (mel, key, sc, mv (rng));            // random in-scale walk
            mel = std::min (79.0f, std::max (48.0f, mel));  // keep in vocal range
            const float h = cl.update (mel, key, sc);
            ++total;
            if (! inScale (h, key, sc)) { ++badScale; }
            const int iv = std::abs ((int) std::lround (h - snap (mel, key, sc))) % 12;
            // allowed after guards: 3,4,5,7,8,9 (0/12 may transiently appear only if guards failed)
            if (! (iv==3||iv==4||iv==5||iv==7||iv==8||iv==9)) ++badIv;
            if (std::abs (h - mel) > 14.0f) ++runaway;
            const float ms = snap (mel, key, sc);
            if (prevMelSnap > 0 && std::abs (ms - prevMelSnap) >= 0.5f && prevHarm > 0)
            {
                ++moves;
                const float dm = ms - prevMelSnap, dh = h - prevHarm;
                // contrary or oblique (guards may force same-direction re-anchor)
                if (dm * dh <= 0.0f) ++contraryOk;
            }
            prevMelSnap = ms; prevHarm = h;
        }
    }
    printf ("checked %d readings\n", total);
    printf ("  out-of-scale          : %d\n", badScale);
    printf ("  disallowed interval   : %d (%.2f%%)\n", badIv, 100.0 * badIv / total);
    printf ("  runaway (>14 st)      : %d\n", runaway);
    printf ("  contrary/oblique rate : %.1f%% of %d melody moves\n", 100.0 * contraryOk / (moves ? moves : 1), moves);
    const bool pass = badScale == 0 && runaway == 0 && badIv == 0 && contraryOk >= (int)(0.65 * moves);
    printf (pass ? "PASS\n" : "FAIL\n");
    return pass ? 0 : 1;
}
