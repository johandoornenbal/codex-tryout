#include "LoudnessMatch.h"
#include "ebur128.h"
#include <iostream>
#include <vector>
#include <stdexcept>
void check(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
int main() try {
    for(double rate:{44100.,48000.,96000.,192000.}) {
        LoudnessMatch match; match.prepare(rate); match.start();
        auto* reference=ebur128_init(2,static_cast<unsigned long>(rate),EBUR128_MODE_I);
        check(reference!=nullptr,"Cannot initialise reference LUFS meter");
        std::vector<double> buffer(2048);
        for(int base=0;base<static_cast<int>(rate*16);base+=1024) {
            int frames=std::min(1024,static_cast<int>(rate*16)-base);
            for(int i=0;i<frames;++i) {
                const auto t=(base+i)/rate;
                const double level=t<4?.03:t<8?.3:.1;
                const auto left=level*std::sin(t*2*3.141592653589793*997);
                const auto right=level*.6*std::sin(t*2*3.141592653589793*213);
                buffer[static_cast<size_t>(i*2)]=left; buffer[static_cast<size_t>(i*2+1)]=right;
                match.sample(left,right,left*2,right*2);
                if(match.complete()) break;
            }
            // Reference only the same 15.3 s measurement interval.
            const auto remaining=static_cast<int>(std::lround(rate*15.3))-base;
            if(remaining>0) ebur128_add_frames_double(reference,buffer.data(),static_cast<size_t>(std::min(frames,remaining)));
            if(match.complete()) break;
        }
        check(match.complete(),"Learning did not auto-finish");
        check(match.finish()==LoudnessMatch::Result::success,"Failed to learn known gain");
        check(std::abs(match.trimDb+6.020599913)<1e-6,"Incorrect learned 2x compensation");
        double lufs=0; ebur128_loudness_global(reference,&lufs); ebur128_destroy(&reference);
        check(std::abs(match.inputLufs-lufs)<.06,"LUFS disagrees with independent libebur128 reference");
        std::cout<<"rate "<<rate<<": input "<<match.inputLufs<<", reference "<<lufs<<", trim "<<match.trimDb<<'\n';
        match.start();
        for(int i=0;i<static_cast<int>(rate);++i) match.sample(0,0,0,0);
        check(match.seconds()==0,"Silence did not pause learning");
        check(match.finish()==LoudnessMatch::Result::tooShort,"Silent learn should fail without a result");
        match.start();
        for(int i=0;i<static_cast<int>(rate*4);++i) {
            const auto x=.1*std::sin(i*.1); match.sample(x,x,0,0);
        }
        check(match.finish()==LoudnessMatch::Result::noOutput,"Muted output must not produce infinite gain");
        match.start();
        for(int i=0;i<static_cast<int>(rate*4);++i) {
            const auto x=.1*std::sin(i*.1); match.sample(x,x,x*.03,x*.03);
        }
        check(match.finish()==LoudnessMatch::Result::limited && match.trimDb==24,"Extreme gain not bounded");
    }
    // Relative gating must discard blocks 30 dB below the loud programme.
    double energy[]={.1,.1,.0001,.0001};
    check(std::abs(LoudnessMatch::integrated(energy,4)-(-10.691))<1e-8,"Relative gate wrong");
    std::cout<<"LUFS reference, known gain, silence, muted output, gain limit and relative-gate tests passed\n";
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
