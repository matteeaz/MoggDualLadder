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
        noiseAlpha = 1.0 - std::exp(-2.0 * pi * 350.0 / sampleRate);
        crackleDecayCoeff = std::exp(-1.0 / (0.0012 * sampleRate));
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

        // Sanftes Resonanz-Taper an den offenen Park-Anschlägen (10..12 Hz HP / 24..30 kHz LP)
        const double hpParkTaper = std::clamp((static_cast<double>(targetHpFreq) - 10.0) / 2.0, 0.0, 1.0);
        const double lpParkTaper = std::clamp((30000.0 - static_cast<double>(targetLpFreq)) / 6000.0, 0.0, 1.0);

        // Resonanz 0.0 bis 1.0 -> intern 0.0 bis 4.15 (Selbstoszillation knapp unter 4.0)
        lpRes = static_cast<double>(std::clamp(lpResonance, 0.0f, 1.05f)) * 4.15 * resTolerance * lpParkTaper;
        hpRes = static_cast<double>(std::clamp(hpResonance, 0.0f, 1.05f)) * 4.15 * resTolerance * hpParkTaper;

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
        crackleEnv = 0.0;
        snapSmoothing();
    }

    float processSample(float input) {
        smoothLpG       += smoothCoeff * (targetLpG - smoothLpG);
        smoothHpG       += smoothCoeff * (targetHpG - smoothHpG);
        smoothLpBypass  += smoothCoeff * (targetLpBypass - smoothLpBypass);
        smoothHpAlpha   += smoothCoeff * (targetHpAlpha - smoothHpAlpha);
        smoothLpAlpha   += smoothCoeff * (targetLpAlpha - smoothLpAlpha);
        smoothDirt      += smoothCoeff * (targetDirt - smoothDirt);
        smoothInGain    += smoothCoeff * (targetInGain - smoothInGain);
        smoothOutGain   += smoothCoeff * (targetOutGain - smoothOutGain);

        double modFactor = 1.0;
        if (smoothDirt > 1.0e-6) {
            // 1. Gefiltertes analoges Noise (nicht direkt hörbar)
            const double white = nextBipolarRandom();
            noiseLpState += noiseAlpha * (white - noiseLpState);

            // 2. Vinyl-Knackser / Staubimpulse
            const double trig = nextUnipolarRandom();
            if (trig < (32.0 / sampleRate)) {
                const double r = nextUnipolarRandom();
                const double sign = (nextBipolarRandom() >= 0.0) ? 1.0 : -1.0;
                const double mag = 0.25 + 0.75 * (r * r * r);
                crackleEnv += sign * mag;
            }
            crackleEnv *= crackleDecayCoeff;

            // Leichte Frequenzmodulation von HP & LP
            const double modOffset = smoothDirt * (0.14 * noiseLpState + 0.16 * crackleEnv);
            modFactor = 1.0 + std::clamp(modOffset, -0.35, 0.35);
        } else {
            noiseLpState = 0.0;
            crackleEnv = 0.0;
        }

        const double scaledIn = static_cast<double>(input) * smoothInGain;

        // 1. Stufe: Mogg Highpass
        double hpOut = processMoggStage(scaledIn, smoothHpG * modFactor, hpRes, smoothHpAlpha, hpSatMode, hpState, hpState2, true);

        // 2. Stufe: Mogg Lowpass
        double lpOut = processMoggStage(hpOut, smoothLpG * modFactor, lpRes, smoothLpAlpha, lpSatMode, lpState, lpState2, false);

        // Auf den letzten 600 Hz vor Rechtsanschlag (29.4..30.0 kHz) restliche Ultraschall-Dämpfung
        // phasenfrei ausblenden, während die LP-Sättigung aktiv bleibt
        if (smoothLpBypass > 1.0e-6) {
            double openSat = hpOut;
            if (smoothLpAlpha > 1.0001 || lpSatMode != SatMode::Ladder) {
                double unusedDer = 1.0;
                openSat = evalSat(hpOut, smoothLpAlpha, 1.0 / smoothLpAlpha, lpSatMode, unusedDer);
            }
            lpOut += smoothLpBypass * (openSat - lpOut);
        }

        return static_cast<float>(lpOut * smoothOutGain);
    }

private:
    double sampleRate = 44100.0;
    double smoothCoeff = 0.01;
    double noiseAlpha = 0.01;
    double crackleDecayCoeff = 0.99;

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
    double targetLpBypass = 0.0, smoothLpBypass = 0.0;

    double lpState[4]  = { 0.0, 0.0, 0.0, 0.0 };
    double hpState[4]  = { 0.0, 0.0, 0.0, 0.0 };
    double lpState2[4] = { 0.0, 0.0, 0.0, 0.0 };
    double hpState2[4] = { 0.0, 0.0, 0.0, 0.0 };

    uint32_t rngState = 0x12345678u;
    double noiseLpState = 0.0;
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
        const double maxOpenFreq = sampleRate * 0.48;

        // Lowpass: bis 24 kHz exakt; zwischen 24 kHz und 30 kHz sanfter Glide hoch bis zur Nyquist-Grenze
        const double rawLp = static_cast<double>(targetLpFreq);
        double effectiveLpHz = rawLp;
        if (rawLp > 24000.0) {
            const double t = std::clamp((rawLp - 24000.0) / 6000.0, 0.0, 1.0);
            effectiveLpHz = 24000.0 + (maxOpenFreq - 24000.0) * (t * t);
        }
        const double lpHz = std::clamp(effectiveLpHz * freqTolerance, 10.0, maxOpenFreq);
        targetLpG = std::tan(pi * lpHz / sampleRate);
        targetLpBypass = std::clamp((rawLp - 29400.0) / 600.0, 0.0, 1.0);

        // Highpass: ab 12 Hz exakt; zwischen 12 Hz und 10 Hz sanfter Glide hinunter auf 0 Hz
        const double rawHp = static_cast<double>(targetHpFreq);
        if (rawHp <= 10.001) {
            targetHpG = 0.0;
        } else if (rawHp < 12.0) {
            const double t = (rawHp - 10.0) / 2.0;
            const double effectiveHpHz = 12.0 * (t * t) * freqTolerance;
            targetHpG = std::tan(pi * effectiveHpHz / sampleRate);
        } else {
            const double hpHz = std::clamp(rawHp * freqTolerance, 12.0, sampleRate * 0.45);
            targetHpG = std::tan(pi * hpHz / sampleRate);
        }
    }

    void snapSmoothing() {
        smoothLpG      = targetLpG;
        smoothHpG      = targetHpG;
        smoothLpBypass = targetLpBypass;
        smoothHpAlpha  = targetHpAlpha;
        smoothLpAlpha  = targetLpAlpha;
        smoothDirt     = targetDirt;
        smoothInGain   = targetInGain;
        smoothOutGain  = targetOutGain;
    }

    double processMoggStage(double input, double g, double k, double alpha, SatMode mode,
                            double s1[4], double s2[4], bool isHighpass) {
        // Wenn der Highpass vollständig auf 0 Hz geparkt ist: verlustfreier Durchlass inkl. HP-Sättigung
        if (isHighpass && g < 1.0e-7) {
            for (int i = 0; i < 4; ++i) {
                s1[i] = 0.0;
                s2[i] = 0.0;
            }
            if (alpha > 1.0001 || mode != SatMode::Ladder) {
                double unusedDer = 1.0;
                return evalSat(input, alpha, 1.0 / alpha, mode, unusedDer);
            }
            return input;
        }

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
