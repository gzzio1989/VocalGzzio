// ポップ/リップ除去のロジック検証（JUCE非依存の再実装で、同じ式・同じ定数を使う）
#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>
static const double SR = 48000.0;
struct Biquad {
    double b0=1,b1=0,b2=0,a1=0,a2=0, z1=0,z2=0;
    float process(float x){ double y=b0*x+z1; z1=b1*x-a1*y+z2; z2=b2*x-a2*y; return (float)y; }
    void reset(){z1=z2=0;}
    void setLP(double f,double q){ double w=2*M_PI*f/SR,a=sin(w)/(2*q),c=cos(w),n=1+a;
        b0=(1-c)/2/n; b1=(1-c)/n; b2=b0; a1=-2*c/n; a2=(1-a)/n; }
    void setHP(double f,double q){ double w=2*M_PI*f/SR,a=sin(w)/(2*q),c=cos(w),n=1+a;
        b0=(1+c)/2/n; b1=-(1+c)/n; b2=b0; a1=-2*c/n; a2=(1-a)/n; }
    void setBP(double f,double q){ double w=2*M_PI*f/SR,a=sin(w)/(2*q),c=cos(w),n=1+a;
        b0=a/n; b1=0; b2=-a/n; a1=-2*c/n; a2=(1-a)/n; }
};
static float tc(float ms){ return 1.0f-std::exp(-1.0f/(ms*0.001f*(float)SR)); }

struct Proc {
    Biquad popHp[2], lipLp[2], popDet, lipDet;
    float popLfFast=0,popLfSlow=0,popMid=0,popG=0, lipFast=0,lipSlow=0,lipBody=0,lipG=0;
    float pFA=tc(1.5f),pFR=tc(45),pSA=tc(80),pSR=tc(250),pMA=tc(10),pMR=tc(120),pGA=tc(1.5f),pGR=tc(90);
    float lFA=tc(0.15f),lFR=tc(6),lSA=tc(40),lSR=tc(150),lBA=tc(5),lBR=tc(140),lGA=tc(0.4f),lGR=tc(22);
    float maxPopG=0,maxLipG=0;
    Proc(){ popHp[0].setHP(190,0.707); popHp[1].setHP(190,0.707);
            lipLp[0].setLP(2200,0.707); lipLp[1].setLP(2200,0.707);
            popDet.setLP(120,0.707); lipDet.setBP(3500,0.7); }
    float tick(float x, float popAmt, float lipAmt){
        float gPop=0;
        if(popAmt>0.001f){
            float lf=std::fabs(popDet.process(x)); float mid=std::fabs(x)-lf;
            popLfFast += (lf>popLfFast?pFA:pFR)*(lf-popLfFast);
            popLfSlow += (lf>popLfSlow?pSA:pSR)*(lf-popLfSlow);
            popMid    += (mid>popMid?pMA:pMR)*(mid-popMid);
            if(popLfFast>2.0e-4f){
                float burst=popLfFast/(popLfSlow*2.2f+1e-6f)-1.0f;
                float domin=popLfFast/(popMid*1.1f+1e-6f)-1.0f;
                gPop=std::clamp(burst,0.f,1.f)*std::clamp(domin,0.f,1.f);
            }
            gPop*=popAmt;
        }
        popG += (gPop>popG?pGA:pGR)*(gPop-popG);
        maxPopG=std::max(maxPopG,popG);
        float gLip=0;
        if(lipAmt>0.001f){
            float hf=std::fabs(lipDet.process(x)); float bd=std::fabs(x);
            lipFast += (hf>lipFast?lFA:lFR)*(hf-lipFast);
            lipSlow += (hf>lipSlow?lSA:lSR)*(hf-lipSlow);
            lipBody += (bd>lipBody?lBA:lBR)*(bd-lipBody);
            if(lipFast>1.0e-4f && lipBody<0.06f){
                float spike=lipFast/(lipSlow*3.5f+1e-6f)-1.0f;
                gLip=std::clamp(spike,0.f,1.f);
            }
            gLip*=lipAmt;
        }
        lipG += (gLip>lipG?lGA:lGR)*(gLip-lipG);
        maxLipG=std::max(maxLipG,lipG);
        float v=x;
        if(popG>0.0005f){ float h=popHp[1].process(popHp[0].process(v)); v+=popG*(h-v); }
        else { popHp[0].process(v); popHp[1].process(v); }
        if(lipG>0.0005f){ float l=lipLp[1].process(lipLp[0].process(v)); v+=lipG*(l-v); }
        else { lipLp[0].process(v); lipLp[1].process(v); }
        return v;
    }
};
static double rms(const std::vector<float>&v,int a,int b){ double s=0; for(int i=a;i<b;++i) s+=v[i]*v[i]; return std::sqrt(s/std::max(1,b-a)); }

