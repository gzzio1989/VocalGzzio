#include "TestPaths.h"
// dsp_dnfade.cpp — 「ノイズ除去が、時間が経つにつれてサフサフしていく」の検査。
//
//  報告: ノイズキャンセルを掛けて最初は良いのに、しゃべっているうちに
//  だんだん音がサフサフしてくる（トーク配信）。
//
//  コードから見つけた原因（自動追従＝LEARNを押していないときの床の推定）:
//   ・床は「速く下がり、+3dB/秒でゆっくり上がる」最小値追従
//   ・ところが**声が出ている間も**上がり続ける（声＞床なので常に上向き）
//   ・ゲートONだと、フレーズの合間はゲートが閉じ、v2.8.0の決まりで
//     追従が止まる ＝ **下がる機会が一度も来ない**
//   → 床が声に向かって一方通行で這い上がるラチェット。数分で床≒声になり、
//     「声そのものがノイズ扱い」になって削られ始める。
//     最初は良いのに時間とともに悪化する、の正体。
//
//  直しかた: 上向きのドリフトは「声が出ていない間」だけにする。
//  起動時は音量で声と決めつけず、定常で周期性のないノイズを確認して
//  床へ追従する。数十秒の待ち時間を必要とせず、早く効いた状態を維持する。
//
//  ここで確かめること:
//   [1] トークの形（声2秒+間0.4秒×150秒・ゲートON）で、声の通り方が
//       最初と最後で変わらないか（直す前: 最後は2割まで削られていた）
//   [2] 息継ぎなしで30秒歌い続けても声が削られないか
//   [3] うるさい部屋（床の初期値が実際より40dB低い）でも自力で立ち上がるか
//   [4] 自動学びなおし(dn_relearn): 部屋のノイズが後から+12dB増えても、
//       静かな間に追いついて、また消せるようになるか
//   [5] 自動学びなおしは、完全な無音（ミュート）では動かないか
//   [6] 自動学びなおしOFF（既定）なら、学習した床は凍ったままか
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

// おそうじ（ゲート・ノイズ除去）だけを通す。他のモジュールは切る。
static void dnOnlySetup (VocalGzzioProcessor& p, float gateDb, float dn)
{
    using M = gz::ModuleChain;
    for (int m : { (int) M::Henshin, (int) M::Totonoe, (int) M::Soroe,
                   (int) M::Sagyo, (int) M::Neiro, (int) M::Chara, (int) M::Hirogari })
        setP (p, M::paramId (m), 0.0f);
    setP (p, "dn_relearn", 0.0f);   // ★前のテストの autosave から持ち越されるので明示する
    setP (p, "gate_on", gateDb > -79.0f ? 1.0f : 0.0f);
    setP (p, "gate",    gateDb);
    setP (p, "dn_on",   1.0f);
    setP (p, "denoise", dn);
    setP (p, "hum_amt", 0.0f);
    setP (p, "mix",     100.0f);
    setP (p, "makeup",  0.0f);
}

// 声っぽい倍音（220Hz、-20dBFS くらい）
struct Talker
{
    double ph = 0.0;
    float next()
    {
        double v = 0.0;
        for (int h = 1; h <= 12; ++h) v += std::sin (ph * h) / h;
        ph += 2.0 * juce::MathConstants<double>::pi * 220.0 / kSR;
        return (float) (0.075 * v);          // ピークおよそ -20dBFS
    }
};

// 部屋ノイズ（白色をならしたもの）。levelDb は RMS 目安。
struct Room
{
    unsigned s = 123u; float lp = 0.0f;
    float next (float levelDb)
    {
        s = s * 1664525u + 1013904223u;
        const float w = (float) ((int) (s >> 9) - 4194304) / 4194304.0f;
        lp += 0.25f * (w - lp);              // 高域を少しならす
        return lp * 2.0f * juce::Decibels::decibelsToGain (levelDb);
    }
};

struct RunStat { double inRms = 0, outRms = 0; long n = 0;
                 void add (float i, float o) { inRms += (double) i * i; outRms += (double) o * o; ++n; }
                 double ratioDb() const { return 10.0 * std::log10 ((outRms + 1e-20) / (inRms + 1e-20)); } };

