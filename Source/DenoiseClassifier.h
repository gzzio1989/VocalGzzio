#pragma once

#include <array>
#include <algorithm>
#include <cmath>
#include <complex>

namespace gz
{
// 床の大きさで声を判定しない。左右を別々に測り、周期を持つ声や演奏、
// 大きさの変化中、無音を床の更新から除く。測定だけなので音声遅延は増えない。
class DenoiseClassifier
{
public:
    DenoiseClassifier() noexcept
    {
        constexpr double pi = 3.14159265358979323846;
        for (int i = 0; i < frameSize; ++i)
            window[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * pi * i / (frameSize - 1)));
        for (int i = 0; i < frameSize / 2; ++i)
        {
            const double phase = -2.0 * pi * i / frameSize;
            twiddles[(size_t) i] = { (float) std::cos (phase), (float) std::sin (phase) };
        }
    }

    void prepare (double sampleRate) noexcept
    {
        // 学習解除からも呼ばれるので、音声スレッドで表の再生成や確保をしない。
        sum.fill (0); bandSum.fill (0); bandMean.fill (0);
        decimCount = position = sampleCount = stableFrames = 0;
        periodic = stationary = havePrevious = false;
        silent = frameGateOpen = true;
        decimation = std::max (1, (int) std::floor (sampleRate / 8000.0));
        analysisRate = sampleRate / decimation;
        neededFrames = (int) std::ceil (0.5 * analysisRate / frameSize);
    }

    bool push (float left, float right, const float* envelopes, bool gateOpen) noexcept
    {
        sum[0] += left; sum[1] += right;
        for (int b = 0; b < 4; ++b) bandSum[(size_t) b] += envelopes[b];
        ++sampleCount;
        frameGateOpen = frameGateOpen && gateOpen;
        if (++decimCount < decimation) return false;
        decimCount = 0;
        for (int ch = 0; ch < 2; ++ch)
        {
            samples[(size_t) ch][(size_t) position] = sum[(size_t) ch] / (float) decimation;
            sum[(size_t) ch] = 0;
        }
        if (++position < frameSize) return false;
        position = 0;
        periodic = false;
        for (const auto& channel : samples)
        {
            double mean = 0;
            for (float x : channel) mean += x;
            mean /= frameSize;
            const int maxLag = std::min (frameSize / 3, (int) (analysisRate / 55.0));
            const int count = frameSize - maxLag;
            bool hadValley = false;
            for (int lag = 1; lag <= maxLag; ++lag)
            {
                double cross = 0, aPower = 0, bPower = 0;
                for (int i = 0; i < count; ++i)
                {
                    const double a = channel[(size_t) i] - mean;
                    const double b = channel[(size_t) (i + lag)] - mean;
                    cross += a * b; aPower += a * a; bPower += b * b;
                }
                const double corr = cross / std::sqrt (aPower * bPower + 1e-30);
                if (corr < 0.45) hadValley = true;
                if (hadValley && corr > 0.72 && aPower / count > 1e-12)
                { periodic = true; break; }
            }
            // 和音には短い共通周期がないため、自己相関だけでは部屋ノイズと
            // 誤認する。周波数の山と谷も調べて、複数の弦の持続音を保護する。
            if (! periodic) periodic = hasTonalSpectrum (channel, mean);
            if (periodic) break;
        }

        bool stable = havePrevious && frameGateOpen && ! periodic;
        float largest = 0;
        for (int b = 0; b < 4; ++b)
        {
            const float current = (float) (bandSum[(size_t) b] / sampleCount);
            const float previous = bandMean[(size_t) b];
            if (current > std::max (previous * 2.0f, 1e-6f)
                || previous > std::max (current * 2.0f, 1e-6f)) stable = false;
            bandMean[(size_t) b] = current;
            largest = std::max (largest, current);
            bandSum[(size_t) b] = 0;
        }
        // 入力の切断・ミュートを「静かな部屋」として覚えない。
        silent = largest < 1e-5f;
        stableFrames = stable && ! silent ? stableFrames + 1 : 0;
        stationary = stableFrames >= neededFrames;
        havePrevious = true;
        sampleCount = 0;
        frameGateOpen = true;
        return true;
    }

