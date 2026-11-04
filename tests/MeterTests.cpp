#include "ButterEngine.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
void require(bool condition,const char* message) { if(!condition) throw std::runtime_error(message); }
int main() try {
    double maximumError=0;
    for(double rate:{44100.,48000.,96000.,192000.}) {
        ButterEngine engine; engine.reset(rate); engine.parameters(1,.5f,1);
        require(engine.reductionDb(0)==0&&engine.reductionDb(1)==0,"Meter must start at zero");
        double envelopeL=1,envelopeR=1;
        const double inputGain=std::pow(10.,14./20.);
        const double makeup=1+(inputGain-1)/1.5;
        for(int i=0;i<static_cast<int>(rate*2);++i) {
            double dryL=.85+.1*std::sin(i*.043), dryR=.04+.01*std::sin(i*.031);
            double left=dryL,right=dryR;
            double* data[]={&left,&right};
            engine.processDoubleReplacing(data,data,1);
            // Reconstruct the actual gain element from rendered wet audio,
            // excluding ButterComp2's drive and automatic makeup correction.
            double gainL=std::clamp(left/dryL*makeup/inputGain,0.,1.);
            double gainR=std::clamp(right/dryR*makeup/inputGain,0.,1.);
            const auto follow=[rate](double target,double& envelope) {
                const auto coeff=std::exp(-1./(rate*(target<envelope?.012:.280)));
                envelope=target+coeff*(envelope-target);
            };
            follow(gainL,envelopeL); follow(gainR,envelopeR);
            maximumError=std::max(maximumError,std::abs(engine.reductionDb(0)+20*std::log10(envelopeL)));
            maximumError=std::max(maximumError,std::abs(engine.reductionDb(1)+20*std::log10(envelopeR)));
        }
        require(maximumError<1e-8,"Meter does not match actual applied gain");
        require(engine.reductionDb(0)>3,"Strong compression not registered");
        require(engine.reductionDb(0)>engine.reductionDb(1)+1,"Stereo meters not independent");
        // Output and Mix must not directly masquerade as compression reduction.
        // ButterComp2 intentionally feeds the final output into later recovery,
        // so compare cloned states on one sample, before that feedback diverges.
        auto dry=engine, muted=engine;
        dry.parameters(1,.5f,0); muted.parameters(1,0,1);
        double l=.9,r=.04,dl=l,dr=r,ml=l,mr=r;
        double* data[]={&l,&r}; double* dryData[]={&dl,&dr}; double* mutedData[]={&ml,&mr};
        engine.processDoubleReplacing(data,data,1);
        dry.processDoubleReplacing(dryData,dryData,1);
        muted.processDoubleReplacing(mutedData,mutedData,1);
        for(int ch=0;ch<2;++ch) {
            require(engine.reductionDb(ch)==dry.reductionDb(ch),"Mix directly altered GR reading");
            require(engine.reductionDb(ch)==muted.reductionDb(ch),"Output directly altered GR reading");
        }
        require(ml==0&&mr==0,"Mute audio semantics changed");
        engine.releaseMeters(static_cast<int>(rate*3));
        require(engine.reductionDb(0)<.001,"Bypass must release meters");
        engine.reset(rate);
        require(engine.reductionDb(0)==0&&engine.reductionDb(1)==0,"Reset did not clear meters");
        // Silence must release to rest, even while the compressor state recovers.
        engine.parameters(1,.5f,1);
        for(int i=0;i<static_cast<int>(rate);++i) {
            double a=.9,b=.3; double* d[]={&a,&b}; engine.processDoubleReplacing(d,d,1);
        }
        for(int i=0;i<static_cast<int>(rate*3);++i) {
            double a=0,b=0; double* d[]={&a,&b}; engine.processDoubleReplacing(d,d,1);
        }
        require(engine.reductionDb(0)<.001,"Silence must release meters");
        // Float and double paths must publish equivalent reduction ballistics.
        ButterEngine floatEngine,doubleEngine; floatEngine.reset(rate); doubleEngine.reset(rate);
        floatEngine.parameters(1,.5f,1); doubleEngine.parameters(1,.5f,1);
        for(int i=0;i<8192;++i) {
            float a=.9f,b=.2f; double c=a,d=b;
            float* f[]={&a,&b}; double* g[]={&c,&d};
            floatEngine.processReplacing(f,f,1); doubleEngine.processDoubleReplacing(g,g,1);
        }
        require(std::abs(floatEngine.reductionDb(0)-doubleEngine.reductionDb(0))<1e-9,"Float metering diverged");
    }
    std::cout<<"GR calibration, stereo independence, output/mix isolation, bypass/silence release, reset and float/double checks passed at four sample rates; max dB error "<<maximumError<<'\n';
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