int main(){
    const int N=(int)(SR*3);
    // --- signal: 2s of sung note (150Hz fundamental + harmonics), a pop at 0.5s, a click at 2.5s (silence) ---
    std::vector<float> sig(N,0.f);
    for(int i=0;i<N;++i){
        double t=i/SR;
        if(t>0.2&&t<2.0){ // singing
            double env=0.35;
            sig[i]+= (float)(env*(0.6*sin(2*M_PI*150*t)+0.35*sin(2*M_PI*300*t)+0.25*sin(2*M_PI*600*t)+0.15*sin(2*M_PI*1800*t)));
        }
        if(t>=0.5&&t<0.58){ // plosive: LF burst 60Hz decaying
            double a=std::exp(-(t-0.5)/0.02);
            sig[i]+=(float)(0.9*a*sin(2*M_PI*60*(t-0.5)));
        }
        if(t>=2.5&&t<2.508){ // mouth click: short HF burst in silence
            double a=std::exp(-(t-2.5)/0.0015);
            sig[i]+=(float)(0.25*a*sin(2*M_PI*3500*(t-2.5)));
        }
    }
    auto run=[&](float pa,float la){
        Proc p; std::vector<float> o(N);
        for(int i=0;i<N;++i) o[i]=p.tick(sig[i],pa,la);
        printf("  maxPopG=%.2f maxLipG=%.2f\n",p.maxPopG,p.maxLipG);
        return o;
    };
    printf("OFF (0%%):\n"); auto off=run(0,0);
    printf("ON (100%%):\n"); auto on=run(1,1);

    int p0=(int)(SR*0.50),p1=(int)(SR*0.58);      // pop window
    int s0=(int)(SR*1.0),s1=(int)(SR*1.8);        // steady singing
    int c0=(int)(SR*2.50),c1=(int)(SR*2.53);      // click window
    int q0=(int)(SR*2.1),q1=(int)(SR*2.4);        // silence

    double popOff=rms(off,p0,p1), popOn=rms(on,p0,p1);
    double sinOff=rms(off,s0,s1), sinOn=rms(on,s0,s1);
    double cliOff=rms(off,c0,c1), cliOn=rms(on,c0,c1);
    double qOff=rms(off,q0,q1),  qOn=rms(on,q0,q1);
    auto db=[](double a,double b){ return 20*log10((b+1e-12)/(a+1e-12)); };
    printf("pop window    : %+.1f dB (should be clearly negative)\n", db(popOff,popOn));
    printf("sung steady   : %+.1f dB (should be ~0: must not touch singing)\n", db(sinOff,sinOn));
    printf("click window  : %+.1f dB (should be negative)\n", db(cliOff,cliOn));
    printf("silence       : %+.1f dB (should be ~0)\n", db(qOff,qOn));
    bool pass = db(popOff,popOn) < -4.0 && std::fabs(db(sinOff,sinOn)) < 0.6
             && db(cliOff,cliOn) < -3.0 && std::fabs(db(qOff,qOn)) < 1.0;
    // --- 単独のポップ音だけで、実際の減衰量を測る ---
    {
        std::vector<float> only(N,0.f);
        for(int i=0;i<N;++i){ double t=i/SR;
            if(t>=0.5&&t<0.60){ double a=std::exp(-(t-0.5)/0.02);
                only[i]=(float)(0.9*a*sin(2*M_PI*60*(t-0.5))); } }
        Proc pa; std::vector<float> oa(N); for(int i=0;i<N;++i) oa[i]=pa.tick(only[i],1,0);
        double a=rms(only,p0,(int)(SR*0.60)), b=rms(oa,p0,(int)(SR*0.60));
        printf("pop alone     : %+.1f dB\n", 20*log10((b+1e-12)/(a+1e-12)));
    }
    printf(pass?"PASS\n":"FAIL\n");
    return pass?0:1;
}
