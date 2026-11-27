#pragma once

#include <cmath>
#include <cstdint>
#include <algorithm>

class MoggDualLadderFilter {
public:
    enum class Slope {
        Slope6 = 0,
        Slope12 = 1,
        Slope24 = 2,
        Slope48 = 3
    };

    enum class SatMode {
        Ladder = 0,
        Warm = 1,
        Fold = 2,
        Grit = 3
    };

    void setSampleRate(double sr) {
        constexpr double pi = 3.14159265358979323846;
        sampleRate = std::max(sr, 8000.0);
        smoothCoeff = 1.0 - std::exp(-1.0 / (0.005 * sampleRate));

        // Bandbegrenztes Analog-Noise (~950 Hz) inkl. RMS-Normierung für 4x Oversampling
        noiseAlpha = 1.0 - std::exp(-2.0 * pi * 950.0 / sampleRate);
        noiseNorm  = 1.0 / std::sqrt(std::max(noiseAlpha / 6.0, 1.0e-6));

        // Abklingzeiten für feinen Vinyl-Staub (0.6 ms) und kräftigere Pops (2.2 ms)
        dustDecayCoeff    = std::exp(-1.0 / (0.0006 * sampleRate));
        crackleDecayCoeff = std::exp(-1.0 / (0.0022 * sampleRate));

        updateFrequencies();
        snapSmoothing();
    }

    void setComponentTolerance(double freqFactor, double resFactor) {
        freqTolerance = freqFactor;
        resTolerance = resFactor;
        rngState = (static_cast<uint32_t>(freqFactor * 1000000.0) ^ 0x9E3779B9u) | 1u;
        updateFrequencies();
    }

    void setParameters(float lpFreqHz, float hpFreqHz,
                       float lpResonance, float hpResonance,
                       float hpSat, SatMode newHpSatMode,
                       float lpSat, SatMode newLpSatMode,
                       float dirtPercent,
                       float inGainDb, float outGainDb,
                       Slope newSlope) {
        const float maxFreq = static_cast<float>(sampleRate * 0.45);
        targetLpFreq = std::clamp(lpFreqHz, 10.0f, maxFreq);
        targetHpFreq = std::clamp(hpFreqHz, 10.0f, maxFreq);

        // Resonanz 0.0 bis 1.0 -> intern 0.0 bis 4.15 (Selbstoszillation knapp unter 4.0)
        lpRes = static_cast<double>(std::clamp(lpResonance, 0.0f, 1.05f)) * 4.15 * resTolerance;
        hpRes = static_cast<double>(std::clamp(hpResonance, 0.0f, 1.05f)) * 4.15 * resTolerance;

        // UI-Skala 1.0 bis 11.0 -> intern musikalisch skaliert auf alpha 1.0 (natürlich) bis 4.5 (+13 dB)
        const double hpNorm = (std::clamp(static_cast<double>(hpSat), 1.0, 11.0) - 1.0) / 10.0;
        const double lpNorm = (std::clamp(static_cast<double>(lpSat), 1.0, 11.0) - 1.0) / 10.0;

        targetHpAlpha = 1.0 + 3.5 * hpNorm;
        targetLpAlpha = 1.0 + 3.5 * lpNorm;

        hpSatMode = newHpSatMode;
        lpSatMode = newLpSatMode;

        // Dirt 0..100 % -> interne Modulationsstärke für Noise + Vinyl-Knackser
        targetDirt = std::clamp(static_cast<double>(dirtPercent) / 100.0, 0.0, 1.0);

        targetInGain  = std::pow(10.0, static_cast<double>(std::clamp(inGainDb, -10.0f, 10.0f)) / 20.0);
        targetOutGain = std::pow(10.0, static_cast<double>(std::clamp(outGainDb, -10.0f, 10.0f)) / 20.0);

        if (slope != Slope::Slope48 && newSlope == Slope::Slope48) {
            for (int i = 0; i < 4; ++i) {
                lpState2[i] = 0.0;
                hpState2[i] = 0.0;
            }
        }
        slope = newSlope;

        updateFrequencies();
    }

