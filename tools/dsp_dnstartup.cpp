#include "TestPaths.h"
#include "PluginProcessor.h"
#include <cmath>
#include <cstdio>
#include <memory>
#include <array>

// 実機報告に合わせ、静かなノイズをあらかじめ学習した前提を置かない。
// 本体の入出力で、起動直後・大きい白色ノイズ・保存復元までを確認する。
static int failures = 0;
#define CHECK(ok, ...) do { std::printf ((ok) ? "合格: " : "不合格: "); \
    std::printf (__VA_ARGS__); std::printf ("\n"); if (!(ok)) ++failures; } while (false)

static void setP (VocalGzzioProcessor& p, const char* id, float value)
{
    auto* parameter = p.apvts.getParameter (id);
    if (parameter) parameter->setValueNotifyingHost (p.apvts.getParameterRange (id).convertTo0to1 (value));
}

static void setup (VocalGzzioProcessor& p, double sr, int block, int channels)
{
    auto layout = p.getBusesLayout();
    layout.inputBuses.set (0, channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
    layout.outputBuses.set (0, layout.inputBuses[0]);
    p.setBusesLayout (layout);
    for (int m = 0; m < gz::ModuleChain::Count; ++m)
        setP (p, gz::ModuleChain::paramId (m), m == gz::ModuleChain::Souji ? 1.0f : 0.0f);
    for (auto* id : { "gate_on", "hum_amt", "pop_amt", "lip_amt", "prox_amt", "in_gain", "makeup", "dn_relearn", "crush_on" })
        setP (p, id, 0);
    setP (p, "src_mode", 0); setP (p, "mix", 100);
    setP (p, "dn_on", 1); setP (p, "denoise", 60);
    p.clearDenoiseLearn();
    p.prepareToPlay (sr, block);
}

struct Noise
{
    unsigned state = 789123;
    float next() { state = state * 1664525u + 1013904223u; return (float) (state >> 8) / 8388608.0f - 1.0f; }
};

static double run (VocalGzzioProcessor& p, double sr, int block, int channels,
                   float noiseDb, bool manualLearn, bool voice = false,
                   double voiceHz = 196, bool oppositeRight = false, int noiseColour = 0)
{
    Noise noise;
    float pink[3] {}, brown = 0;
    juce::AudioBuffer<float> buffer (channels, block);
    juce::MidiBuffer midi;
    const int total = (int) (sr * 4);
    const float amplitude = juce::Decibels::decibelsToGain (noiseDb) * std::sqrt (3.0f);
    bool requested = false;
    double inputEnergy = 0, outputEnergy = 0;
    for (int start = 0; start < total; start += block)
    {
        if (manualLearn && ! requested && start >= sr * 0.4) { p.requestDenoiseLearn(); requested = true; }
        const int count = juce::jmin (block, total - start);
        buffer.setSize (channels, count, false, false, true);
        for (int i = 0; i < count; ++i)
        {
            const double t = (start + i) / sr;
            const float white = noise.next();
            float coloured = white;
            if (noiseColour == 1)
            {
                pink[0] = 0.99765f * pink[0] + white * 0.0990460f;
                pink[1] = 0.96300f * pink[1] + white * 0.2965164f;
                pink[2] = 0.57000f * pink[2] + white * 1.0526913f;
                coloured = (pink[0] + pink[1] + pink[2] + white * 0.1848f) * 0.2f;
            }
            else if (noiseColour == 2)
            {
                brown += 0.01f * (white - brown);
                coloured = brown * 10;
            }
            float x = amplitude * coloured;
            if (voice)
            {
                x *= 0.03f;
                for (int h = 1; h <= 6; ++h)
                    x += amplitude * 0.65f * (float) std::sin (2 * juce::MathConstants<double>::pi * voiceHz * h * t) / h;
            }
            for (int ch = 0; ch < channels; ++ch)
                buffer.setSample (ch, i, ch == 1 && oppositeRight ? -x : x);
            if (start + i >= sr * 3) inputEnergy += (double) x * x;
        }
        p.processBlock (buffer, midi);
        for (int i = 0; i < count; ++i)
            if (start + i >= sr * 3)
            {
                const double x = buffer.getSample (0, i);
                outputEnergy += x * x;
            }
    }
    return 10 * std::log10 ((outputEnergy + 1e-30) / (inputEnergy + 1e-30));
}

// アコギのコードは、単一の基本周期を持たない。単音を守るだけでは足りない。
// 倍音を持つ各弦を重ね、同じ和音を伸ばしたときに床へ取り込まれないことを測る。
static double runChord (VocalGzzioProcessor& p, const std::array<double, 6>& frequencies,
                        float levelDb, bool manualLearn, double sr = 48000,
                        bool oppositeRight = false, bool roomNoise = false)
{
    constexpr int block = 128;
    const int total = (int) sr * 4;
    Noise noise;
    juce::AudioBuffer<float> buffer (2, block);
    juce::MidiBuffer midi;
    const float amplitude = juce::Decibels::decibelsToGain (levelDb);
    double inputEnergy = 0, outputEnergy = 0;
    bool requested = false;
    for (int start = 0; start < total; start += block)
    {
        if (manualLearn && ! requested && start >= sr * 0.4)
        { p.requestDenoiseLearn(); requested = true; }
        const int count = juce::jmin (block, total - start);
        buffer.setSize (2, count, false, false, true);
        for (int i = 0; i < count; ++i)
        {
            const double t = (start + i) / sr;
            double sum = 0;
            int strings = 0;
            for (double frequency : frequencies)
                if (frequency > 0)
                {
                    ++strings;
                    for (int harmonic = 1; harmonic <= 6; ++harmonic)
                        sum += std::sin (2 * juce::MathConstants<double>::pi * frequency * harmonic * t) / harmonic;
                }
            const float x = amplitude * ((float) (sum / strings) + (roomNoise ? noise.next() * 0.03f : 0));
            buffer.setSample (0, i, x); buffer.setSample (1, i, oppositeRight ? -x : x);
            if (start + i >= sr * 3) inputEnergy += (double) x * x;
        }
        p.processBlock (buffer, midi);
        for (int i = 0; i < count; ++i)
            if (start + i >= sr * 3)
            {
                const double x = buffer.getSample (0, i);
                outputEnergy += x * x;
            }
    }
    return 10 * std::log10 ((outputEnergy + 1e-30) / (inputEnergy + 1e-30));
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    auto fresh = [] { gz::dataDirectory().getChildFile ("autosave.xml").deleteFile(); };
    // AU/単体起動でも共通の音声本体を通る。入力の用途、モノ/ステレオ、
    // ホストのブロック長が違っても除去量が実際の出力へ反映されること。
    for (int source = 0; source < 4; ++source)
        for (int channels : { 1, 2 })
            for (int block : { 32, 1024 })
            {
                fresh();
                auto p = std::make_unique<VocalGzzioProcessor>();
                setup (*p, 48000, block, channels);
                setP (*p, "src_mode", (float) source);
                const double change = run (*p, 48000, block, channels, -42, false);
                CHECK (change < -4, "用途%d・%d ch・%dサンプルでもノイズを抑える（%.2f dB）",
                       source, channels, block, change);
            }
    // 学習と音を変えるスイッチは別。切/0%で測っても処理を勝手に入れず、
    // その後の入/量の操作とホストの再準備で確実に効くことを検査する。
    for (const char* bypass : { "denoise", "dn_on", "mod_souji" })
    {
        fresh();
        auto p = std::make_unique<VocalGzzioProcessor>();
        setup (*p, 48000, 128, 2);
        setP (*p, bypass, 0);
        const double off = run (*p, 48000, 128, 2, -42, true);
        CHECK (p->getDenoiseLearnResult() == 1 && std::abs (off) < 0.3,
               "%sを切って学習しても音量を変えない（結果%d・%.2f dB）",
               bypass, p->getDenoiseLearnResult(), off);
        setP (*p, bypass, juce::String (bypass) == "denoise" ? 60.0f : 1.0f);
        const double on = run (*p, 48000, 128, 2, -42, false);
        CHECK (on < -6, "%sを戻すと学習したノイズを抑える（%.2f dB）", bypass, on);
        p->releaseResources();
        p->prepareToPlay (44100, 256);
        const double restarted = run (*p, 44100, 256, 2, -42, false);
        CHECK (p->isDenoiseLearned() && restarted < -6,
               "音声機器/ホストの再準備後も学習と除去が働く（%.2f dB）", restarted);
    }
    for (const auto sr : { 44100.0, 48000.0, 96000.0 })
        for (const bool manual : { false, true })
            for (const float level : { -54.0f, -42.0f, -30.0f })
            {
                fresh();
                auto p = std::make_unique<VocalGzzioProcessor>();
                const int channels = sr == 44100 ? 1 : 2, block = sr == 48000 ? 512 : 127;
                setup (*p, sr, block, channels);
                const double change = run (*p, sr, block, channels, level, manual);
                CHECK (change < -6, "%.0f Hz・%d ch・白色ノイズ %.0f dBFS・%s: 開始3秒後 %.2f dB",
                       sr, channels, level, manual ? "学習操作あり" : "つまみ操作のみ", change);
                if (manual)
                    CHECK (p->getDenoiseLearnResult() == 1, "定常ノイズを採用する（結果%d / 測定%.1f dBFS）",
                           p->getDenoiseLearnResult(), p->getDenoiseLearnLevelDb());
                if (manual && level == -30)
                {
                    juce::MemoryBlock saved; p->getStateInformation (saved);
                    fresh();
                    auto restored = std::make_unique<VocalGzzioProcessor>();
                    setup (*restored, sr, block, channels);
                    restored->setStateInformation (saved.getData(), (int) saved.getSize());
                    CHECK (restored->isDenoiseLearned(), "大きい定常ノイズの学習を保存・復元できる");
                    const double after = run (*restored, sr, block, channels, level, false);
                    CHECK (std::abs (after - change) < 0.2, "復元前後の効きの差 %.3f dB", after - change);
                }
            }
    for (bool manual : { false, true })
        for (int colour : { 1, 2 })
        {
            fresh();
            auto p = std::make_unique<VocalGzzioProcessor>();
            setup (*p, 48000, 128, 2);
            const double change = run (*p, 48000, 128, 2, -30, manual, false, 196, false, colour);
            CHECK (change < -6, "%sノイズ・%sも抑える（音量差%.2f dB）",
                   colour == 1 ? "ピンク" : "低域の多い", manual ? "学習操作あり" : "自動追従", change);
            if (manual) CHECK (p->getDenoiseLearnResult() == 1, "色付きの定常ノイズを採用する（結果%d）", p->getDenoiseLearnResult());
        }
    for (bool manual : { false, true })
        for (float level : { -54.0f, -42.0f, -30.0f, -18.0f })
        {
            fresh();
            auto p = std::make_unique<VocalGzzioProcessor>();
            setup (*p, 48000, 128, 2);
            const double change = run (*p, 48000, 128, 2, level, manual, true);
            CHECK (change > -1, "持続する声 %.0f dBFS・%sをノイズと学習しない（音量差%.2f dB）",
                   level, manual ? "学習操作あり" : "自動追従", change);
            if (manual) CHECK (p->getDenoiseLearnResult() != 1, "声を含む学習を採用しない（結果%d）", p->getDenoiseLearnResult());
        }
    for (double hz : { 65.4, 82.4, 440.0, 880.0 })
        for (bool opposite : { false, true })
        {
            fresh();
            auto p = std::make_unique<VocalGzzioProcessor>();
            setup (*p, 48000, 128, 2);
            const double change = run (*p, 48000, 128, 2, -42, true, true, hz, opposite);
            CHECK (change > -1 && p->getDenoiseLearnResult() != 1,
                   "%.1f Hz・左右%sの声/演奏を学習しない（音量差%.2f dB・結果%d）",
                   hz, opposite ? "逆相" : "同相", change, p->getDenoiseLearnResult());
        }
    for (bool manual : { false, true })
      for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (int chord = 0; chord < 3; ++chord)
        {
            fresh();
            auto p = std::make_unique<VocalGzzioProcessor>();
            setup (*p, sr, 128, 2);
            const std::array<double, 6> normal { 82.4069, 123.4708, 164.8138, 207.6523, 246.9417, 329.6276 };
            const std::array<double, 6> low { 65.4064, 82.4069, 97.9989, 0, 0, 0 };
            const float level = chord == 2 ? -54.0f : -24.0f;
            const double change = runChord (*p, chord == 1 ? low : normal, level, manual, sr,
                                           sr == 96000, sr == 44100);
            CHECK (change > -1.0, "%.0f Hz・%s和音・%sをノイズへ取り込まない（音量差%.2f dB）", sr,
                   chord == 1 ? "低音の" : chord == 2 ? "小音量の" : "通常の",
                   manual ? "学習操作あり" : "自動追従", change);
            if (manual) CHECK (p->getDenoiseLearnResult() != 1,
                               "和音を含む学習を採用しない（結果%d）", p->getDenoiseLearnResult());
        }
    {
        fresh();
        auto p = std::make_unique<VocalGzzioProcessor>();
        setup (*p, 48000, 128, 2);
        run (*p, 48000, 128, 2, -42, true);
        juce::MemoryBlock before; p->getStateInformation (before);
        auto beforeXml = juce::AudioProcessor::getXmlFromBinary (before.getData(), (int) before.getSize());
        juce::AudioBuffer<float> muted (2, 128);
        juce::MidiBuffer midi;
        muted.clear();
        // 入力をミュートしてから学習。既存の正しい部屋設定を消さない。
        for (int n = 0; n < 200; ++n) { muted.clear(); p->processBlock (muted, midi); }
        p->requestDenoiseLearn();
        for (int n = 0; n < 800; ++n) { muted.clear(); p->processBlock (muted, midi); }
        juce::MemoryBlock after; p->getStateInformation (after);
        auto afterXml = juce::AudioProcessor::getXmlFromBinary (after.getData(), (int) after.getSize());
        CHECK (p->getDenoiseLearnResult() != 1 && p->isDenoiseLearned(),
               "入力ミュート中の学習を拒否し、以前の学習を保持（結果%d）", p->getDenoiseLearnResult());
        auto* beforeFloors = beforeXml ? beforeXml->getChildByName ("DENOISE") : nullptr;
        auto* afterFloors = afterXml ? afterXml->getChildByName ("DENOISE") : nullptr;
        bool floorsSame = beforeFloors != nullptr && afterFloors != nullptr;
        if (floorsSame)
            for (int b = 0; b < 4; ++b)
            {
                auto key = "f" + juce::String (b);
                floorsSame = floorsSame && beforeFloors->getDoubleAttribute (key)
                    == afterFloors->getDoubleAttribute (key);
            }
        CHECK (floorsSame, "ミュート中の誤学習で保存値を書き換えない");
    }
    // 新方式で検証済みの印があっても、壊れた保存値を信頼しない。
    // 1帯域だけの破損も検出し、自動推定へ戻して有限な音を出せること。
    for (const char* invalid : { "nan", "inf", "-inf", "-0.01", "0", "1" })
    {
        fresh();
        auto p = std::make_unique<VocalGzzioProcessor>();
        setup (*p, 48000, 128, 2);
        juce::MemoryBlock saved;
        p->getStateInformation (saved);
        auto xml = juce::AudioProcessor::getXmlFromBinary (saved.getData(), (int) saved.getSize());
        auto* floors = xml ? xml->getChildByName ("DENOISE") : nullptr;
        CHECK (floors != nullptr, "保存不正値の検査用データを取得できる");
        if (floors == nullptr) continue;
        floors->setAttribute ("learned", true);
        floors->setAttribute ("validation", 1);
        for (int band = 0; band < 4; ++band) floors->setAttribute ("f" + juce::String (band), 0.001);
        floors->setAttribute ("f2", invalid);
        juce::MemoryBlock corrupted;
        juce::AudioProcessor::copyXmlToBinary (*xml, corrupted);
        p->setStateInformation (corrupted.getData(), (int) corrupted.getSize());
        CHECK (! p->isDenoiseLearned(), "不正な床 %s を読み込まず自動推定へ戻す", invalid);
        const double change = run (*p, 48000, 128, 2, -42, false);
        CHECK (std::isfinite (change) && change < -6,
               "不正値 %s の復元後も有限な出力でノイズを抑える（%.2f dB）", invalid, change);
    }
    fresh();
    std::printf ("検査失敗: %d件\n", failures);
    return failures == 0 ? 0 : 1;
}
