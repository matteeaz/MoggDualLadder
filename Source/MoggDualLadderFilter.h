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
                       float driveDb, Slope newSlope) {
        const float maxFreq = static_cast<float>(sampleRate * 0.45);
        targetLpFreq = std::clamp(lpFreqHz, 10.0f, maxFreq);
        targetHpFreq = std::clamp(hpFreqHz, 10.0f, maxFreq);

        lpRes = static_cast<double>(std::clamp(lpResonance, 0.0f, 1.05f)) * 4.0 * resTolerance;
        hpRes = static_cast<double>(std::clamp(hpResonance, 0.0f, 1.05f)) * 4.0 * resTolerance;

        drive = std::pow(10.0, static_cast<double>(driveDb) / 20.0);

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
        smoothLpG += smoothCoeff * (targetLpG - smoothLpG);
        smoothHpG += smoothCoeff * (targetHpG - smoothHpG);

        // Transistor-Eingangsstufe mit Drive
        double signal = fastTanh(static_cast<double>(input) * drive);

        // 1. Stufe: Mogg Highpass
        double hpOut = processMoggStage(signal, smoothHpG, hpRes, hpState, hpState2, true);

        // 2. Stufe: Mogg Lowpass
        double lpOut = processMoggStage(hpOut, smoothLpG, lpRes, lpState, lpState2, false);

        return static_cast<float>(lpOut);
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
    double drive = 1.0;
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
        smoothLpG = targetLpG;
        smoothHpG = targetHpG;
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

    double processSingleLadder(double input, double g, double k,
                               double s[4], bool isHighpass, Slope tapSlope) {
        double G = g / (1.0 + g);
        double G2 = G * G;
        double G3 = G2 * G;
        double G4 = G2 * G2;

        double S = G3 * s[0] + G2 * s[1] + G * s[2] + s[3];
        double y4 = (G4 * input + S) / (1.0 + k * G4);

        // Newton-Raphson Iterationen im nichtlinearen Feedback-Loop
        for (int iter = 0; iter < 3; ++iter) {
            double tanh_y4 = fastTanh(y4);
            double sech2_y4 = 1.0 - tanh_y4 * tanh_y4;

            double u = input - k * tanh_y4;
            double f = G4 * u + S - y4;
            double df = -G4 * k * sech2_y4 - 1.0;

            y4 -= f / df;
        }

        double u = fastTanh(input - k * fastTanh(y4));

        double v1 = G * (u - s[0]);
        double y1 = fastTanh(v1 + s[0]);
        s[0] = y1 + v1;

        double v2 = G * (y1 - s[1]);
        double y2 = fastTanh(v2 + s[1]);
        s[1] = y2 + v2;

        double v3 = G * (y2 - s[2]);
        double y3 = fastTanh(v3 + s[2]);
        s[2] = y3 + v3;

        double v4 = G * (y3 - s[3]);
        double y4_final = fastTanh(v4 + s[3]);
        s[3] = y4_final + v4;

        if (!isHighpass) {
            switch (tapSlope) {
                case Slope::Slope6:  return y1;
                case Slope::Slope12: return y2;
                case Slope::Slope24:
                case Slope::Slope48:
                default:             return y4_final;
            }
        }

        switch (tapSlope) {
            case Slope::Slope6:  return u - y1;
            case Slope::Slope12: return u - 2.0 * y1 + y2;
            case Slope::Slope24:
            case Slope::Slope48:
            default:             return u - 4.0 * y1 + 6.0 * y2 - 4.0 * y3 + y4_final;
        }
    }

    inline double fastTanh(double x) {
        if (x < -3.0) return -1.0;
        if (x > 3.0) return 1.0;
        double x2 = x * x;
        return x * (27.0 + x2) / (27.0 + 9.0 * x2);
    }
};

