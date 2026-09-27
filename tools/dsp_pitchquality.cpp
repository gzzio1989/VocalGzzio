// 音程検出・移調・補正先の回帰検査。合成音を使うため実声の聴感評価とは分ける。
// JUCEに依存せず、Windows/macOSの標準C++17で実行できる。
#include "../Source/PitchDetector.h"
#include "../Source/PitchCorrection.h"
#include "../Source/VoiceShifter.h"
#include <cstdio>
#include <limits>
#include <random>

namespace
{
constexpr double pi = 3.14159265358979323846;
int failures = 0;
void check (bool ok, const char* name)
{
    std::printf ("%s: %s\n", ok ? "合格" : "不合格", name);
    if (! ok) ++failures;
}
double cents (double actual, double expected)
{
    return actual > 0.0 ? 1200.0 * std::log2 (actual / expected) : 99999.0;
}
std::vector<float> tone (double sr, double hz, double seconds, bool harmonics = false)
{
    std::vector<float> out ((size_t) std::lround (sr * seconds));
    for (size_t i = 0; i < out.size(); ++i)
    {
        const double p = 2.0 * pi * hz * (double) i / sr;
        out[i] = (float) (harmonics ? 0.02 * std::sin (p) + 0.20 * std::sin (2.0*p)
                                      + 0.10 * std::sin (3.0*p) : 0.3 * std::sin (p));
    }
    return out;
}
float detect (gz::PitchDetector& detector, const std::vector<float>& input, int block)
{
    for (size_t i = 0; i < input.size(); i += (size_t) block)
        detector.process (input.data() + i, (int) std::min ((size_t) block, input.size() - i));
    return detector.lastFreq();
}
void detectorTests()
{
    double worst = 0.0, partitionDifference = 0.0;
    for (double sr : {44100.0, 48000.0, 96000.0})
        for (double hz : {82.4069, 110.0, 146.8324, 195.9977, 246.9417, 329.6276, 440.0, 880.0})
        {
            const auto input = tone (sr, hz, 0.18, true);
            float first = 0.0f;
            for (int block : {1, 64, 124, 512, 2048})
            {
                gz::PitchDetector pd; pd.prepare (sr);
                const float got = detect (pd, input, block);
                worst = std::max (worst, std::abs (cents (got, hz)));
                if (first == 0.0f) first = got;
                partitionDifference = std::max (partitionDifference, std::abs ((double) got - first));
            }
        }
    std::printf ("  弱い基音・ギター6弦〜高音: 最大誤差 %.3f セント、ブロック分割差 %.8f Hz\n",
                 worst, partitionDifference);
    check (worst < 3.0, "倍音が強い単音の基本周波数を検出");
    check (partitionDifference < 0.0001, "検出周期がホストのブロック長に左右されない");

    gz::PitchDetector pd; pd.prepare (48000.0);
    double transitionWorst = 0.0;
    for (double hz : {110.0, 220.0, 440.0, 220.0, 110.0, 82.4069, 329.6276})
    {
        const auto found = detect (pd, tone (48000, hz, 0.20, true), 124);
        transitionWorst = std::max (transitionWorst, std::abs (cents (found, hz)));
        if (std::abs (cents (found, hz)) >= 3.0)
            std::printf ("  音符移行 %.2f Hz → 検出 %.3f Hz\n", hz, found);
    }
    check (transitionWorst < 3.0, "音符・オクターブの切り替え後に正しい音へ追従");

    std::vector<float> input (9600, 0.0f);
    const bool silent = detect (pd, input, 124) == 0.0f;
    std::fill (input.begin(), input.end(), 0.25f);
    const bool dc = detect (pd, input, 124) == 0.0f;
    std::mt19937 rng (238);
    for (auto& v : input) v = (float) ((double) rng() / rng.max() * 0.4 - 0.2);
    const bool noise = detect (pd, input, 124) == 0.0f;
    std::fill (input.begin(), input.end(), std::numeric_limits<float>::quiet_NaN());
    const bool nan = detect (pd, input, 124) == 0.0f;
    const double recovered = detect (pd, tone (48000, 220.0, 0.20), 124);
    std::printf ("  無音 %d、直流 %d、雑音 %d、非有限 %d、復帰 %.3f Hz\n", silent, dc, noise, nan, recovered);
    check (silent && dc && noise && nan && std::abs (cents (recovered, 220.0)) < 3.0,
           "無音・直流・雑音・非有限入力を音程にせず、その後復帰");
}

// 検出器と独立した測定。期待周波数の複素相関と、前後半の位相差を使う。
// 周波数の大きな誤りは相関エネルギー、微小なずれは位相差で検出する。
struct ToneMeasurement { double errorCents, coherence, gain; };
ToneMeasurement measureTone (const std::vector<float>& out, double sr, double wanted)
{
    const int first = (int) (0.25 * sr);
    const int half = (int) (0.20 * sr);
    std::complex<double> sums[2] {};
    double energy = 0.0;
    for (int j = 0; j < 2; ++j)
        for (int i = 0; i < half; ++i)
        {
            const int n = first + j * half + i;
            const double p = -2.0 * pi * wanted * n / sr;
            const double value = out[(size_t) n];
            sums[j] += value * std::complex<double> (std::cos (p), std::sin (p));
            energy += value * value;
        }
    const double phaseDifference = std::arg (sums[1] * std::conj (sums[0]));
    const double measuredHz = wanted + phaseDifference * sr / (2.0 * pi * half);
    const double projected = (std::norm (sums[0]) + std::norm (sums[1])) * 2.0 / half;
    return { cents (measuredHz, wanted), projected / std::max (energy, 1.0e-20),
             std::sqrt (energy / (2.0 * half)) / (0.3 / std::sqrt (2.0)) };
}
void shifterTests()
{
    double worstIdentity = 0.0;
    bool latencyOk = true;
    for (double sr : {44100.0, 48000.0, 96000.0})
        for (int order : {0, 9, 10})
            for (float mix : {0.0f, 0.5f, 1.0f})
            {
                gz::VoiceShifter sh; sh.prepare (sr); if (order != 0) sh.setWindow (order); sh.setParams (0, 0, mix);
                auto input = tone (sr, 231.7, 0.16, true);
                std::mt19937 rng (17);
                for (auto& v : input) v += (float) ((double) rng() / rng.max() * 0.2 - 0.1);
                auto output = input;
                for (size_t i = 0; i < output.size(); i += 124)
                    sh.processBlock (output.data() + i, (int) std::min ((size_t) 124, output.size() - i));
                for (size_t i = (size_t) std::max (3072, sh.latencySamples() + 1024); i < output.size(); ++i)
                    worstIdentity = std::max (worstIdentity,
                         std::abs ((double) output[i] - input[i - (size_t) sh.latencySamples()]));
                sh.reset();
                std::vector<float> impulse (8192, 0.0f); impulse[2048] = 0.5f;
                sh.processBlock (impulse.data(), (int) impulse.size());
                const auto largest = std::max_element (impulse.begin(), impulse.end());
                latencyOk = latencyOk && (largest - impulse.begin() == 2048 + sh.latencySamples())
                            && std::abs (*largest - 0.5f) < 0.00001f;
            }
    std::printf ("  補正ゼロ時の原音との最大差 %.8f\n", worstIdentity);
    check (worstIdentity < 0.00002, "補正ゼロ・中間ミックスで原音の位相と音量を保持");
    check (latencyOk, "実際の移調遅延と原音遅延・申告値が一致");

    double worstCents = 0.0, lowestCoherence = 1.0, lowestGain = 10.0, highestGain = 0.0;
    for (double sr : {44100.0, 48000.0, 96000.0})
        for (double hz : {82.4069, 220.0, 440.0})
            for (float shift : {-12.0f, -3.0f, -0.4f, 0.4f, 3.0f, 12.0f})
            {
                gz::VoiceShifter sh; sh.prepare (sr); sh.setParams (shift, 0, 1);
                auto output = tone (sr, hz, 0.66);
                sh.processBlock (output.data(), (int) output.size());
                const auto m = measureTone (output, sr, hz * std::pow (2.0, shift / 12.0));
                worstCents = std::max (worstCents, std::abs (m.errorCents));
                lowestCoherence = std::min (lowestCoherence, m.coherence);
                lowestGain = std::min (lowestGain, m.gain);
                highestGain = std::max (highestGain, m.gain);
                if (std::abs (m.errorCents) >= 4.0 || m.coherence <= 0.65 || m.gain <= 0.65 || m.gain >= 1.35)
                    std::printf ("  移調条件 %.0f Hz / %.2f Hz / %.1f 半音: 誤差 %.3f、純音比 %.3f、音量比 %.3f\n",
                                 sr, hz, shift, m.errorCents, m.coherence, m.gain);
            }
    std::printf ("  移調誤差最大 %.3f セント、純音比最小 %.3f、音量比 %.3f〜%.3f\n",
                 worstCents, lowestCoherence, lowestGain, highestGain);
    check (worstCents < 4.0 && lowestCoherence > 0.65, "微小補正〜上下1オクターブの周波数と安定性");
    check (lowestGain > 0.65 && highestGain < 1.35, "移調時の音量が極端に落ちたり増えたりしない");

    gz::VoiceShifter a, b; a.prepare (48000); b.prepare (48000);
    a.setParams (3, 1, 0.7f); b.setParams (3, 1, 0.7f);
    auto whole = tone (48000, 220, 0.2, true), split = whole;
    a.processBlock (whole.data(), (int) whole.size());
    for (size_t i = 0; i < split.size(); i += 61)
        b.processBlock (split.data() + i, (int) std::min ((size_t) 61, split.size() - i));
    check (whole == split, "波形加工がホストのブロック分割に左右されない");
}

void correctionTests()
{
    gz::PitchCorrection correction;
    for (int i = 0; i < 100; ++i) correction.target (60.3f, 0.005f, 0, gz::scale::Chromatic, 80, 1);
    float afterJump = 0.0f;
    for (int i = 0; i < 4; ++i) afterJump = correction.target (67.2f, 0.005f, 0, gz::scale::Chromatic, 80, 1);
    check (std::abs (afterJump + 0.2f) < 0.01f, "自然補正が次の音に古い中心音を持ち越さない");

    correction.reset();
    correction.target (60.4f, 0.005f, 0, gz::scale::Chromatic, 0, 1);
    float largestCorrected = 0.0f;
    for (int i = 0; i < 100; ++i)
    {
        const float midi = (i & 1) ? 60.49f : 60.51f;
        largestCorrected = std::max (largestCorrected,
              std::abs (midi + correction.target (midi, 0.005f, 0, gz::scale::Chromatic, 0, 1) - 60.0f));
    }
    check (largestCorrected < 0.001f, "音符境界の微小な揺れで補正先が交互に飛ばない");

    correction.reset();
    double minimum = 999.0, maximum = -999.0;
    for (int i = 0; i < 800; ++i)
    {
        const float midi = (float) (60.2 + 0.12 * std::sin (2.0 * pi * 5.0 * i * 0.005));
        const double corrected = midi + correction.target (midi, 0.005f, 0, gz::scale::Chromatic, 80, 1);
        if (i > 400) { minimum = std::min (minimum, corrected); maximum = std::max (maximum, corrected); }
    }
    std::printf ("  自然補正後のビブラート幅 %.2f セント（入力24セント）\n", 100.0 * (maximum - minimum));
    check (maximum - minimum > 0.18 && maximum - minimum < 0.28,
           "自然補正で持続音のビブラートを残す");
    correction.target (0, 0.1f, 0, gz::scale::Chromatic, 80, 1);
    check (std::abs (correction.target (72.25f, 0.005f, 0, gz::scale::Chromatic, 80, 1) + 0.25f) < 0.001f,
           "無声区間の後は新しい歌い出しとして補正");
    check (correction.target (72.25f, 0.005f, 0, gz::scale::Chromatic, 80, 0) == 0.0f,
           "補正量ゼロで補正値もゼロ");
}
}

int main()
{
    detectorTests();
    shifterTests();
    correctionTests();
    std::printf ("\n不合格: %d 件\n", failures);
    return failures == 0 ? 0 : 1;
}
