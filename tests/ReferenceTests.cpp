#include "ButterEngine.h"
#include "ReferenceEngine.h"
#include <cmath>
#include <iostream>
#include <vector>
int main() {
    double maxError=0;
    for(double rate:{44100.,48000.,96000.,192000.})
    for(float amount:{0.f,.25f,.75f,1.f})
    for(float wet:{0.f,.5f,1.f}) {
        ButterEngine port; ReferenceEngine reference;
        port.reset(rate); reference.reset(rate);
        port.parameters(amount,.7f,wet); reference.parameters(amount,.7f,wet);
        std::vector<double> l(32768),r(32768);
        for(size_t i=0;i<l.size();++i) { l[i]=.6*std::sin(i*.062); r[i]=.4*std::sin(i*.029); }
        auto a=l,b=r;
        double* actual[]={l.data(),r.data()}; double* expected[]={a.data(),b.data()};
        port.processDoubleReplacing(actual,actual,static_cast<int>(l.size()));
        reference.processDoubleReplacing(expected,expected,static_cast<int>(l.size()));
        for(size_t i=0;i<l.size();++i) {
            auto error=std::max(std::abs(l[i]-a[i]),std::abs(r[i]-b[i]));
            if(!std::isfinite(error)||error>1e-11) { std::cerr<<"Upstream comparison failed: "<<error<<'\n'; return 1; }
            maxError=std::max(maxError,error);
        }
    }
    std::cout<<"48 upstream-reference scenarios passed; max absolute error "<<maxError<<'\n';
}
