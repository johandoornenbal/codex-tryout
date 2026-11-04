#include "ButterEngine.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>
void require(bool ok,const char* what) { if(!ok) throw std::runtime_error(what); }
std::array<std::vector<double>,2> signal(int n,double rate) {
    std::array<std::vector<double>,2> a{std::vector<double>(n),std::vector<double>(n)};
    for(int i=0;i<n;++i) { a[0][i]=.55*std::sin(i*440*6.283185307179586/rate); a[1][i]=.3*std::sin(i*173*6.283185307179586/rate); }
    return a;
}
void run(ButterEngine& e,std::array<std::vector<double>,2>& a,int block) {
    for(size_t i=0;i<a[0].size();i+=static_cast<size_t>(block)) {
        double* ptr[]={a[0].data()+i,a[1].data()+i};
        e.processDoubleReplacing(ptr,ptr,static_cast<int>(std::min(static_cast<size_t>(block),a[0].size()-i)));
    }
}
int main() try {
    int checks=0;
    for(double rate:{44100.,48000.,96000.,192000.}) {
        auto input=signal(16384,rate);
        for(float amount:{0.f,.5f,1.f}) {
            ButterEngine a,b; a.reset(rate); b.reset(rate);
            a.parameters(amount,.5f,1); b.parameters(amount,.5f,1);
            auto x=input,y=input; run(a,x,64); run(b,y,511);
            for(int ch=0;ch<2;++ch) for(size_t i=0;i<x[ch].size();++i) {
                require(std::isfinite(x[ch][i]),"non-finite output");
                require(x[ch][i]==y[ch][i],"block-size-dependent DSP");
            }
            ++checks;
        }
        ButterEngine dry; dry.reset(rate); dry.parameters(1,1,0);
        auto x=input; run(dry,x,127);
        for(int ch=0;ch<2;++ch) for(size_t i=0;i<x[ch].size();++i)
            require(std::abs(x[ch][i]-input[ch][i])<1e-12,"dry mix changes signal");
        ++checks;
        ButterEngine mute; mute.reset(rate); mute.parameters(1,0,1);
        x=input; run(mute,x,512);
        for(auto& ch:x) for(auto v:ch) require(v==0,"output zero does not mute");
        ++checks;
        ButterEngine silent; silent.reset(rate);
        std::array<std::vector<double>,2> zero{std::vector<double>(4096),std::vector<double>(4096)};
        run(silent,zero,256);
        for(auto& ch:zero) for(auto v:ch) require(std::isfinite(v) && std::abs(v)<2e-14,"unexpected silence noise floor");
        ++checks;
        ButterEngine f; f.reset(rate); f.parameters(.7f,.5f,1);
        std::vector<float> l(input[0].begin(),input[0].end()),r(input[1].begin(),input[1].end());
        float* ptr[]={l.data(),r.data()}; f.processReplacing(ptr,ptr,static_cast<int>(l.size()));
        ButterEngine d; d.reset(rate); d.parameters(.7f,.5f,1); x=input; run(d,x,256);
        for(size_t i=0;i<l.size();++i) {
            require(std::isfinite(l[i])&&std::isfinite(r[i]),"float output not finite");
            require(std::abs(l[i]-x[0][i])<2e-6&&std::abs(r[i]-x[1][i])<2e-6,"float/double mismatch");
        }
        ++checks;
    }
    ButterEngine e; auto x=signal(512,44100); run(e,x,512); e.reset(44100);
    auto y=signal(512,44100); ButterEngine fresh; auto z=y; run(e,y,512); run(fresh,z,512);
    require(y==z,"reset not reproducible"); ++checks;
    std::cout << checks << " DSP scenarios passed (sample rates, block sizes, dry mix, mute, silence, precision, reset)\n";
    return 0;
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
