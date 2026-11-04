#pragma once
#include <cmath>
#include <cstdint>
// Adapted from Chris Johnson's MIT-licensed Airwindows ButterComp2.
class ReferenceEngine {
public:
    void reset(double rate) { *this = ReferenceEngine{}; sampleRate = rate; }
    void parameters(float compress, float output, float mix) { A=compress; B=output; C=mix; }
    void processReplacing(float**, float**, int);
    void processDoubleReplacing(double**, double**, int);
private:
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
