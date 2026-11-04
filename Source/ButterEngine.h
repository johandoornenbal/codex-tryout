#pragma once
#include <cmath>
#include <algorithm>
#include <cstdint>
// Adapted from Chris Johnson's MIT-licensed Airwindows ButterComp2.
class ButterEngine {
public:
    void reset(double rate) { *this = ButterEngine{}; sampleRate = rate;
        meterAttack = std::exp(-1.0 / (rate * 0.012));
        meterRelease = std::exp(-1.0 / (rate * 0.280)); }
    void parameters(float compress, float output, float mix) { A=compress; B=output; C=mix; }
    void processReplacing(float**, float**, int);
    void processDoubleReplacing(double**, double**, int);
    // Compressor gain element only: excludes input drive, auto makeup, output,
    // and parallel mix. Observation never feeds back into the audio algorithm.
    double reductionDb(int channel) const {
        return std::max(0.0, -20.0 * std::log10(std::max(channel == 0 ? meterGainL : meterGainR, 1.0e-6)));
    }
    void releaseMeters(int samples) {
        const auto decay = std::pow(meterRelease, samples);
        meterGainL = 1.0 + (meterGainL - 1.0) * decay;
        meterGainR = 1.0 + (meterGainR - 1.0) * decay;
    }
private:
    void observeGain(double left, double right, double dryL, double dryR) {
        auto follow = [this](double gain, double dry, double& envelope) {
            // Return the needle to rest when there is no audible input.
            const auto target = std::abs(dry) < 1.0e-9 ? 1.0 : std::clamp(gain, 0.0, 1.0);
            const auto coefficient = target < envelope ? meterAttack : meterRelease;
            envelope = target + coefficient * (envelope - target);
        };
        follow(left, dryL, meterGainL);
        follow(right, dryR, meterGainR);
    }
    double meterGainL=1, meterGainR=1;
    double meterAttack=std::exp(-1.0/(44100.0*.012));
    double meterRelease=std::exp(-1.0/(44100.0*.280));
    double getSampleRate() const { return sampleRate; }
    double sampleRate=44100;
    double controlAposL=1, controlAnegL=1, controlBposL=1, controlBnegL=1;
    double targetposL=1, targetnegL=1, lastOutputL=0;
    double controlAposR=1, controlAnegR=1, controlBposR=1, controlBnegR=1;
    double targetposR=1, targetnegR=1, lastOutputR=0;
    bool flip=false;
    uint32_t fpdL=0x12345678u, fpdR=0x87654321u;
    uint32_t noisesourceL=0, noisesourceR=850010;
    float A=0, B=0.5f, C=1;
};
