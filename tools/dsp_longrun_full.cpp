// dsp_longrun_full.cpp — 「一定時間たつと音質が劣化する」報告の再現・回帰検査（本体まるごと）
//
//  まったく同じ4秒のフレーズ（歌＋子音＋息継ぎ＋部屋の雑音）を延々と繰り返し流し、
//  序盤（ウォームアップ後）と 2分 / 5分 / 10分 後の「同じフレーズに対する出力」を比べる。
//  処理が時間不変なら出力はほぼ同じになる。時間とともにズレる量を数値で出す。
//   ・相関（波形がどれだけ同じか）
//   ・音量差(dB)
//   ・高域(4kHz+)比の差(dB) … こもり・ザラつきの指標
//   ・非有限値・ピーク
#include "PluginProcessor.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <string>

static int gFail = 0;
#define CHECK(cond, ...) do { \
    if (cond) { std::printf ("  PASS: " __VA_ARGS__); std::printf ("\n"); } \
    else      { std::printf ("  FAIL: " __VA_ARGS__); std::printf ("\n"); ++gFail; } } while (0)

static void setP (VocalGzzioProcessor& p, const char* id, float v)
{
    if (auto* prm = p.apvts.getParameter (id))
        prm->setValueNotifyingHost (p.apvts.getParameterRange (id).convertTo0to1 (v));
}

static constexpr double kSR = 48000.0;
static constexpr int    kBS = 256;
static constexpr double kPhraseSec = 4.0;
static constexpr long   kPhraseLen = (long) (kPhraseSec * kSR);

// 毎フレーズまったく同じ波形を返す（フレーズ内の位置だけで決まる）
static float computePhraseSample (long idxInPhrase)
{
    const double t = (double) idxInPhrase / kSR;
    // 3つの音（わずかに外した音程）＋ビブラート、3.2秒歌って0.8秒休む
    static const double notes[3] = { 218.0, 247.5, 293.0 };
    const int ni = (int) std::fmin (2.0, t / 1.0667);
    double s = 0.0;
    if (t < 3.2)
    {
        const double f0 = notes[ni];
        const double env = std::fmin (1.0, std::fmin (t * 20.0, (3.2 - t) * 20.0));
        const double vib = 0.006 * std::sin (2.0 * M_PI * 5.5 * t);
        for (int h = 1; h <= 18; ++h)
        {
            const double fh = f0 * h; if (fh > kSR * 0.45) break;
            // 位相はフレーズ内時間から直接（毎回同一）
            s += std::sin (2.0 * M_PI * fh * t + h * 0.3 + h * vib * 30.0) / (h * 1.2);
        }
        s *= 0.10 * env;
        // 子音（各音の頭 40ms に帯域ノイズ）
        const double tn = std::fmod (t, 1.0667);
        if (tn < 0.04)
        {
            unsigned r = (unsigned) (idxInPhrase * 2654435761u);
            r ^= r >> 13; r *= 1274126177u; r ^= r >> 16;
            s += ((double) (r & 0xffff) / 32768.0 - 1.0) * 0.05;
        }
    }
    else if (t < 3.65)
    {
        // ささやき・語尾の微弱有声テール（-54dBFS付近: Upward Lifterで浮上する領域）
        const double f0 = notes[2];
        const double tailEnv = (3.65 - t) / 0.45;
        s += 0.0020 * tailEnv * (std::sin (2.0 * M_PI * f0 * t) + 0.5 * std::sin (4.0 * M_PI * f0 * t));
    }
    // 部屋の雑音（-60dB 程度・決定論的）
    unsigned q = (unsigned) (idxInPhrase * 2246822519u + 12345u);
    q ^= q >> 15; q *= 2654435761u; q ^= q >> 13;
    s += ((double) (q & 0xffff) / 32768.0 - 1.0) * 0.001;
    return (float) s;
}

