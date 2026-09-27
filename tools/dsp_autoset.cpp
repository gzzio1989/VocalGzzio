#include "TestPaths.h"
// dsp_autoset.cpp — v2.12.0 うた自動の判定作り直し(§6-4)の検証。
//
//  「歌自動の結果があたたかい声しか出ない、判断が曖昧かつ雑」:
//  旧判定は 高域の割合−低域の割合(±0.15) で、声はもともと低域優位なので
//  ほぼ全員「あたたかい」だった。しかも あたたかみ(drive)は最低12%固定。
//
//  修正後の確認:
//  [1] 明るい声・暗い声・ふつうの声で**判定が分かれる**こと
//  [2] 胴の鳴っている声では あたたかみ = 0% になれること
//  [3] サ行の強い声では サ行おさえ が強く入ること
//  [4] 実測値(高域の差・サ行%)が表示用に取り出せること
#include "PluginProcessor.h"
#include <cstdio>
#include <cmath>

static int gFail = 0;
#define CHECK(cond, ...) do { \
    if (cond) { std::printf("  PASS: " __VA_ARGS__); std::printf("\n"); } \
    else      { std::printf("  FAIL: " __VA_ARGS__); std::printf("\n"); ++gFail; } } while (0)

static float getP (VocalGzzioProcessor& p, const char* id)
{
    return p.apvts.getRawParameterValue (id)->load();
}

// tiltDbPerOct: 倍音の傾き。-6が標準的な声、-2は明るい声、-10はこもった声
struct Voice
{
    double ph = 0.0; unsigned s = 11u; float sh1 = 0, sh2 = 0, sl1 = 0, sl2 = 0;
    float next (double sr, float tiltDbPerOct, float sibAmt)
    {
        double v = 0.0;
        for (int h = 1; h <= 36; ++h)
        {
            const double f = 220.0 * h;
            if (f > 10000.0) break;
            const double oct = std::log2 (f / 220.0);
            const double a = std::pow (10.0, tiltDbPerOct * oct / 20.0);
            v += a * std::sin (ph * h);
        }
        ph += 2.0 * 3.14159265358979 * 220.0 / sr;
        s = s * 1664525u + 1013904223u;
        const float w = (float)((int)(s>>9)-4194304)/4194304.0f;
        const float a = 0.62f, b = 0.32f;                          // HP(6k寄せ)+LP(8k)
        sh1 = a * (sh1 + w); const float y1 = w - sh1;
        sh2 = a * (sh2 + y1); const float y = y1 - sh2;
        sl1 = b * sl1 + (1-b) * y;
        sl2 = b * sl2 + (1-b) * sl1;                               // 9k超は air 扱いなので落とす
        return (float) (v * 0.08) + sl2 * sibAmt * 6.0f;
    }
};

