#pragma once

#include <cmath>
#include <algorithm>

class MoggDualLadderFilter {
public:
    enum class Slope {
        Slope6 = 0,
        Slope12 = 1,
        Slope24 = 2,
        Slope48 = 3
    };

    void setSampleRate(double sr) {
        sampleRate = std::max(sr, 8000.0);
        smoothCoeff = 1.0 - std::exp(-1.0 / (0.005 * sampleRate));
        updateFrequencies();
        snapSmoothing();
    }

    void setComponentTolerance(double freqFactor, double resFactor) {
        freqTolerance = freqFactor;
        resTolerance = resFactor;
        updateFrequencies();
    }

    void setParameters(float lpFreqHz, float hpFreqHz,
                       float lpResonance, float hpResonance,
                       float inGainDb, float outGainDb,
                       Slope newSlope) {
        const float maxFreq = static_cast<float>(sampleRate * 0.45);
        targetLpFreq = std::clamp(lpFreqHz, 10.0f, maxFreq);
        targetHpFreq = std::clamp(hpFreqHz, 10.0f, maxFreq);

        // Resonanz 0.0 bis 1.0 -> intern 0.0 bis 4.15 (Selbstoszillation knapp unter 4.0)
        lpRes = static_cast<double>(std::clamp(lpResonance, 0.0f, 1.05f)) * 4.15 * resTolerance;
        hpRes = static_cast<double>(std::clamp(hpResonance, 0.0f, 1.05f)) * 4.15 * resTolerance;

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
        snapSmoothing();
    }

    float processSample(float input) {
        smoothLpG     += smoothCoeff * (targetLpG - smoothLpG);
        smoothHpG     += smoothCoeff * (targetHpG - smoothHpG);
        smoothInGain  += smoothCoeff * (targetInGain - smoothInGain);
        smoothOutGain += smoothCoeff * (targetOutGain - smoothOutGain);

        const double scaledIn = static_cast<double>(input) * smoothInGain;

        // 1. Stufe: Mogg Highpass
        double hpOut = processMoggStage(scaledIn, smoothHpG, hpRes, hpState, hpState2, true);

        // 2. Stufe: Mogg Lowpass
        double lpOut = processMoggStage(hpOut, smoothLpG, lpRes, lpState, lpState2, false);

        return static_cast<float>(lpOut * smoothOutGain);
    }

private:
    double sampleRate = 44100.0;
    double smoothCoeff = 0.01;

    double freqTolerance = 1.0;
    double resTolerance = 1.0;

    float targetLpFreq = 30000.0f;
    float targetHpFreq = 10.0f;

    double lpRes = 0.0;
    double hpRes = 0.0;
    double targetInGain = 1.0, smoothInGain = 1.0;
    double targetOutGain = 1.0, smoothOutGain = 1.0;
    Slope slope = Slope::Slope24;

    double targetLpG = 0.0, smoothLpG = 0.0;
    double targetHpG = 0.0, smoothHpG = 0.0;

    double lpState[4]  = { 0.0, 0.0, 0.0, 0.0 };
    double hpState[4]  = { 0.0, 0.0, 0.0, 0.0 };
    double lpState2[4] = { 0.0, 0.0, 0.0, 0.0 };
    double hpState2[4] = { 0.0, 0.0, 0.0, 0.0 };

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
        smoothInGain  = targetInGain;
        smoothOutGain = targetOutGain;
    }

    double processMoggStage(double input, double g, double k,
                            double s1[4], double s2[4], bool isHighpass) {
        const Slope activeBlockSlope = (slope == Slope::Slope48) ? Slope::Slope24 : slope;
        double out = processSingleLadder(input, g, k, s1, isHighpass, activeBlockSlope);

        if (slope == Slope::Slope48) {
            const double k2 = std::min(k * 0.35, 1.4);
            out = processSingleLadder(out, g, k2, s2, isHighpass, Slope::Slope24);
        }

        return out;
    }

    // Physikalisches 4-Pol Moog Ladder Modell mit exaktem Unity-Gain im Durchlassbereich
    double processSingleLadder(double input, double g, double k,
                               double s[4], bool isHighpass, Slope tapSlope) {
        // Startwerte aus den Integrator-Zuständen
        double x1 = s[0];
        double x2 = s[1];
        double x3 = s[2];
        double x4 = s[3];

        // Geschlossener 4x4 Newton-Raphson-Solver für die Trapezoidal-Integration:
        // x_n - s_n = g * (tanh(x_{n-1}) - tanh(x_n))
        for (int iter = 0; iter < 3; ++iter) {
            const double tx4 = fastTanh(x4);
            const double x0  = input - k * tx4;

            const double tx0 = fastTanh(x0);
            const double tx1 = fastTanh(x1);
            const double tx2 = fastTanh(x2);
            const double tx3 = fastTanh(x3);

            const double f1 = x1 - s[0] - g * (tx0 - tx1);
            const double f2 = x2 - s[1] - g * (tx1 - tx2);
            const double f3 = x3 - s[2] - g * (tx2 - tx3);
            const double f4 = x4 - s[3] - g * (tx3 - tx4);

            const double d0 = 1.0 - tx0 * tx0;
            const double d1 = 1.0 - tx1 * tx1;
            const double d2 = 1.0 - tx2 * tx2;
            const double d3 = 1.0 - tx3 * tx3;
            const double d4 = 1.0 - tx4 * tx4;

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

        // Trapezoidal-States aktualisieren
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

        const double x0 = input - k * fastTanh(x4);

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