static const std::vector<float>& getPhraseTable()
{
    static std::vector<float> tbl = [] {
        std::vector<float> v (kPhraseLen);
        for (long i = 0; i < kPhraseLen; ++i) v[(size_t) i] = computePhraseSample (i);
        return v;
    }();
    return tbl;
}

static inline float phraseSample (long idxInPhrase)
{
    return getPhraseTable()[(size_t) idxInPhrase];
}

struct Stats { double corr, rmsDb, hfDb; };

static Stats compare (const std::vector<float>& a, const std::vector<float>& b)
{
    double ab = 0, aa = 1e-20, bb = 1e-20, ha = 1e-20, hb = 1e-20, la = 1e-20, lb = 1e-20;
    // 簡易高域: 1次差分のエネルギー(高域ほど大きい)
    for (size_t i = 1; i < a.size(); ++i)
    {
        ab += (double) a[i] * b[i]; aa += (double) a[i] * a[i]; bb += (double) b[i] * b[i];
        const double da = a[i] - a[i - 1], db = b[i] - b[i - 1];
        ha += da * da; hb += db * db;
    }
    la = aa; lb = bb;
    return { ab / std::sqrt (aa * bb),
             10.0 * std::log10 (bb / aa),
             10.0 * std::log10 ((hb / lb) / (ha / la)) };
}

static void runCase (const char* name, void (*setup) (VocalGzzioProcessor&), double minutes)
{
    std::printf ("\n[%s] %.0f分ぶん\n", name, minutes);
    std::fflush (stdout);
    VocalGzzioProcessor p;
    setup (p);
    p.prepareToPlay (kSR, kBS);
    juce::AudioBuffer<float> buf (2, kBS);
    juce::MidiBuffer midi;
    const long long phraseLen = (long long) (kPhraseSec * kSR);
    const long long total = (long long) (minutes * 60.0 * kSR);
    // 比べるフレーズ番号: 序盤ウォームアップ後(15番=60秒後、短時間指定時は3番=12秒後)を基準に、
    // 2分・5分・1時間・6時間・12.5時間・終盤を比較
    //  ★12.5時間 … 48kHz で 32bit のサンプル数カウンタが溢れる(2^31 ≒ 12.43時間)直後
    const long long basePhrase = (minutes >= 5.0) ? 15 : 3;
    struct Mark { long long phrase; std::string label; };
    std::vector<Mark> marks = { { basePhrase, "基準" } };
    auto addMark = [&] (double sec, const char* lab)
    {
        const long long ph = (long long) (sec / kPhraseSec);
        if (ph > basePhrase && ph < (long long) (minutes * 60.0 / kPhraseSec) - 1) marks.push_back ({ ph, lab });
    };
    addMark (120.0, "2分後"); addMark (300.0, "5分後"); addMark (3600.0, "1時間後");
    addMark (4.0 * 3600.0, "4時間後"); addMark (6.0 * 3600.0, "6時間後"); addMark (12.5 * 3600.0, "12.5時間後(32bit溢れ直後)");
    marks.push_back ({ (long long) (minutes * 60.0 / kPhraseSec) - 1, "終盤" });
    std::vector<std::vector<float>> caps (marks.size());
    std::vector<bool> printed (marks.size(), false);
    long long pos = 0; bool finite = true; float peak = 0.0f;
    double nextReport = 3600.0;
    while (pos < total)
    {
        for (int i = 0; i < kBS; ++i)
        {
            const float x = phraseSample ((long) ((pos + i) % phraseLen));
            buf.setSample (0, i, x); buf.setSample (1, i, x);
        }
        p.processBlock (buf, midi);
        for (int i = 0; i < kBS; ++i)
        {
            const float y = buf.getSample (0, i);
            if (! std::isfinite (y)) finite = false;
            peak = std::max (peak, std::abs (y));
            const long long g = pos + i, ph = g / phraseLen;
            for (size_t m = 0; m < marks.size(); ++m)
            {
                if (ph == marks[m].phrase)
                {
                    caps[m].push_back (y);
                    if (m > 0 && ! printed[m] && caps[m].size() == (size_t) phraseLen && caps[0].size() == (size_t) phraseLen)
                    {
                        printed[m] = true;
                        const auto s = compare (caps[0], caps[m]);
                        std::printf ("  %s: 相関 %.4f  音量差 %+.2f dB  高域比差 %+.2f dB\n", marks[m].label.c_str(), s.corr, s.rmsDb, s.hfDb);
                        std::fflush (stdout);
                    }
                }
            }
        }
        pos += kBS;
        if ((double) pos / kSR >= nextReport)
        {
            std::printf ("  … %.0f時間経過\n", nextReport / 3600.0);
            std::fflush (stdout);
            nextReport += 3600.0;
        }
    }
    double worstCorr = 1.0, worstRms = 0.0, worstHf = 0.0;
    for (size_t m = 1; m < marks.size(); ++m)
    {
        if (caps[m].size() != caps[0].size() || caps[0].empty()) continue;
        const auto s = compare (caps[0], caps[m]);
        if (! printed[m])
            std::printf ("  %s: 相関 %.4f  音量差 %+.2f dB  高域比差 %+.2f dB\n", marks[m].label.c_str(), s.corr, s.rmsDb, s.hfDb);
        worstCorr = std::min (worstCorr, s.corr);
        worstRms = std::max (worstRms, std::abs (s.rmsDb));
        worstHf  = std::max (worstHf,  std::abs (s.hfDb));
    }
    CHECK (finite, "%s: 非有限値なし (ピーク %.2f)", name, peak);
    CHECK (worstRms < 1.0, "%s: 時間がたっても音量が変わらない (最大 %.2f dB)", name, worstRms);
    CHECK (worstHf < 1.0, "%s: 時間がたっても音色(高域比)が変わらない (最大 %.2f dB)", name, worstHf);
    CHECK (worstCorr > 0.90, "%s: 時間がたっても同じ入力に同じ出力 (相関最小 %.4f)", name, worstCorr);
    std::fflush (stdout);
}