// 声2.0秒 + 間0.4秒 を繰り返し、各バーストの「声の通り方(dB)」を返す
static std::vector<double> talkRun (VocalGzzioProcessor& p, double seconds, float noiseDb)
{
    juce::AudioBuffer<float> buf (2, kBS);
    juce::MidiBuffer midi;
    Talker voice; Room room;
    const long total = (long) (kSR * seconds);
    const long burst = (long) (kSR * 2.0), gap = (long) (kSR * 0.4);
    const long cycle = burst + gap;
    std::vector<double> ratios;
    RunStat cur; bool inBurstPrev = false;
    for (long done = 0; done < total; done += kBS)
    {
        for (int i = 0; i < kBS; ++i)
        {
            const long t = done + i;
            const bool inBurst = (t % cycle) < burst;
            const bool measure = inBurst && (t % cycle) > (long) (kSR * 0.3);  // 立ち上がりは見ない
            const float v = inBurst ? voice.next() : 0.0f;
            const float x = v + room.next (noiseDb);
            buf.setSample (0, i, x); buf.setSample (1, i, x);
            if (inBurstPrev && ! inBurst) { if (cur.n > 0) ratios.push_back (cur.ratioDb()); cur = RunStat{}; }
            inBurstPrev = inBurst;
            if (measure) cur.add (v, 0.0f);          // 出力は後で足す
        }
        p.processBlock (buf, midi);
        // 出力側を同じ窓で測り直す（入力は上で足した。窓の判定は同じ式）
        for (int i = 0; i < kBS; ++i)
        {
            const long t = done + i;
            const bool inBurst = (t % cycle) < burst;
            const bool measure = inBurst && (t % cycle) > (long) (kSR * 0.3);
            if (measure) { cur.outRms += (double) buf.getSample (0, i) * buf.getSample (0, i); }
        }
    }
    return ratios;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    auto autosave = gz::dataDirectory().getChildFile ("autosave.xml");
    auto backup = autosave.getSiblingFile ("autosave.bak_dnfade");
    autosave.getParentDirectory().createDirectory();
    if (autosave.existsAsFile()) autosave.moveFileTo (backup);
    struct Restore { juce::File a, b;
        ~Restore() { a.deleteFile(); if (b.existsAsFile()) b.moveFileTo (a); } } rst { autosave, backup };
    autosave.deleteFile();

    std::printf ("ノイズ除去が時間とともに悪化しないか（サフサフのラチェット）\n\n");

    // ---------------------------------------------------------------- [1]
    std::printf ("[1] トークの形で150秒（声2秒+間0.4秒、ゲートON、LEARNなし）\n");
    {
        VocalGzzioProcessor p; dnOnlySetup (p, -45.0f, 60.0f);
        p.prepareToPlay (kSR, kBS);
        auto r = talkRun (p, 150.0, -58.0f);
        double first = 0, last = 0; const int k = 5;
        for (int i = 0; i < k; ++i)               first += r[(size_t) i + 1];   // 0本目は立ち上がり
        for (int i = 0; i < k; ++i)               last  += r[r.size() - 1 - (size_t) i];
        first /= k; last /= k;
        std::printf ("  声の通り方: 最初 %.2f dB → 最後 %.2f dB（差 %.2f dB）\n",
                     first, last, last - first);
        CHECK (last - first > -1.0, "150秒しゃべっても声が削られていかない (差 %.2f dB)", last - first);
    }

    // ---------------------------------------------------------------- [2]
    std::printf ("\n[2] 息継ぎなしで30秒（ゲートOFF、LEARNなし）\n");
    {
        VocalGzzioProcessor p; dnOnlySetup (p, -80.0f, 60.0f);
        p.prepareToPlay (kSR, kBS);
        juce::AudioBuffer<float> buf (2, kBS); juce::MidiBuffer midi;
        Talker voice; Room room;
        const long total = (long) (kSR * 30.0);
        RunStat head, tail;
        for (long done = 0; done < total; done += kBS)
        {
            std::vector<float> in ((size_t) kBS);
            for (int i = 0; i < kBS; ++i)
            {
                in[(size_t) i] = voice.next() + room.next (-58.0f);
                buf.setSample (0, i, in[(size_t) i]); buf.setSample (1, i, in[(size_t) i]);
            }
            p.processBlock (buf, midi);
            for (int i = 0; i < kBS; ++i)
            {
                const double t = (double) (done + i) / kSR;
                if (t > 1.0 && t < 4.0)   head.add (in[(size_t) i], buf.getSample (0, i));
                if (t > 27.0)             tail.add (in[(size_t) i], buf.getSample (0, i));
            }
        }
        std::printf ("  1〜4秒 %.2f dB / 27〜30秒 %.2f dB\n", head.ratioDb(), tail.ratioDb());
        CHECK (tail.ratioDb() - head.ratioDb() > -1.0,
               "歌い続けても削られない (差 %.2f dB)", tail.ratioDb() - head.ratioDb());
    }

    // ---------------------------------------------------------------- [3]
    std::printf ("\n[3] うるさい部屋（-45dBFS）でも、自力でノイズを消し始めるか\n");
    //  初期床が実際より低い場合でも、開始直後から十分に抑制できる。
    //  早期より後期が下がることは要求しない。早期・後期の絶対低減量と
    //  長時間の悪化量を別々に確認し、遅れて効く旧不具合も検出する。
    {
        VocalGzzioProcessor p; dnOnlySetup (p, -80.0f, 60.0f);
        p.prepareToPlay (kSR, kBS);
        juce::AudioBuffer<float> buf (2, kBS); juce::MidiBuffer midi;
        Room room;
        RunStat early, late;
        const long total = (long) (kSR * 70.0);
        for (long done = 0; done < total; done += kBS)
        {
            std::vector<float> in ((size_t) kBS);
            for (int i = 0; i < kBS; ++i)
            {
                in[(size_t) i] = room.next (-45.0f);
                buf.setSample (0, i, in[(size_t) i]); buf.setSample (1, i, in[(size_t) i]);
            }
            p.processBlock (buf, midi);
            for (int i = 0; i < kBS; ++i)
            {
                const double t = (double) (done + i) / kSR;
                if (t > 2.0  && t < 6.0)  early.add (in[(size_t) i], buf.getSample (0, i));
                if (t > 60.0)             late.add (in[(size_t) i], buf.getSample (0, i));
            }
        }
        std::printf ("  ノイズの通り方: 2〜6秒 %.2f dB → 60秒以降 %.2f dB\n",
                     early.ratioDb(), late.ratioDb());
        CHECK (early.ratioDb() < -6.0,
               "開始2〜6秒でノイズを6dB以上抑える (%.2f dB)", early.ratioDb());
        CHECK (late.ratioDb() < -6.0,
               "60秒後もノイズを6dB以上抑える (%.2f dB)", late.ratioDb());
        CHECK (late.ratioDb() - early.ratioDb() < 1.0,
               "長時間使っても抑制が悪化しない (差 %.2f dB)", late.ratioDb() - early.ratioDb());
    }

    // ---------------------------------------------------------------- [4][5][6]
    //  LEARN 済みの床が、あとから増えた部屋ノイズに追従できるか
    auto learnAt = [] (VocalGzzioProcessor& p, float noiseDb) -> bool
    {
        juce::AudioBuffer<float> buf (2, kBS); juce::MidiBuffer midi; Room room;
        // 部屋を1秒鳴らして包絡を落ち着かせてから学習
        for (int b = 0; b < (int) (kSR * 1.0 / kBS); ++b)
        {
            for (int i = 0; i < kBS; ++i) { const float x = room.next (noiseDb);
                buf.setSample (0, i, x); buf.setSample (1, i, x); }
            p.processBlock (buf, midi);
        }
        p.requestDenoiseLearn();
        while (p.isDenoiseLearning())
        {
            for (int i = 0; i < kBS; ++i) { const float x = room.next (noiseDb);
                buf.setSample (0, i, x); buf.setSample (1, i, x); }
            p.processBlock (buf, midi);
        }
        return p.getDenoiseLearnResult() == 1;
    };
    auto floorsOf = [] (VocalGzzioProcessor& p) -> std::array<double,4>
    {
        juce::MemoryBlock mb; p.getStateInformation (mb);
        auto xml = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize());
        std::array<double,4> f {};
        if (xml != nullptr)
            if (auto* d = xml->getChildByName ("DENOISE"))
                for (int b = 0; b < 4; ++b) f[(size_t) b] = d->getDoubleAttribute ("f" + juce::String (b));
        CHECK (std::all_of (f.begin(), f.end(), [] (double value) { return std::isfinite (value) && value > 0.0; }),
               "4帯域の有効な学習値を保存から読み出せる");
        return f;
    };
    auto runNoise = [] (VocalGzzioProcessor& p, double seconds, float noiseDb,
                        double measFrom) -> double
    {
        juce::AudioBuffer<float> buf (2, kBS); juce::MidiBuffer midi; Room room; RunStat st;
        const long total = (long) (kSR * seconds);
        for (long done = 0; done < total; done += kBS)
        {
            std::vector<float> in ((size_t) kBS);
            for (int i = 0; i < kBS; ++i)
            {
                in[(size_t) i] = noiseDb > -200.0f ? room.next (noiseDb) : 0.0f;
                buf.setSample (0, i, in[(size_t) i]); buf.setSample (1, i, in[(size_t) i]);
            }
            p.processBlock (buf, midi);
            for (int i = 0; i < kBS; ++i)
                if ((double) (done + i) / kSR > measFrom)
                    st.add (in[(size_t) i], buf.getSample (0, i));
        }
        return st.ratioDb();
    };

    std::printf ("\n[4] 自動学びなおし: 部屋が後から +12dB うるさくなったとき\n");
    {
        VocalGzzioProcessor pOn;  dnOnlySetup (pOn,  -80.0f, 60.0f);
        VocalGzzioProcessor pOff; dnOnlySetup (pOff, -80.0f, 60.0f);
        setP (pOn, "dn_relearn", 1.0f);
        pOn.prepareToPlay (kSR, kBS); pOff.prepareToPlay (kSR, kBS);
        CHECK (learnAt (pOn,  -60.0f), "LEARN が採用された (relearn ON 側)");
        CHECK (learnAt (pOff, -60.0f), "LEARN が採用された (relearn OFF 側)");
        const double aOn  = runNoise (pOn,  16.0, -48.0f, 12.0);
        const double aOff = runNoise (pOff, 16.0, -48.0f, 12.0);
        std::printf ("  増えたノイズの通り方(12秒後): 学びなおしON %.2f dB / OFF %.2f dB\n", aOn, aOff);
        CHECK (aOn < aOff - 3.0,
               "学びなおしONは増えたノイズにも効く (ON %.2f / OFF %.2f dB)", aOn, aOff);
    }

    std::printf ("\n[5] 自動学びなおしは、完全な無音（ミュート）では動かない\n");
    {
        VocalGzzioProcessor p; dnOnlySetup (p, -80.0f, 60.0f);
        setP (p, "dn_relearn", 1.0f);
        p.prepareToPlay (kSR, kBS);
        CHECK (learnAt (p, -60.0f), "LEARN が採用された");
        const auto before = floorsOf (p);
        (void) runNoise (p, 10.0, -999.0f, 9.0);      // 10秒の完全無音
        const auto after = floorsOf (p);
        double worst = 0.0;
        for (int b = 0; b < 4; ++b)
            worst = std::max (worst, std::abs (20.0 * std::log10 ((after[(size_t)b] + 1e-12)
                                                               / (before[(size_t)b] + 1e-12))));
        CHECK (worst < 0.4, "床が動いていない (最大 %.2f dB)", worst);
    }

    std::printf ("\n[6] 学びなおしOFF（既定）なら、LEARN した床は凍ったまま\n");
    {
        VocalGzzioProcessor p; dnOnlySetup (p, -80.0f, 60.0f);
        p.prepareToPlay (kSR, kBS);
        CHECK (learnAt (p, -60.0f), "LEARN が採用された");
        const auto before = floorsOf (p);
        (void) runNoise (p, 10.0, -48.0f, 9.0);       // 部屋が+12dBうるさくなっても
        const auto after = floorsOf (p);
        double worst = 0.0;
        for (int b = 0; b < 4; ++b)
            worst = std::max (worst, std::abs (20.0 * std::log10 ((after[(size_t)b] + 1e-12)
                                                               / (before[(size_t)b] + 1e-12))));
        CHECK (worst < 0.1, "床は動かない (最大 %.2f dB)", worst);
    }

    std::printf (gFail ? "\n== %d 件 FAIL ==\n" : "\n== 全テスト PASS ==\n", gFail);
    return gFail ? 1 : 0;
}