    void reset() {
        for (int i = 0; i < 4; ++i) {
            lpState[i] = 0.0;
            hpState[i] = 0.0;
            lpState2[i] = 0.0;
            hpState2[i] = 0.0;
        }
        noiseLpState = 0.0;
        dustEnv = 0.0;
        crackleEnv = 0.0;
        snapSmoothing();
    }

    float processSample(float input) {
        smoothLpG     += smoothCoeff * (targetLpG - smoothLpG);
        smoothHpG     += smoothCoeff * (targetHpG - smoothHpG);
        smoothHpAlpha += smoothCoeff * (targetHpAlpha - smoothHpAlpha);
        smoothLpAlpha += smoothCoeff * (targetLpAlpha - smoothLpAlpha);
        smoothDirt    += smoothCoeff * (targetDirt - smoothDirt);
        smoothInGain  += smoothCoeff * (targetInGain - smoothInGain);
        smoothOutGain += smoothCoeff * (targetOutGain - smoothOutGain);

        double modFactor = 1.0;
        if (smoothDirt > 1.0e-6) {
            // 1. RMS-normiertes analoges Band-Noise (nicht direkt hörbar)
            const double white = nextBipolarRandom();
            noiseLpState += noiseAlpha * (white - noiseLpState);
            const double normNoise = std::clamp(noiseLpState * noiseNorm, -2.5, 2.5);

            // 2a. Feiner Vinyl-Oberflächenstaub (~160 Impulse/s)
            if (nextUnipolarRandom() < (160.0 / sampleRate)) {
                const double r = nextUnipolarRandom();
                const double sign = (nextBipolarRandom() >= 0.0) ? 1.0 : -1.0;
                dustEnv += sign * (0.15 + 0.35 * r * r);
            }
            dustEnv *= dustDecayCoeff;

            // 2b. Kräftigere Vinyl-Knackser & Pops (~24 Impulse/s)
            if (nextUnipolarRandom() < (24.0 / sampleRate)) {
                const double r = nextUnipolarRandom();
                const double sign = (nextBipolarRandom() >= 0.0) ? 1.0 : -1.0;
                crackleEnv += sign * (0.45 + 0.55 * r * r);
            }
            crackleEnv *= crackleDecayCoeff;

            const double crackleSum = std::clamp(dustEnv + crackleEnv, -2.2, 2.2);

            // Oktav-skalierte Cutoff-Modulation
            const double dirtCurve = smoothDirt * (0.45 + 0.55 * smoothDirt);
            const double modOctaves = std::clamp(dirtCurve * (0.75 * normNoise + 1.45 * crackleSum), -2.5, 2.5);
            modFactor = std::exp2(modOctaves);
        } else {
            noiseLpState = 0.0;
            dustEnv = 0.0;
            crackleEnv = 0.0;
        }

        const double scaledIn = static_cast<double>(input) * smoothInGain;
        const double modHpG   = std::min(smoothHpG * modFactor, 12.0);
        const double modLpG   = std::min(smoothLpG * modFactor, 12.0);

        // 1. Stufe: Mogg Highpass
        double hpOut = processMoggStage(scaledIn, modHpG, hpRes, smoothHpAlpha, hpSatMode, hpState, hpState2, true);

        // 2. Stufe: Mogg Lowpass
        double lpOut = processMoggStage(hpOut, modLpG, lpRes, smoothLpAlpha, lpSatMode, lpState, lpState2, false);

        return static_cast<float>(lpOut * smoothOutGain);
    }

private:
    double sampleRate = 44100.0;
    double smoothCoeff = 0.01;
    double noiseAlpha = 0.03;
    double noiseNorm = 13.0;
    double dustDecayCoeff = 0.98;
    double crackleDecayCoeff = 0.995;

    double freqTolerance = 1.0;
    double resTolerance = 1.0;

    float targetLpFreq = 30000.0f;
    float targetHpFreq = 10.0f;

    double lpRes = 0.0;
    double hpRes = 0.0;
    double targetHpAlpha = 1.0, smoothHpAlpha = 1.0;
    double targetLpAlpha = 1.0, smoothLpAlpha = 1.0;
    double targetDirt    = 0.0, smoothDirt    = 0.0;
    double targetInGain  = 1.0, smoothInGain  = 1.0;
    double targetOutGain = 1.0, smoothOutGain = 1.0;