static void setupFull (VocalGzzioProcessor& p)
{
    // 配信でよくある「全部入り」: ピッチ補正＋ハモリ＋ひびき＋ノイズ除去＋ディエッサー
    setP (p, "at_on", 1.0f); setP (p, "jn_on", 1.0f); setP (p, "jn_harm", 6.0f);
    setP (p, "revon", 1.0f); setP (p, "revmix", 30.0f);
    setP (p, "dn_on", 1.0f); setP (p, "denoise", 60.0f);
    setP (p, "ds_on", 1.0f); setP (p, "deess", 60.0f);
}

int main (int argc, char** argv)
{
    // 使い方: dsp_longrun_full [ケース番号 0..5 / -1=全部] [分]
    //   既定は全ケース5.2分(CTest 300秒上限内)。24時間検証は「dsp_longrun_full 1 1440」のように個別に回す。
    const int    which = argc > 1 ? std::atoi (argv[1]) : -1;
    const double mins  = argc > 2 ? std::atof (argv[2]) : 5.2;
    std::printf ("本体まるごとの長時間劣化検査 @ %.0f Hz / block %d\n", kSR, kBS);
    struct Case { const char* name; void (*fn) (VocalGzzioProcessor&); };
    const Case cases[] = {
        { "既定",       [] (VocalGzzioProcessor&) {} },
        { "ピッチ補正", [] (VocalGzzioProcessor& p) { setP (p, "at_on", 1.0f); } },
        { "ボイス変換", [] (VocalGzzioProcessor& p) { setP (p, "vc_on", 1.0f); setP (p, "vc_pitch", 4.0f); setP (p, "vc_form", 2.8f); } },
        { "ハモリ",     [] (VocalGzzioProcessor& p) { setP (p, "jn_on", 1.0f); setP (p, "jn_harm", 1.0f); } },
        { "ボイス変換+ピッチ補正", [] (VocalGzzioProcessor& p) { setP (p, "vc_on", 1.0f); setP (p, "vc_pitch", -4.0f); setP (p, "vc_form", -2.5f); setP (p, "at_on", 1.0f); } },
        { "全部入り",   setupFull },
        { "ノイズ除去単体", [] (VocalGzzioProcessor& p) {
            setP (p, "dn_on", 1.0f); setP (p, "denoise", 75.0f); setP (p, "dn_relearn", 1.0f);
        } },
        { "おそうじ(ジー音+ゲート+ポップ+リップ)", [] (VocalGzzioProcessor& p) {
            setP (p, "hum_amt", 80.0f); setP (p, "gate_on", 1.0f); setP (p, "gate", -48.0f);
            setP (p, "pop_amt", 70.0f); setP (p, "lip_amt", 70.0f);
        } },
        { "ととのえ+距離ならし", [] (VocalGzzioProcessor& p) {
            setP (p, "prox_amt", 70.0f); setP (p, "lowcut", 120.0f);
            setP (p, "mud", -6.0f); setP (p, "harsh", -4.0f); setP (p, "res_amt", 65.0f);
        } },
        { "音量そろえ+サ行おさえ", [] (VocalGzzioProcessor& p) {
            setP (p, "comp1", 60.0f); setP (p, "comp2", 55.0f);
            setP (p, "seq_on", 1.0f); setP (p, "seq_amount", 60.0f);
            setP (p, "ride_amt", 65.0f); setP (p, "ds_on", 1.0f); setP (p, "deess", 65.0f);
            setP (p, "crush_on", 1.0f);
        } },
        { "音色づくり+Argon", [] (VocalGzzioProcessor& p) {
            setP (p, "cons_amt", 65.0f); setP (p, "presence", 4.0f); setP (p, "air", 4.0f);
            setP (p, "br_amt", 50.0f); setP (p, "ring", 55.0f);
            setP (p, "drive", 35.0f); setP (p, "sustain", 40.0f);
            setP (p, "argon_velvet", 50.0f); setP (p, "argon_focus", 50.0f);
            setP (p, "argon_silk", 50.0f); setP (p, "argon_halo", 50.0f);
        } },
        { "キャラ声+空間フル(へや残響+やまびこ+エモ+サビリフト)", [] (VocalGzzioProcessor& p) {
            setP (p, "emo_amt", 60.0f); setP (p, "lift_amt", 60.0f);
            setP (p, "dly_on", 1.0f); setP (p, "delay", 35.0f);
            setP (p, "revon", 1.0f); setP (p, "revmix", 35.0f); setP (p, "rev_type", 10.0f);
            setP (p, "duck", 50.0f);
        } },
        { "v5.3.0フル(ささやき浮上エアー+オートスイートスポット+骨伝導返り+部屋鳴り除去+息継ぎ+名機マイク+ピッチ)", [] (VocalGzzioProcessor& p) {
            setupFull (p);
            p.setUpwardAirOn (true);
            p.setPitchSweetSpotOn (true);
            p.setSingerCueMonitorOn (true);
            p.setRoomDereverbOn (true);
            p.setBreathControlOn (true);
            p.setMicModelMode (1);
            p.setLipDeclickOn (true);
            p.setBgmUnmaskOn (true);
            p.setBgmVocalRiderOn (true);
        } },
    };
    const int defaultCount = 6;
    for (int i = 0; i < (int) (sizeof (cases) / sizeof (cases[0])); ++i)
        if ((which < 0 && i < defaultCount) || which == i || (which == -2))
            runCase (cases[i].name, cases[i].fn, mins);
    std::printf (gFail ? "\n== %d 件 FAIL ==\n" : "\n== 全テスト PASS ==\n", gFail);
    return gFail ? 1 : 0;
}