// 1本の声を「うた自動」に通して、判定コードと主要ツマミを返す
struct Result { int code; float drive, deess, air; float bright, sib; };
static Result runOne (float tilt, float sibAmt)
{
    VocalGzzioProcessor p;
    p.prepareToPlay (44100.0, 128);
    p.requestAutoSetup (1);                                  // うた(8秒)
    juce::AudioBuffer<float> buf (2, 128); juce::MidiBuffer midi;
    Voice vo;
    const int total = (int) (8.3 * 44100.0);
    for (int done = 0; done < total; done += 128)
    {
        auto* L = buf.getWritePointer (0); auto* R = buf.getWritePointer (1);
        for (int i = 0; i < 128; ++i) { const float v = vo.next (44100.0, tilt, sibAmt); L[i]=v; R[i]=v; }
        p.processBlock (buf, midi);
    }
    const int done1 = p.getAutoSetupResult();                // 100 = 取り終わり
    if (done1 != 100) std::printf ("  (注意: 取り込み完了コード %d)\n", done1);
    p.applyAutoSetup();
    Result r;
    r.code  = p.getAutoSetupResult() % 100;
    r.drive = getP (p, "drive");
    r.deess = getP (p, "deess");
    r.air   = getP (p, "air");
    r.bright = p.getAutoBrightDb();
    r.sib    = p.getAutoSibPct();
    return r;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    auto autosave = gz::dataDirectory().getChildFile ("autosave.xml");
    auto backup = autosave.getSiblingFile ("autosave.bak_autoset");
    autosave.getParentDirectory().createDirectory();
    if (autosave.existsAsFile()) autosave.moveFileTo (backup);
    struct Restore { juce::File a, b;
        ~Restore() { a.deleteFile(); if (b.existsAsFile()) b.moveFileTo (a); } } restore { autosave, backup };
    autosave.deleteFile();

    std::printf ("うた自動の判定(あたたかい一辺倒の修正)の検証\n\n");

    const auto bright = runOne (-1.5f, 0.004f);   // 明るい声(傾き浅い)
    const auto normal = runOne (-6.0f, 0.004f);   // 標準的な声
    const auto dark   = runOne (-11.0f, 0.002f);  // こもり気味の声
    const auto sibby  = runOne (-6.0f, 0.050f);   // サ行の強い声

    std::printf ("  明るい: 判定%d 高域%+.1fdB drive%.0f%% deess%.0f%%\n", bright.code, bright.bright, bright.drive, bright.deess);
    std::printf ("  ふつう: 判定%d 高域%+.1fdB drive%.0f%% deess%.0f%%\n", normal.code, normal.bright, normal.drive, normal.deess);
    std::printf ("  暗い : 判定%d 高域%+.1fdB drive%.0f%% deess%.0f%%\n", dark.code, dark.bright, dark.drive, dark.deess);
    std::printf ("  サ行 : 判定%d サ行%.0f%% deess%.0f%%\n\n", sibby.code, sibby.sib, sibby.deess);

    std::printf ("[1] 声がちがえば判定が分かれる\n");
    CHECK (bright.code == 10, "明るい声 → 明るい判定 (実測 %d)", bright.code);
    CHECK (dark.code   == 11, "暗い声 → あたたかい判定 (実測 %d)", dark.code);
    CHECK (bright.code != dark.code && normal.code != 11,
           "3本の声が「あたたかい」一色にならない (%d/%d/%d)", bright.code, normal.code, dark.code);

    std::printf ("\n[2] あたたかみは 0%% になれる\n");
    // ★向きに注意: この合成では「暗い声」=低域どっしり(胴が鳴っている)、
    //   「明るい声」=低域が薄い(細い)。あたたかみは細い声に足すのが正しい。
    CHECK (dark.drive <= 0.5f, "胴の鳴っている(暗い)声は drive 0%% (実測 %.0f%%)", dark.drive);
    CHECK (bright.drive > dark.drive, "細い(明るい)声には足す (%.0f%% > %.0f%%)", bright.drive, dark.drive);

    std::printf ("\n[3] サ行の強い声では おさえ が強く入る\n");
    CHECK (sibby.deess > normal.deess + 8.0f,
           "deess %.0f%% > ふつう %.0f%% + 8", sibby.deess, normal.deess);

    std::printf ("\n[4] 表示用の実測値が取れる\n");
    CHECK (bright.bright > dark.bright + 4.0f,
           "高域の差が声を反映 (%+.1f vs %+.1f dB)", bright.bright, dark.bright);
    // 分母(高域全体)には母音の2-5kHzが常に入るので、割合の動きは数%が正常
    CHECK (sibby.sib > normal.sib + 5.0f,
           "サ行%%が声を反映 (%.0f%% vs %.0f%%)", sibby.sib, normal.sib);

    std::printf (gFail ? "\n== %d 件 FAIL ==\n" : "\n== 全テスト PASS ==\n", gFail);
    return gFail ? 1 : 0;
}