    bool isPeriodic() const noexcept { return periodic; }
    bool isStationary() const noexcept { return stationary; }
    float floorTarget (int band) const noexcept { return bandMean[(size_t) band] * 1.4f; }

private:
    // 約128 msの分析窓で低い弦同士も分離する。音を溜めて出力しないため
    // 音声の遅延は増えず、床の更新だけが約0.5秒の確認後に始まる。
    static constexpr int frameSize = 1024;
    bool hasTonalSpectrum (const std::array<float, frameSize>& channel, double mean) noexcept
    {
        double energy = 0;
        for (int i = 0; i < frameSize; ++i)
        {
            const float x = channel[(size_t) i] - (float) mean;
            energy += (double) x * x;
            spectrum[(size_t) i] = { x * window[(size_t) i], 0.0f };
        }
        if (energy / frameSize <= 1e-12) return false;

        // 固定サイズのFFT。作業領域と回転係数は構築時から保持する。
        for (int i = 1, j = 0; i < frameSize; ++i)
        {
            int bit = frameSize >> 1;
            for (; (j & bit) != 0; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap (spectrum[(size_t) i], spectrum[(size_t) j]);
        }
        for (int length = 2; length <= frameSize; length <<= 1)
            for (int start = 0; start < frameSize; start += length)
                for (int j = 0; j < length / 2; ++j)
                {
                    const auto a = spectrum[(size_t) (start + j)];
                    const auto b = spectrum[(size_t) (start + j + length / 2)]
                                 * twiddles[(size_t) (j * frameSize / length)];
                    spectrum[(size_t) (start + j)] = a + b;
                    spectrum[(size_t) (start + j + length / 2)] = a - b;
                }
        for (int i = 0; i <= frameSize / 2; ++i)
            power[(size_t) i] = std::norm (spectrum[(size_t) i]);

        // 全体の傾きは部屋ノイズでも生じる。近傍の平均でならしてから
        // 平坦さを測り、低域の多いノイズを和音と取り違えないようにする。
        const int first = std::max (1, (int) std::ceil (55.0 * frameSize / analysisRate));
        const int last = std::min (frameSize / 2 - 8, (int) (2000.0 * frameSize / analysisRate));
        double logSum = 0, linearSum = 0;
        for (int bin = first; bin <= last; ++bin)
        {
            double localPower = 0;
            const int from = std::max (0, bin - 8), to = std::min (frameSize / 2, bin + 8);
            for (int i = from; i <= to; ++i) localPower += power[(size_t) i];
            localPower /= to - from + 1;
            const double normalized = std::max (1e-12, power[(size_t) bin] / (localPower + 1e-30));
            linearSum += normalized;
            logSum += std::log (normalized);
        }
        const int bins = last - first + 1;
        const double flatness = std::exp (logSum / bins) / (linearSum / bins);
        return flatness < 0.38;
    }

    std::array<std::array<float, frameSize>, 2> samples {};
    std::array<float, frameSize> window {};
    std::array<std::complex<float>, frameSize / 2> twiddles {};
    std::array<std::complex<float>, frameSize> spectrum {};
    std::array<float, frameSize / 2 + 1> power {};
    std::array<float, 2> sum {};
    std::array<double, 4> bandSum {};
    std::array<float, 4> bandMean {};
    double analysisRate = 8000;
    int decimation = 1, decimCount = 0, position = 0, sampleCount = 0;
    int stableFrames = 0, neededFrames = 8;
    bool periodic = false, stationary = false, silent = true;
    bool havePrevious = false, frameGateOpen = true;
};
}