    Slope slope = Slope::Slope24;
    SatMode hpSatMode = SatMode::Ladder;
    SatMode lpSatMode = SatMode::Ladder;

    double targetLpG = 0.0, smoothLpG = 0.0;
    double targetHpG = 0.0, smoothHpG = 0.0;

    double lpState[4]  = { 0.0, 0.0, 0.0, 0.0 };
    double hpState[4]  = { 0.0, 0.0, 0.0, 0.0 };
    double lpState2[4] = { 0.0, 0.0, 0.0, 0.0 };
    double hpState2[4] = { 0.0, 0.0, 0.0, 0.0 };

    uint32_t rngState = 0x12345678u;
    double noiseLpState = 0.0;
    double dustEnv = 0.0;
    double crackleEnv = 0.0;

    inline uint32_t nextUInt32() {
        uint32_t x = rngState;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        rngState = x;
        return x;
    }

    inline double nextUnipolarRandom() {
        return static_cast<double>(nextUInt32() >> 8) * (1.0 / 16777216.0);
    }

    inline double nextBipolarRandom() {
        return nextUnipolarRandom() * 2.0 - 1.0;
    }

    void updateFrequencies() {
        constexpr double pi = 3.14159265358979323846;
        const double maxFreq = sampleRate * 0.45;
        const double lpHz = std::clamp(static_cast<double>(targetLpFreq) * freqTolerance, 10.0, maxFreq);
        const double hpHz = std::clamp(static_cast<double>(targetHpFreq) * freqTolerance, 10.0, maxFreq);

        targetLpG = std::tan(pi * lpHz / sampleRate);
        targetHpG = std::tan(pi * hpHz / sampleRate);
    }

    void snapSmoothing() {
        smoothLpG     = targetLpG;
        smoothHpG     = targetHpG;
        smoothHpAlpha = targetHpAlpha;
        smoothLpAlpha = targetLpAlpha;
        smoothDirt    = targetDirt;
        smoothInGain  = targetInGain;
        smoothOutGain = targetOutGain;
    }

    double processMoggStage(double input, double g, double k, double alpha, SatMode mode,
                            double s1[4], double s2[4], bool isHighpass) {
        const Slope activeBlockSlope = (slope == Slope::Slope48) ? Slope::Slope24 : slope;
        double out = processSingleLadder(input, g, k, alpha, mode, s1, isHighpass, activeBlockSlope);

        if (slope == Slope::Slope48) {
            const double k2 = std::min(k * 0.35, 1.4);
            const double alpha2 = 1.0 + (alpha - 1.0) * 0.35;
            out = processSingleLadder(out, g, k2, alpha2, mode, s2, isHighpass, Slope::Slope24);
        }

        return out;
    }

    // Kennlinien-Auswertung mit exakter Ableitung und normierter Steigung am Ursprung (0 dB Passband-Gain)
    inline double evalSat(double x, double alpha, double invAlpha, SatMode mode, double& outDer) const {
        const double u = alpha * x;

        switch (mode) {
            case SatMode::Warm: {
                constexpr double bias = 0.45;
                constexpr double tanhBias = 0.4218990052500079;
                constexpr double invSech2Bias = 1.216546722769713;
                const double t = fastTanh(u + bias);
                outDer = (1.0 - t * t) * invSech2Bias;
                return (t - tanhBias) * invSech2Bias * invAlpha;
            }
            case SatMode::Fold: {
                constexpr double beta = 7.853981633974483; // 2.5 * pi
                constexpr double invBeta = 0.12732395447351627;
                const double t = fastTanh(u * invBeta);
                const double phase = beta * t;
                outDer = std::cos(phase) * (1.0 - t * t);
                return std::sin(phase) * invAlpha;
            }
            case SatMode::Grit: {
                constexpr double c = 0.82;
                constexpr double k = 1.15;
                constexpr double ck = c * k;
                constexpr double norm = 1.0 / (1.0 - c);
                const double t1 = fastTanh(u);
                const double t2 = fastTanh(k * u);
                outDer = norm * ((1.0 - t1 * t1) - ck * (1.0 - t2 * t2));
                return norm * (t1 - c * t2) * invAlpha;
            }
            case SatMode::Ladder:
            default: {
                const double t = fastTanh(u);
                outDer = 1.0 - t * t;
                return t * invAlpha;
            }
        }
    }

