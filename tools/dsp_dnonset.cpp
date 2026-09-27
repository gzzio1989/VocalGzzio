#include "TestPaths.h"
// dsp_dnonset.cpp — v2.12.0 サフサフ対策(§6-1)の検証。プラグイン本体で実測。
//
//  「De-noiseをかけて喋ると、喋りはじめがサフサフ聴こえる」:
//  高域バンドが床×2.5のしきい値で、弱い子音を「まだノイズ」と判定して削っていた。
//  修正 = 声の中心帯域が床+12dBを超えたら全帯域を一斉に開いて80ms保持。
//
//  [1] 声(低い音)が鳴っている最中の弱い高域(子音のモデル)が削られないこと
//  [2] 声のない区間のノイズは今までどおり削れていること(修正で除去が死んでいない)
//  [3] v3.1 ★「喋りはじめ」が子音のとき(さ行スタート)、その子音が揉まれないこと。
//      声判定は帯域1・2(250-5000Hz)しか見ておらず、さ行の頭(エネルギーは5kHz超)は
//      母音が来るまで「声」と認識されない。文中の子音は80ms保持に守られるので、
//      症状が「喋りはじめだけ」に出る——報告と一致。
#include "PluginProcessor.h"
#include <cstdio>
#include <cmath>
#include <vector>

static int gFail = 0;
#define CHECK(cond, ...) do { \
    if (cond) { std::printf("  PASS: " __VA_ARGS__); std::printf("\n"); } \
    else      { std::printf("  FAIL: " __VA_ARGS__); std::printf("\n"); ++gFail; } } while (0)

static void setP (VocalGzzioProcessor& p, const char* id, float v)
{
    if (auto* prm = p.apvts.getParameter (id))
        prm->setValueNotifyingHost (p.apvts.getParameterRange (id).convertTo0to1 (v));
}

static void flatten (VocalGzzioProcessor& p)
{
    // dsp_srcmode と同じ「全部平ら」。既定は mud=-3dB, harsh=-1.5dB, 息やことば等も
    // 入っているため、これを怠ると測定が汚れる(このテストの初版は+4dBの謎の
    // 増幅を「削れていない」と誤読した。正体は既定ONの機能群だった)。
    for (const char* id : { "comp1", "comp2", "ride_amt", "res_amt", "deess",
                            "seq_amount", "drive", "sustain", "cons_amt", "br_amt",
                            "ring", "hum_amt", "denoise", "makeup", "prox_amt",
                            "doubler", "width", "delay", "revmix" })
        setP (p, id, 0.0f);
    setP (p, "mud", 0.0f);
    setP (p, "harsh", 0.0f);
    setP (p, "air", 0.0f);
    setP (p, "presence", 0.0f);
    setP (p, "seq_on", 0.0f);
    setP (p, "revon", 0.0f);
    setP (p, "dly_on", 0.0f);
    setP (p, "gate", -80.0f);
}

// 6kHz あたりに寄せた「サ行っぽい」ノイズ(1次HPを2回)
struct SNoise
{
    unsigned s = 5u; float h1 = 0, h2 = 0;
    float next (float amp)
    {
        s = s * 1664525u + 1013904223u;
        const float w = (float) ((int) (s >> 9) - 4194304) / 4194304.0f;
        const float a = 0.55f;                    // ~5.6kHz @44.1k
        h1 = a * (h1 + w);  const float y1 = w - h1;
        h2 = a * (h2 + y1); return (y1 - h2) * amp;
    }
};

