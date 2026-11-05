#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

// Stereo BS.1770 K-weighting and integrated absolute/relative gating.
// Filter design follows libebur128 (MIT, Jan Kokemueller); see THIRD_PARTY.md.
// Bounded storage; prepare, start, and sample processing never allocate.
class LoudnessMatch {
    struct Biquad {
        double b0=1,b1=0,b2=0,a1=0,a2=0,z1=0,z2=0;
        double tick(double x) { const auto y=b0*x+z1; z1=b1*x-a1*y+z2; z2=b2*x-a2*y; return y; }
        void clear() { z1=z2=0; }
    };
    struct Weighting {
        Biquad shelf,highpass;
        void prepare(double rate) {
            constexpr double pi=3.14159265358979323846;
            auto k=std::tan(pi*1681.974450955533/rate);
            auto q=.7071752369554196, vh=std::pow(10.,3.999843853973347/20.), vb=std::pow(vh,.4996667741545416);
            auto a=1+k/q+k*k;
            shelf={(vh+vb*k/q+k*k)/a,2*(k*k-vh)/a,(vh-vb*k/q+k*k)/a,2*(k*k-1)/a,(1-k/q+k*k)/a,0,0};
            k=std::tan(pi*38.13547087602444/rate); q=.5003270373238773; a=1+k/q+k*k;
            highpass={1,-2,1,2*(k*k-1)/a,(1-k/q+k*k)/a,0,0};
        }
        double tick(double x) { return highpass.tick(shelf.tick(x)); }
        void clear() { shelf.clear(); highpass.clear(); }
    };
public:
    enum class Result { none, success, tooShort, noOutput, limited };
    void prepare(double rate) {
        step=std::max(1,static_cast<int>(std::lround(rate*.1)));
        for(auto& f:filters) f.prepare(rate);
        start(); active=false;
    }
    void start() {
        for(auto& f:filters) f.clear();
        inBins.fill(0); outBins.fill(0); input.fill(0); output.fill(0);
        bin=0; frames=0; completedBins=0; count=0; inSum=outSum=0; active=true;
    }
    void cancel() { active=false; }
    bool learning() const { return active; }
    float seconds() const { return static_cast<float>(count)*.1f; }
    bool complete() const { return count==capacity; }
    void sample(double dryL,double dryR,double wetL,double wetR) {
        if(!active || complete()) return;
        const double a=filters[0].tick(dryL),b=filters[1].tick(dryR);
        const double c=filters[2].tick(wetL),d=filters[3].tick(wetR);
        inSum+=a*a+b*b; outSum+=c*c+d*d;
        if(++frames<step) return;
        inBins[static_cast<size_t>(bin)]=inSum/step; outBins[static_cast<size_t>(bin)]=outSum/step;
        bin=(bin+1)%4; frames=0; inSum=outSum=0;
        if(++completedBins<4) return;
        double in=0,out=0;
        for(int i=0;i<4;++i) { in+=inBins[static_cast<size_t>(i)]*.25; out+=outBins[static_cast<size_t>(i)]*.25; }
        // Pause the learning clock on windows below the BS.1770 absolute gate.
        if(!std::isfinite(in)||!std::isfinite(out)||in<absoluteGate()) return;
        input[static_cast<size_t>(count)]=in; output[static_cast<size_t>(count)]=out; ++count;
    }
    Result finish() {
        active=false;
        if(count<30) return Result::tooShort;
        inputLufs=integrated(input.data(),count); outputLufs=integrated(output.data(),count);
        if(!std::isfinite(inputLufs)||!std::isfinite(outputLufs)) return Result::noOutput;
        const auto difference=inputLufs-outputLufs;
        trimDb=std::clamp(difference,-24.,24.);
        return difference>=-24. && difference<=24.?Result::success:Result::limited;
    }
    static double integrated(const double* energy,int size) {
        double total=0; int accepted=0;
        for(int i=0;i<size;++i) if(energy[i]>=absoluteGate()) { total+=energy[i]; ++accepted; }
        if(!accepted) return -std::numeric_limits<double>::infinity();
        const auto threshold=std::max(absoluteGate(),total/accepted*.1);
        total=0; accepted=0;
        for(int i=0;i<size;++i) if(energy[i]>=threshold) { total+=energy[i]; ++accepted; }
        return accepted?-.691+10*std::log10(total/accepted):-std::numeric_limits<double>::infinity();
    }
    double trimDb=0,inputLufs=0,outputLufs=0;
private:
    static double absoluteGate() { return std::pow(10.,(-70.+.691)/10.); }
    static constexpr int capacity=150; // Fifteen seconds of accepted 100 ms steps.
    std::array<Weighting,4> filters;
    std::array<double,4> inBins{},outBins{};
    std::array<double,capacity> input{},output{};
    int step=4800,frames=0,bin=0,completedBins=0,count=0;
    double inSum=0,outSum=0;
    bool active=false;
};
