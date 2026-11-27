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
                       float driveDb, float inGainDb, float outGainDb,
                       Slope newSlope) {
        const float maxFreq = static_cast<float>(sampleRate * 0.45);
        targetLpFreq = std::clamp(lpFreqHz, 10.0f, maxFreq);
        targetHpFreq = std::clamp(hpFreqHz, 10.0f, maxFreq);

        lpRes = static_cast<double>(std::clamp(lpResonance, 0.0f, 1.05f)) * 4.15 * resTolerance;
        hpRes = static_cast<double>(std::clamp(hpResonance, 0.0f, 1.05f)) * 4.15 * resTolerance;

        targetDrive   = std::pow(10.0, static_cast<double>(driveDb) / 20.0);
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
        smoothDrive   += smoothCoeff * (targetDrive - smoothDrive);
        smoothInGain  += smoothCoeff * (targetInGain - smoothInGain);
        smoothOutGain += smoothCoeff * (targetOutGain - smoothOutGain);

        // Input Level vor den Filterstufen
        const double scaledIn = static_cast<double>(input) * smoothInGain;

        // 1. Stufe: Mogg Highpass
        double hpOut = processMoggStage(scaledIn, smoothHpG, hpRes, smoothDrive, hpState, hpState2, true);

        // 2. Stufe: Mogg Lowpass
        double lpOut = processMoggStage(hpOut, smoothLpG, lpRes, smoothDrive, lpState, lpState2, false);

        // Interne Drive-Pegelkompensation + Output Level
        const double makeupGain = 1.0 / (1.0 + 0.42 * (smoothDrive - 1.0));

        return static_cast<float>(lpOut * makeupGain * smoothOutGain);
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
    double targetDrive = 1.0, smoothDrive = 1.0;
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
        smoothDrive   = targetDrive;
        smoothInGain  = targetInGain;
        smoothOutGain = targetOutGain;
    }

    double processMoggStage(double input, double g, double k, double stageDrive,
                            double s1[4], double s2[4], bool isHighpass) {
        const Slope activeBlockSlope = (slope == Slope::Slope48) ? Slope::Slope24 : slope;
        double out = processSingleLadder(input, g, k, stageDrive, s1, isHighpass, activeBlockSlope);

        if (slope == Slope::Slope48) {
            const double k2 = std::min(k * 0.35, 1.4);
            out = processSingleLadder(out, g, k2, 1.0, s2, isHighpass, Slope::Slope24);
        }

        return out;
    }

    double processSingleLadder(double input, double g, double k, double stageDrive,
                               double s[4], bool isHighpass, Slope tapSlope) {
        const double G = g / (1.0 + g);
        const double G2 = G * G;
        const double G3 = G2 * G;
        const double G4 = G2 * G2;

        const double S = G3 * s[0] + G2 * s[1] + G * s[2] + s[3];
        const double drivenInput = input * stageDrive;

        double y4 = (G4 * asymTanh(drivenInput) + S) / (1.0 + k * G4);

        for (int iter = 0; iter < 4; ++iter) {
            const double tanh_y4  = fastTanh(y4);
            const double sech2_y4 = 1.0 - tanh_y4 * tanh_y4;

            const double diff      = drivenInput - k * tanh_y4;
            const double u_est     = asymTanh(diff);
            const double du_ddiff  = asymSech2(diff);

            const double f  = G4 * u_est + S - y4;
            const double df = -G4 * k * du_ddiff * sech2_y4 - 1.0;

            y4 -= f / df;
        }

        const double u = asymTanh(drivenInput - k * fastTanh(y4));

        const double v1 = G * (u - s[0]);
        const double y1 = fastTanh(v1 + s[0]);
        s[0] = y1 + v1;

        const double v2 = G * (y1 - s[1]);
        const double y2 = fastTanh(v2 + s[1]);
        s[1] = y2 + v2;

        const double v3 = G * (y2 - s[2]);
        const double y3 = fastTanh(v3 + s[2]);
        s[2] = y3 + v3;

        const double v4 = G * (y3 - s[3]);
        const double y4_final = fastTanh(v4 + s[3]);
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

    inline double asymTanh(double x) const {
        constexpr double bias = 0.08;
        constexpr double biasOffset = 0.07982977;
        return fastTanh(x + bias) - biasOffset;
    }

    inline double asymSech2(double x) const {
        constexpr double bias = 0.08;
        const double t = fastTanh(x + bias);
        return 1.0 - t * t;
    }

    inline double fastTanh(double x) const {
        if (x < -3.0) return -1.0;
        if (x > 3.0) return 1.0;
        const double x2 = x * x;
        return x * (27.0 + x2) / (27.0 + 9.0 * x2);
    }
};
