#include "TestPaths.h"
// dsp_trial.cpp — 体験版ビルド（VOCALGZZIO_TRIAL）の検査。
//
//  ★このテストは build_trial/ の SharedCode に対して、同じ define 付きで
//    コンパイルすること（define でクラスの形が変わるため）。
//    run_tests.sh は ../build_trial があるときだけ回す。
//
//  体験版の約束:
//   [1] 60秒ごとに0.6秒、なめらかに -18dB 落ちる（挿した直後には落ちない）
//   [2] ディップの出入りがなめらか（プチッと言わない）
//   [3] 保存は完全にブロック（DAWへ渡す状態は「体験版でした」だけ、読み込みも無視）
//   [4] autosave ファイルを作らない（閉じるときも書かない）
#include "PluginProcessor.h"
#include <cstdio>
#include <cmath>
#include <vector>

static int gFail = 0;
#define CHECK(cond, ...) do { \
    if (cond) { std::printf("  PASS: " __VA_ARGS__); std::printf("\n"); } \
    else      { std::printf("  FAIL: " __VA_ARGS__); std::printf("\n"); ++gFail; } } while (0)

static constexpr double kSR = 44100.0;
static constexpr int    kBS = 512;

static void setP (VocalGzzioProcessor& p, const char* id, float v)
{
    if (auto* prm = p.apvts.getParameter (id))
        prm->setValueNotifyingHost (p.apvts.getParameterRange (id).convertTo0to1 (v));
}

int main()
{
   #if ! VOCALGZZIO_TRIAL
    std::printf ("(このバイナリは体験版 define なしでビルドされています。テスト対象外)\n");
    return 1;
   #else
    juce::ScopedJuceInitialiser_GUI init;

    auto autosave = gz::dataDirectory().getChildFile ("autosave.xml");
    auto backup = autosave.getSiblingFile ("autosave.bak_trial");
    autosave.getParentDirectory().createDirectory();
    if (autosave.existsAsFile()) autosave.moveFileTo (backup);
    struct Restore { juce::File a, b;
        ~Restore() { a.deleteFile(); if (b.existsAsFile()) b.moveFileTo (a); } } rst { autosave, backup };
    autosave.deleteFile();

    std::printf ("体験版ビルドの検査\n\n");

    // ---------------------------------------------------------------- [1][2]
    std::printf ("[1][2] ディップの周期・深さ・なめらかさ（130秒）\n");
    {
        VocalGzzioProcessor p;
        using M = gz::ModuleChain;
        for (int m = 0; m < M::Count; ++m) setP (p, M::paramId (m), 0.0f);  // 素通しで測る
        setP (p, "mix", 100.0f); setP (p, "makeup", 0.0f);
        p.prepareToPlay (kSR, kBS);

        juce::AudioBuffer<float> buf (2, kBS); juce::MidiBuffer midi;
        double ph = 0.0;
        const long total = (long) (kSR * 130.0);
        // 0.1秒窓のRMSを並べる
        std::vector<double> win; double acc = 0; long n = 0;
        double maxStep = 0.0; float prev = 0.0f;
        for (long done = 0; done < total; done += kBS)
        {
            for (int i = 0; i < kBS; ++i)
            {
                const float x = (float) (0.25 * std::sin (ph));
                ph += 2.0 * juce::MathConstants<double>::pi * 220.0 / kSR;
                buf.setSample (0, i, x); buf.setSample (1, i, x);
            }
            p.processBlock (buf, midi);
            for (int i = 0; i < kBS; ++i)
            {
                const float y = buf.getSample (0, i);
                if (done > (long) kSR)                      // 立ち上がりは見ない
                    maxStep = juce::jmax (maxStep, (double) std::abs (y - prev));
                prev = y;
                acc += (double) y * y;
                if (++n >= (long) (kSR * 0.1))
                { win.push_back (10.0 * std::log10 (acc / (double) n + 1e-20)); acc = 0; n = 0; }
            }
        }
        // 基準 = 5〜50秒の中央値
        std::vector<double> base (win.begin() + 50, win.begin() + 500);
        std::sort (base.begin(), base.end());
        const double ref = base[base.size() / 2];
        // ディップ検出: ref より 6dB 以上低い窓
        std::vector<double> dipT;
        for (size_t i = 0; i < win.size(); ++i)
            if (win[i] < ref - 6.0)
            {
                const double t = 0.1 * (double) i;
                if (dipT.empty() || t - dipT.back() > 5.0) dipT.push_back (t);
            }
        std::printf ("  見つけたディップ: %d 回 (", (int) dipT.size());
        for (double t : dipT) std::printf (" %.1fs", t);
        std::printf (" ) / 基準 %.1f dB\n", ref);
        CHECK (dipT.size() == 2, "130秒でディップは2回 (60秒周期)");
        if (dipT.size() >= 1)
            CHECK (dipT[0] > 30.0 && dipT[0] < 70.0,
                   "最初のディップは挿した直後ではなく約60秒後 (%.1fs)", dipT[0]);
        if (dipT.size() >= 2)
            CHECK (std::abs ((dipT[1] - dipT[0]) - 60.0) < 2.0,
                   "間隔は約60秒 (%.1fs)", dipT[1] - dipT[0]);
        // 深さ: いちばん低い窓が ref-24 〜 ref-12 の範囲（-18dB狙い）
        const double lowest = *std::min_element (win.begin() + 50, win.end());
        CHECK (lowest < ref - 12.0 && lowest > ref - 26.0,
               "深さはおよそ -18dB (最深 %.1f dB)", lowest - ref);
        // なめらかさ: 220Hz 正弦の隣接差の上限は 2π*220/44100*0.25 ≒ 0.0078。
        // フェード 0.15s は十分ゆっくりなので、これを大きく超えたらプチッ。
        CHECK (maxStep < 0.02, "出入りがなめらか (最大段差 %.4f)", maxStep);
    }

    // ---------------------------------------------------------------- [3]
    std::printf ("\n[3] 保存の完全ブロック\n");
    {
        VocalGzzioProcessor p;
        setP (p, "denoise", 77.0f);
        juce::MemoryBlock mb;
        p.getStateInformation (mb);
        auto xml = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize());
        CHECK (xml != nullptr && xml->hasTagName ("VOCALGZZIO_TRIAL_NOSAVE"),
               "保存されるのは「体験版でした」の印だけ");

        VocalGzzioProcessor q;
        q.setStateInformation (mb.getData(), (int) mb.getSize());
        const float dn = q.apvts.getRawParameterValue ("denoise")->load();
        CHECK (std::abs (dn - 0.0f) < 0.01f,
               "読み込みも無視される (denoise=%.0f のまま既定)", dn);
    }

    // ---------------------------------------------------------------- [4]
    std::printf ("\n[4] autosave を作らない\n");
    {
        {
            VocalGzzioProcessor p;
            setP (p, "denoise", 55.0f);       // 変更を作って
        }                                     // 閉じる（デストラクタが書く場面）
        CHECK (! autosave.existsAsFile(), "閉じても autosave.xml が出来ていない");
    }

    std::printf (gFail ? "\n== %d 件 FAIL ==\n" : "\n== 全テスト PASS ==\n", gFail);
    return gFail ? 1 : 0;
   #endif
}
