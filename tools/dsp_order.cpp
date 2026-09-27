#include "TestPaths.h"
// dsp_order.cpp — v3.0-c「順番の分岐」（案C）の検証。
//
//  いただいた最初の指摘のひとつ:「各エフェクトの**起動順**を選べるようにしてほしい」
//
//  8つを自由に並べ替える案（案B）は採らなかった。8つのうち3つが
//  processBlock の中で1か所にまとまっておらず、まとめた瞬間に
//  **いま保存してある設定の音が全部変わる**ため。
//  代わりに「実際に要望が出る分岐だけ」を足した（案C）。
//
//  ここで確かめること:
//   [1] 既定（分岐オフ）は、切り出す前と**1サンプルも変わらない**
//       ＝ 上げただけでは誰の音も変わらない。これがこの作りの生命線。
//   [2] 分岐を入れると、ちゃんと音が変わる（＝飾りのスイッチではない）
//   [3] 分岐を入れても、申告する遅延は増えない（DAWのズレを起こさない）
//   [4] 分岐を入れても、モジュールのON/OFFは今までどおり効く
//   [5] 切り替えでプチッと言わない
//   [8] 分岐3で「ひろがり」を前に出しても、サビリフトが死なない
//       （キャラ声は「測る所」と「音を変える所」の2つでできていて、
//         ひろがりは測った結果を使う。丸ごと動かすと測れなくなる）
#include "PluginProcessor.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <memory>

static int gFail = 0;
#define CHECK(cond, ...) do { \
    if (cond) { std::printf("  PASS: " __VA_ARGS__); std::printf("\n"); } \
    else      { std::printf("  FAIL: " __VA_ARGS__); std::printf("\n"); ++gFail; } } while (0)

static void setP (VocalGzzioProcessor& p, const char* id, float v)
{
    if (auto* prm = p.apvts.getParameter (id))
        prm->setValueNotifyingHost (p.apvts.getParameterRange (id).convertTo0to1 (v));
}

// 分岐が効いていることが分かる設定（サ行と圧縮とEQをはっきり動かす）
static void loudSetup (VocalGzzioProcessor& p)
{
    setP (p, "mud", -12.0f);        // こもり（ととのえ）
    setP (p, "harsh", -12.0f);      // キンキン（ととのえ）
    setP (p, "res_amt", 60.0f);     // なめらか（ととのえ）
    setP (p, "comp1", 70.0f);       // ピーク圧縮（音量そろえ）
    setP (p, "comp2", 80.0f);       // ならし圧縮（音量そろえ）
    setP (p, "ds_on", 1.0f);
    setP (p, "deess", 90.0f);       // サ行おさえ
    setP (p, "makeup", 0.0f);
    setP (p, "mix", 100.0f);
}

// 分岐3（ひろがりをキャラ声の前へ）用。
//  キャラ声（メガホン）と ひろがり（ひびき・広がり・かさね）を両方鳴らす。
//  この分岐は「ロボ声・メガホンに残響を後がけしたくない」ための物なので、
//  どちらかが黙っていると差が出ないのが**正しい**。
static void spaceSetup (VocalGzzioProcessor& p)
{
    setP (p, "mega_on",  1.0f);
    setP (p, "mega_amt", 70.0f);
    setP (p, "mega_type", 0.0f);
    setP (p, "revon",    1.0f);
    setP (p, "revmix",   45.0f);
    setP (p, "revsize",  50.0f);
    setP (p, "width",    50.0f);
    setP (p, "doubler",  35.0f);
}

// 声っぽい合成音 + 本物のサ行（帯域を絞ったノイズ）。dsp_modules と同じ作り。
struct Voice
{
    double ph = 0.0; unsigned s = 7u;
    float hp1 = 0, hp2 = 0, lp1 = 0, lp2 = 0;
    void fill (float* d, int n, double sr)
    {
        for (int i = 0; i < n; ++i)
        {
            double v = 0.0;
            for (int h = 1; h <= 14; ++h) v += std::sin (ph * h) / h;
            ph += 2.0 * juce::MathConstants<double>::pi * 220.0 / sr;
            float x = (float) (0.12 * v);
            s = s * 1664525u + 1013904223u;
            float w = (float) ((int) (s >> 9) - 4194304) / 4194304.0f;
            const float a = 0.49f;
            hp1 = a * (hp1 + w);  const float y1 = w - hp1;
            hp2 = a * (hp2 + y1); float y2 = y1 - hp2;
            const float b = 0.68f;
            lp1 += b * (y2 - lp1);
            lp2 += b * (lp1 - lp2);
            x += lp2 * 6.0f * 0.10f;
            d[i] = x;
        }
    }
};