    // Physikalisches 4-Pol Moog Ladder Modell mit geschlossenem 4x4 Newton-Raphson-Solver
    double processSingleLadder(double input, double g, double k, double alpha, SatMode mode,
                               double s[4], bool isHighpass, Slope tapSlope) {
        constexpr double resPassbandComp = 0.65;
        const double compInput = isHighpass ? input : input * (1.0 + resPassbandComp * k);
        const double invAlpha  = 1.0 / alpha;

        double x1 = s[0];
        double x2 = s[1];
        double x3 = s[2];
        double x4 = s[3];

        for (int iter = 0; iter < 5; ++iter) {
            double d0 = 1.0, d1 = 1.0, d2 = 1.0, d3 = 1.0, d4 = 1.0;

            const double sx4 = evalSat(x4, alpha, invAlpha, mode, d4);
            const double x0  = compInput - k * sx4;

            const double sx0 = evalSat(x0, alpha, invAlpha, mode, d0);
            const double sx1 = evalSat(x1, alpha, invAlpha, mode, d1);
            const double sx2 = evalSat(x2, alpha, invAlpha, mode, d2);
            const double sx3 = evalSat(x3, alpha, invAlpha, mode, d3);

            const double f1 = x1 - s[0] - g * (sx0 - sx1);
            const double f2 = x2 - s[1] - g * (sx1 - sx2);
            const double f3 = x3 - s[2] - g * (sx2 - sx3);
            const double f4 = x4 - s[3] - g * (sx3 - sx4);

            const double a1 = 1.0 + g * d1;
            const double a2 = 1.0 + g * d2;
            const double a3 = 1.0 + g * d3;
            const double a4 = 1.0 + g * d4;

            const double b1 = g * d1;
            const double b2 = g * d2;
            const double b3 = g * d3;
            const double c  = g * k * d0 * d4;

            const double p1 = -f1 / a1;
            const double q1 = c / a1;

            const double p2 = (-f2 + b1 * p1) / a2;
            const double q2 = (b1 * q1) / a2;

            const double p3 = (-f3 + b2 * p2) / a3;
            const double q3 = (b2 * q2) / a3;

            const double dx4 = (-f4 + b3 * p3) / (a4 + b3 * q3);
            const double dx3 = p3 - q3 * dx4;
            const double dx2 = p2 - q2 * dx4;
            const double dx1 = p1 - q1 * dx4;

            x1 += dx1;
            x2 += dx2;
            x3 += dx3;
            x4 += dx4;
        }

        s[0] = 2.0 * x1 - s[0];
        s[1] = 2.0 * x2 - s[1];
        s[2] = 2.0 * x3 - s[2];
        s[3] = 2.0 * x4 - s[3];

        if (!isHighpass) {
            switch (tapSlope) {
                case Slope::Slope6:  return x1;
                case Slope::Slope12: return x2;
                case Slope::Slope24:
                case Slope::Slope48:
                default:             return x4;
            }
        }

        double unusedDer = 1.0;
        const double sx4_final = evalSat(x4, alpha, invAlpha, mode, unusedDer);
        const double x0 = compInput - k * sx4_final;

        switch (tapSlope) {
            case Slope::Slope6:  return x0 - x1;
            case Slope::Slope12: return x0 - 2.0 * x1 + x2;
            case Slope::Slope24:
            case Slope::Slope48:
            default:             return x0 - 4.0 * x1 + 6.0 * x2 - 4.0 * x3 + x4;
        }
    }

    inline double fastTanh(double x) const {
        if (x < -3.0) return -1.0;
        if (x > 3.0) return 1.0;
        const double x2 = x * x;
        return x * (27.0 + x2) / (27.0 + 9.0 * x2);
    }
};