int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    auto autosave = gz::dataDirectory().getChildFile ("autosave.xml");
    auto backup = autosave.getSiblingFile ("autosave.bak_dnonset");
    autosave.getParentDirectory().createDirectory();
    if (autosave.existsAsFile()) autosave.moveFileTo (backup);
    struct Restore { juce::File a, b;
        ~Restore() { a.deleteFile(); if (b.existsAsFile()) b.moveFileTo (a); } } restore { autosave, backup };
    autosave.deleteFile();

    std::printf ("サフサフ対策(喋りはじめの子音を削らない)の検証\n\n");

    VocalGzzioProcessor p;
    flatten (p);                                    // まっさらにしてから
    setP (p, "dn_on", 1); setP (p, "denoise", 60);  // ノイズ除去だけを効かせる
    p.prepareToPlay (44100.0, 128);

    const int bs = 128;
    juce::AudioBuffer<float> buf (2, bs);
    juce::MidiBuffer midi;
    SNoise sn; double ph = 0.0; unsigned rs = 9u;

    // 波形: [A]2.5秒 部屋ノイズだけ(-60dB)。0.5秒目に**LEARNで床を覚える**
    //       (順応式の床は+3dB/秒でしか上がれず、起動直後の1e-6から-60dBの床まで
    //        18秒かかる。2秒待っても届かないので、製品の想定フロー=LEARNで作る)
    //       → [B]1秒 声(196Hz,-20dB) → [C]0.3秒 声+弱いサ行(-38dB) → [D]1秒 ノイズだけ
    auto roomNoise = [&rs]
    {
        rs = rs * 1664525u + 1013904223u;
        return (float) ((int) (rs >> 9) - 4194304) / 4194304.0f * 0.001f;   // -60dB
    };
    auto voice = [&ph]
    {
        double v = 0.0;
        for (int h = 1; h <= 10; ++h) v += std::sin (ph * h) / h;   // 〜1960Hz(帯域2まで届く)
        ph += 2.0 * juce::MathConstants<double>::pi * 196.0 / 44100.0;
        return (float) (0.10 * v);                  // ~-20dB
    };

    // 6kHz付近だけを取り出す測定用HP(入出力両方に同じものを掛けて比べる)
    struct Meas { float h1=0,h2=0; float hp (float x)
        { const float a=0.55f; h1=a*(h1+x); const float y1=x-h1; h2=a*(h2+y1); return y1-h2; } };
    Meas mIn, mOut;

    double eInC = 0, eOutC = 0;                     // [C] サ行区間の高域エネルギー
    double eInD = 0, eOutD = 0;                     // [D] ノイズ区間の全エネルギー
    const int nA = 110250, nB = 44100, nC = 13230, nD = 44100;   // [A]=2.5s
    const int total = nA + nB + nC + nD;

    for (int done = 0; done < total; done += bs)
    {
        if (done == 22016) p.requestDenoiseLearn();          // 0.5秒目: 床を学習(1.5秒)
        auto* L = buf.getWritePointer (0); auto* R = buf.getWritePointer (1);
        float inSamp[128];
        for (int i = 0; i < bs; ++i)
        {
            const int t = done + i;
            float v = roomNoise();
            if (t >= nA && t < nA + nB + nC) v += voice();
            if (t >= nA + nB && t < nA + nB + nC) v += sn.next (0.0126f);   // -38dB
            inSamp[i] = v; L[i] = v; R[i] = v;
        }
        p.processBlock (buf, midi);
        for (int i = 0; i < bs; ++i)
        {
            const int t = done + i;
            const float hi = mIn.hp (inSamp[i]);
            const float ho = mOut.hp (buf.getSample (0, i));
            if (t >= nA + nB + 1000 && t < nA + nB + nC)          // 過渡1000除き
            { eInC += (double) hi * hi; eOutC += (double) ho * ho; }
            if (t >= nA + nB + nC + 8820 && t < total)            // [D]後半(閉じ直した後)
            { eInD += (double) inSamp[i] * inSamp[i];
              eOutD += (double) buf.getSample (0, i) * buf.getSample (0, i); }
        }
    }

    std::printf ("[1] 声の最中の弱いサ行(-38dB)が残るか\n");
    const double keepDb = 10.0 * std::log10 (eOutC / juce::jmax (1e-20, eInC));
    CHECK (keepDb > -2.0, "高域の保持 %.2f dB (>-2dB。修正前は帯域しきい値で大きく削れる)", keepDb);

    std::printf ("\n[2] 声のない区間のノイズは削れているか\n");
    // 期待値の根拠: この合成ノイズ(一様乱数)では包絡の平均が床(最小値追従)の
    // +2〜4dBに座るため、軟膝エキスパンダーの削れは4dB前後になる(実測4.1dB。
    // 実際の部屋ノイズはもっと下がる)。ここで見たいのは深さではなく、
    // **声リンクの誤発火で開きっぱなしになっていない**こと。0dB近くなら故障。
    const double cutDb = 10.0 * std::log10 (eOutD / juce::jmax (1e-20, eInD));
    CHECK (cutDb < -6.0, "ノイズの削れ %.2f dB (<-6dB)", cutDb);

    // ---------------------------------------------------------------- [3]
    std::printf ("\n[3] 喋りはじめが「さ行」のとき、頭の子音が揉まれないか（v3.1）\n");
    //  現実の条件を作る: 部屋がやや騒がしく(-48dB)、フレーズ頭の子音(-40dB)が
    //  ノイズ床のしきい値の際に座る。ここで旧コードはエキスパンダーが
    //  「まだノイズ」と判定して部分的に削り、シャフシャフさせる。
    //  新コードは帯域3の**立ち上がりの速さ**(ゆっくり平均の2.5倍)で
    //  「子音の頭」と見抜き、全帯域を一斉に開く。
    {
        VocalGzzioProcessor q;
        flatten (q);
        setP (q, "dn_on", 1); setP (q, "denoise", 60);
        setP (q, "dn_relearn", 0);
        q.prepareToPlay (44100.0, bs);

        SNoise sn2; unsigned rs2 = 21u; double ph2 = 0.0;
        auto room2 = [&rs2]
        {
            rs2 = rs2 * 1664525u + 1013904223u;
            return (float) ((int) (rs2 >> 9) - 4194304) / 4194304.0f * 0.004f;  // -48dB
        };
        auto voice2 = [&ph2]
        {
            double v = 0.0;
            for (int h = 1; h <= 10; ++h) v += std::sin (ph2 * h) / h;
            ph2 += 2.0 * juce::MathConstants<double>::pi * 196.0 / 44100.0;
            return (float) (0.10 * v);
        };
        //  [A]2.5秒 部屋のみ(0.5秒目にLEARN) → [B]0.9秒 静けさ →
        //  [F]0.15秒 さ行だけ(-40dB) → [G]0.5秒 声
        const int qA = 110250, qB = 39690, qF = 6615, qG = 22050;
        const int qTotal = qA + qB + qF + qG;
        Meas mIn2, mOut2;
        double eInF = 0, eOutF = 0;
        for (int done = 0; done < qTotal; done += bs)
        {
            if (done == 22016) q.requestDenoiseLearn();
            auto* L = buf.getWritePointer (0); auto* R = buf.getWritePointer (1);
            float inSamp[128];
            for (int i = 0; i < bs; ++i)
            {
                const int t = done + i;
                float v = room2();
                if (t >= qA + qB && t < qA + qB + qF) v += sn2.next (0.01f);   // -40dB
                if (t >= qA + qB + qF)                v += voice2();
                inSamp[i] = v; L[i] = v; R[i] = v;
            }
            q.processBlock (buf, midi);
            for (int i = 0; i < bs; ++i)
            {
                const int t = done + i;
                if (t >= qA + qB + 180 && t < qA + qB + qF)      // 立ち上がり4ms除き
                {
                    const float hi = mIn2.hp (inSamp[i]);
                    const float ho = mOut2.hp (buf.getSample (0, i));
                    eInF += (double) hi * hi; eOutF += (double) ho * ho;
                }
            }
        }
        const double keepF = 10.0 * std::log10 (eOutF / juce::jmax (1e-20, eInF));
        CHECK (keepF > -2.0, "頭の子音の保持 %.2f dB (>-2dB)", keepF);
    }

    std::printf (gFail ? "\n== %d 件 FAIL ==\n" : "\n== 全テスト PASS ==\n", gFail);
    return gFail ? 1 : 0;
}