static std::vector<float> run (VocalGzzioProcessor& p, int blocks, int bs,
                               int flipAt = -1, const char* flipId = nullptr, float flipTo = 0.0f)
{
    juce::AudioBuffer<float> buf (2, bs);
    juce::MidiBuffer midi;
    Voice v;
    std::vector<float> out;
    out.reserve ((size_t) blocks * (size_t) bs);
    for (int b = 0; b < blocks; ++b)
    {
        if (b == flipAt && flipId != nullptr) setP (p, flipId, flipTo);
        float in[1024];
        v.fill (in, bs, 44100.0);
        for (int c = 0; c < 2; ++c)
            juce::FloatVectorOperations::copy (buf.getWritePointer (c), in, bs);
        p.processBlock (buf, midi);
        for (int i = 0; i < bs; ++i) out.push_back (buf.getSample (0, i));
    }
    return out;
}

static double maxAbsDiff (const std::vector<float>& a, const std::vector<float>& b, size_t from)
{
    double m = 0.0;
    const size_t n = std::min (a.size(), b.size());
    for (size_t i = from; i < n; ++i) m = std::max (m, (double) std::abs (a[i] - b[i]));
    return m;
}

// 隣り合うサンプルの最大跳躍（プチッの目安）
static double maxStep (const std::vector<float>& v, size_t from, size_t to)
{
    double m = 0.0;
    for (size_t i = std::max<size_t> (from, 1); i + 1 < std::min (to, v.size()); ++i)
        m = std::max (m, (double) std::abs (v[i] - v[i - 1]));
    return m;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    auto autosave = gz::dataDirectory().getChildFile ("autosave.xml");
    auto backup = autosave.getSiblingFile ("autosave.bak_order");
    autosave.getParentDirectory().createDirectory();
    if (autosave.existsAsFile()) autosave.moveFileTo (backup);
    struct Restore { juce::File a, b;
        ~Restore() { a.deleteFile(); if (b.existsAsFile()) b.moveFileTo (a); } } rst { autosave, backup };
    autosave.deleteFile();

    std::printf ("順番の分岐（案C）の検証\n\n");

    const int bs = 128, blocks = 200;
    const size_t skip = 44100 / 4;

    // 既定（分岐すべてオフ）
    std::vector<float> base;
    {
        auto pStorage = std::make_unique<VocalGzzioProcessor>(); auto& p = *pStorage;
        loudSetup (p);
        p.prepareToPlay (44100.0, bs);
        base = run (p, blocks, bs);
    }

    // ---------------------------------------------------------------- [1]
    std::printf ("[1] 既定は、分岐を作る前と**1サンプルも変わらない**か\n");
    //  ここが崩れたら、この作りの意味が無い。
    //  「切り出し」だけをして呼ぶ場所は変えていないので、完全一致するはず。
    //  ※ 比較の相手は「同じ既定で走らせたもう1つの個体」。決定的であることと、
    //    分岐パラメータを明示的に false に置いても同じであることを見る。
    {
        auto pStorage = std::make_unique<VocalGzzioProcessor>(); auto& p = *pStorage;
        loudSetup (p);
        setP (p, "ord_deess", 0.0f);
        setP (p, "ord_eq",    0.0f);
        setP (p, "ord_space", 0.0f);
        p.prepareToPlay (44100.0, bs);
        auto again = run (p, blocks, bs);
        const double d = maxAbsDiff (again, base, 0);
        CHECK (d == 0.0, "既定と、分岐を明示オフにしたものが完全一致 (最大差 %.3g)", d);
    }

    // ---------------------------------------------------------------- [2]
    std::printf ("\n[2] 分岐を入れると、ちゃんと音が変わるか\n");
    struct Br { const char* id; const char* jp; void (*extra)(VocalGzzioProcessor&); };
    const Br brs[] = {
        { "ord_deess", "サ行おさえを圧縮の前へ",   nullptr },
        { "ord_eq",    "ととのえを圧縮の後へ",     nullptr },
        // 分岐3は「キャラ声」と「ひろがり」が**両方鳴っていないと**差が出ない。
        // 何も鳴っていない所で順番を入れ替えても、当然、音は同じ。
        { "ord_space", "ひろがりをキャラ声の前へ", spaceSetup },
    };
    for (auto& br : brs)
    {
        auto p0Storage = std::make_unique<VocalGzzioProcessor>(); auto& p0 = *p0Storage;                       // 比べる相手も同じ設定で作る
        loudSetup (p0); if (br.extra) br.extra (p0);
        p0.prepareToPlay (44100.0, bs);
        auto off = run (p0, blocks, bs);

        auto pStorage = std::make_unique<VocalGzzioProcessor>(); auto& p = *pStorage;
        loudSetup (p); if (br.extra) br.extra (p);
        setP (p, br.id, 1.0f);
        p.prepareToPlay (44100.0, bs);
        auto one = run (p, blocks, bs);
        const double d = maxAbsDiff (one, off, skip);
        CHECK (d > 1e-4, "%s で音が変わる (最大差 %.4f)", br.jp, d);
    }
    // 2つ同時に入れても壊れない（順番: サ行 → 圧縮 → ととのえ）
    {
        auto pStorage = std::make_unique<VocalGzzioProcessor>(); auto& p = *pStorage;
        loudSetup (p);
        setP (p, "ord_deess", 1.0f);
        setP (p, "ord_eq",    1.0f);
        p.prepareToPlay (44100.0, bs);
        auto both = run (p, blocks, bs);
        double peak = 0.0;
        for (size_t i = skip; i < both.size(); ++i) peak = std::max (peak, (double) std::abs (both[i]));
        CHECK (peak > 1e-4 && peak < 4.0, "2つ同時でも音が出て暴れない (山 %.3f)", peak);
    }

    // ---------------------------------------------------------------- [3]
    std::printf ("\n[3] 分岐を入れても申告遅延が増えないか（DAWのズレ防止）\n");
    {
        auto p0Storage = std::make_unique<VocalGzzioProcessor>(); auto& p0 = *p0Storage; loudSetup (p0); p0.prepareToPlay (44100.0, bs);
        const int lat0 = p0.getLatencySamples();
        auto p1Storage = std::make_unique<VocalGzzioProcessor>(); auto& p1 = *p1Storage; loudSetup (p1);
        setP (p1, "ord_deess", 1.0f); setP (p1, "ord_eq", 1.0f); setP (p1, "ord_space", 1.0f);
        p1.prepareToPlay (44100.0, bs);
        const int lat1 = p1.getLatencySamples();
        CHECK (lat0 == lat1, "遅延は変わらない (%d → %d サンプル)", lat0, lat1);
    }

    // ---------------------------------------------------------------- [4]
    std::printf ("\n[4] 分岐を入れても、モジュールのON/OFFは効くか\n");
    {
        // 場所を変えたほうでも「切ったら素通し」が保たれること
        auto paStorage = std::make_unique<VocalGzzioProcessor>(); auto& pa = *paStorage; loudSetup (pa);
        setP (pa, "ord_deess", 1.0f);
        pa.prepareToPlay (44100.0, bs);
        auto onA = run (pa, blocks, bs);

        auto pbStorage = std::make_unique<VocalGzzioProcessor>(); auto& pb = *pbStorage; loudSetup (pb);
        setP (pb, "ord_deess", 1.0f);
        setP (pb, gz::ModuleChain::paramId (gz::ModuleChain::Sagyo), 0.0f);
        pb.prepareToPlay (44100.0, bs);
        auto offA = run (pb, blocks, bs);
        CHECK (maxAbsDiff (onA, offA, skip) > 1e-5,
               "前へ動かしたサ行おさえも、切れば音が変わる (最大差 %.5f)",
               maxAbsDiff (onA, offA, skip));

        auto pcStorage = std::make_unique<VocalGzzioProcessor>(); auto& pc = *pcStorage; loudSetup (pc);
        setP (pc, "ord_eq", 1.0f);
        pc.prepareToPlay (44100.0, bs);
        auto onB = run (pc, blocks, bs);
        auto pdStorage = std::make_unique<VocalGzzioProcessor>(); auto& pd = *pdStorage; loudSetup (pd);
        setP (pd, "ord_eq", 1.0f);
        setP (pd, gz::ModuleChain::paramId (gz::ModuleChain::Totonoe), 0.0f);
        pd.prepareToPlay (44100.0, bs);
        auto offB = run (pd, blocks, bs);
        CHECK (maxAbsDiff (onB, offB, skip) > 1e-5,
               "後ろへ動かしたととのえも、切れば音が変わる (最大差 %.5f)",
               maxAbsDiff (onB, offB, skip));
    }

    // ---------------------------------------------------------------- [5]
    std::printf ("\n[5] 演奏中に順番を切り替えてもプチッと言わないか\n");
    //  順番の切替は**渡し（クロスフェード）を持たない**——切り替えた瞬間から
    //  新しい経路になる。モジュールのON/OFFのような 10ms の渡しは無いので、
    //  ここは「段差が入力自身の段差を大きく超えないこと」で見る。
    //  （実用上は曲中に動かす物ではないが、オートメーションに載る以上は測る）
    for (auto& br : brs)
    {
        auto pStorage = std::make_unique<VocalGzzioProcessor>(); auto& p = *pStorage;
        loudSetup (p);
        p.prepareToPlay (44100.0, bs);
        auto flip = run (p, blocks, bs, blocks / 2, br.id, 1.0f);
        const size_t at = (size_t) (blocks / 2) * (size_t) bs;
        const double around = maxStep (flip, at - 64, at + 64);
        const double normal = maxStep (flip, skip, skip + 4096);
        CHECK (around < normal * 3.0 + 0.05,
               "%s の切替時の段差 %.4f（ふだんの段差 %.4f の3倍未満）",
               br.jp, around, normal);
    }


    // ---------------------------------------------------------------- [6]
    std::printf ("\n[6] サビリフトの直しと、古いプロジェクトの互換（v3.0-c）\n");
    //  直したこと: サビリフト/エモの検出が「キャラ声」の中にあったので、
    //  **キャラ声を切るとサビリフトが残響に効かなかった**。関係が無いので直した。
    //  ただし直した時点で、キャラ声を切っている人の音は変わる。だから:
    //   ・新しく置いたときは直った動き（既定 lift_legacy = false）
    //   ・v2.12.0 までに保存されたもの（ver<3）を開いたら、昔の動きで開く
    {
        auto liftSetup = [] (VocalGzzioProcessor& p)
        {
            setP (p, "lift_amt", 100.0f);      // サビリフト全開
            setP (p, "revon", 1.0f);
            setP (p, "revmix", 50.0f);         // ひびきを出して、掛かり方の差を見る
            setP (p, "mix", 100.0f);
            setP (p, gz::ModuleChain::paramId (gz::ModuleChain::Chara), 0.0f);  // キャラ声OFF
        };

        // 直った動き（既定）
        auto pFixStorage = std::make_unique<VocalGzzioProcessor>(); auto& pFix = *pFixStorage; liftSetup (pFix);
        pFix.prepareToPlay (44100.0, bs);
        auto fixed = run (pFix, 600, bs);      // サビ判定は遅いので長めに回す

        // 昔の動き
        auto pOldStorage = std::make_unique<VocalGzzioProcessor>(); auto& pOld = *pOldStorage; liftSetup (pOld);
        setP (pOld, "lift_legacy", 1.0f);
        pOld.prepareToPlay (44100.0, bs);
        auto oldv = run (pOld, 600, bs);

        CHECK (maxAbsDiff (fixed, oldv, skip) > 1e-5,
               "キャラ声OFFでも、直した側ではサビリフトが効く (差 %.5f)",
               maxAbsDiff (fixed, oldv, skip));

        // キャラ声を入れてあるときは、直しても昔と1サンプルも変わらない
        auto pAStorage = std::make_unique<VocalGzzioProcessor>(); auto& pA = *pAStorage; liftSetup (pA);
        setP (pA, gz::ModuleChain::paramId (gz::ModuleChain::Chara), 1.0f);
        pA.prepareToPlay (44100.0, bs);
        auto onFix = run (pA, 300, bs);
        auto pBStorage = std::make_unique<VocalGzzioProcessor>(); auto& pB = *pBStorage; liftSetup (pB);
        setP (pB, gz::ModuleChain::paramId (gz::ModuleChain::Chara), 1.0f);
        setP (pB, "lift_legacy", 1.0f);
        pB.prepareToPlay (44100.0, bs);
        auto onOld = run (pB, 300, bs);
        CHECK (maxAbsDiff (onFix, onOld, 0) == 0.0,
               "キャラ声ONなら、直す前と完全一致 (最大差 %.3g)", maxAbsDiff (onFix, onOld, 0));

        // 古い保存(ver=2)を読むと、昔の動きで開く
        {
            auto srcStorage = std::make_unique<VocalGzzioProcessor>(); auto& src = *srcStorage; liftSetup (src);
            juce::MemoryBlock mb;
            src.getStateInformation (mb);
            auto xml = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize());
            const int verNow = xml != nullptr ? xml->getIntAttribute ("ver", 0) : 0;
            CHECK (verNow >= 3, "いま保存すると ver=%d（3以上）", verNow);

            xml->setAttribute ("ver", 2);      // v2.12.0 で保存されたことにする
            juce::MemoryBlock old;
            juce::AudioProcessor::copyXmlToBinary (*xml, old);

            auto dstStorage = std::make_unique<VocalGzzioProcessor>(); auto& dst = *dstStorage;
            dst.setStateInformation (old.getData(), (int) old.getSize());
            const bool legacy = dst.apvts.getRawParameterValue ("lift_legacy")->load() > 0.5f;
            CHECK (legacy, "古い保存(ver=2)を開くと、昔の動きで開く");

            auto dst2Storage = std::make_unique<VocalGzzioProcessor>(); auto& dst2 = *dst2Storage;
            dst2.setStateInformation (mb.getData(), (int) mb.getSize());
            const bool legacy2 = dst2.apvts.getRawParameterValue ("lift_legacy")->load() > 0.5f;
            CHECK (! legacy2, "新しい保存(ver=3)を開くと、直った動きのまま");
        }
    }


    // ---------------------------------------------------------------- [7]
    std::printf ("\n[7] シーン（弾き語り/トーク配信/Band）が保存されるか（v3.0-c）\n");
    //  報告:「意思を持って先に選んだのに、しばらくすると／うた自動を押すと戻る」
    //  原因は2つあった:
    //   (1) 選んだシーンがどこにも保存されておらず、画面を開き直すと必ず
    //       「トーク配信」に戻っていた
    //   (2) うた自動が、シーンの決める7つ(width/doubler/delay/revsize/revmix ほか)を
    //       上書きしていた。ボタンだけ光ったまま中身が消える＝ボタンが嘘をつく
    //  ここでは (1) を機械で見る（(2) は画面側の処理なので ui_fit 側の領分）。
    {
        auto pStorage = std::make_unique<VocalGzzioProcessor>(); auto& p = *pStorage;
        // 「弾き語り」を選んだのと同じ状態を作る
        p.apvts.state.setProperty ("ui_scene", 0, nullptr);

        juce::MemoryBlock mb;
        p.getStateInformation (mb);

        auto qStorage = std::make_unique<VocalGzzioProcessor>(); auto& q = *qStorage;
        q.setStateInformation (mb.getData(), (int) mb.getSize());
        const int back = (int) q.apvts.state.getProperty ("ui_scene", -1);
        CHECK (back == 0, "選んだシーンが保存され、開き直しても残る (%d)", back);

        // 一度も選んでいないときは -1 のまま（＝うた自動のあとに戻す対象にしない）
        auto rStorage = std::make_unique<VocalGzzioProcessor>(); auto& r = *rStorage;
        juce::MemoryBlock mb2;
        r.getStateInformation (mb2);
        auto tStorage = std::make_unique<VocalGzzioProcessor>(); auto& t = *tStorage;
        t.setStateInformation (mb2.getData(), (int) mb2.getSize());
        const int none = (int) t.apvts.state.getProperty ("ui_scene", -1);
        CHECK (none < 0, "一度も選んでいなければ「未選択」のまま (%d)", none);
    }

    // ---------------------------------------------------------------- [8]
    std::printf ("\n[8] 分岐3で「ひろがり」を前に出しても、サビリフトが死んでいないか\n");
    //  ★これが分岐3のいちばん危ない所。
    //  キャラ声は 2つの仕事を持っている:
    //    ・音を変える（ロボ声・メガホン）
    //    ・音を測る（エモ・サビリフト）
    //  そして**ひろがりは「測った結果」を使う**（残響と広がりの送り量に掛ける）。
    //  だからキャラ声を丸ごと後ろに回すと、ひろがりは 0 を読んで
    //  **サビリフトが黙る**。それでは直したはずのものをまた壊すことになる。
    //  実装では「測る所だけ先に済ませる」形にした。ここではそれを確かめる。
    {
        auto liftSpace = [] (VocalGzzioProcessor& p)
        {
            setP (p, "lift_amt", 100.0f);      // サビリフト全開
            setP (p, "revon", 1.0f);
            setP (p, "revmix", 50.0f);
            setP (p, "width", 50.0f);
            setP (p, "mix", 100.0f);
        };

        // 分岐3 ON のまま、サビリフトの有無だけを比べる。
        // 効いていれば音が変わる。黙っていれば完全一致してしまう。
        auto pOnStorage = std::make_unique<VocalGzzioProcessor>(); auto& pOn = *pOnStorage;  liftSpace (pOn);
        setP (pOn, "ord_space", 1.0f);
        pOn.prepareToPlay (44100.0, bs);
        auto withLift = run (pOn, 600, bs);

        auto pOffStorage = std::make_unique<VocalGzzioProcessor>(); auto& pOff = *pOffStorage; liftSpace (pOff);
        setP (pOff, "ord_space", 1.0f);
        setP (pOff, "lift_amt", 0.0f);         // サビリフトだけ切る
        pOff.prepareToPlay (44100.0, bs);
        auto without = run (pOff, 600, bs);

        const double d = maxAbsDiff (withLift, without, skip);
        CHECK (d > 1e-5,
               "ひろがりを前に出しても、サビリフトはひろがりに効いている (差 %.5f)", d);

        // 逆向きの確認: 分岐3 を切ったときも同じように効いていること
        // （＝「前に出したから効く」ではなく、どちらでも効く）
        auto qOnStorage = std::make_unique<VocalGzzioProcessor>(); auto& qOn = *qOnStorage;  liftSpace (qOn);  qOn.prepareToPlay (44100.0, bs);
        auto qWith = run (qOn, 600, bs);
        auto qOffStorage = std::make_unique<VocalGzzioProcessor>(); auto& qOff = *qOffStorage; liftSpace (qOff); setP (qOff, "lift_amt", 0.0f);
        qOff.prepareToPlay (44100.0, bs);
        auto qWithout = run (qOff, 600, bs);
        CHECK (maxAbsDiff (qWith, qWithout, skip) > 1e-5,
               "分岐3を切った既定でも、いままでどおり効いている (差 %.5f)",
               maxAbsDiff (qWith, qWithout, skip));
    }

    // ---------------------------------------------------------------- [9]
    std::printf ("\n[9] 分岐3を切り替えてもプチッと言わないか\n");
    {
        auto st = [] (VocalGzzioProcessor& p) { loudSetup (p); spaceSetup (p); };
        auto pStorage = std::make_unique<VocalGzzioProcessor>(); auto& p = *pStorage; st (p);
        p.prepareToPlay (44100.0, bs);
        auto v = run (p, 300, bs, 150, "ord_space", 1.0f);
        const size_t at = (size_t) 150 * (size_t) bs;
        const double near = maxStep (v, at - (size_t) bs, at + (size_t) bs * 4);
        const double calm = maxStep (v, skip, at - (size_t) bs * 4);
        CHECK (near < calm * 6.0 + 0.02,
               "切り替えの前後で跳びが暴れない (ふだん %.4f / 切替時 %.4f)", calm, near);
    }

    std::printf (gFail ? "\n== %d 件 FAIL ==\n" : "\n== 全テスト PASS ==\n", gFail);
    return gFail ? 1 : 0;
}
