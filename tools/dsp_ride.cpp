// 音量キープ(自動ゲインライド)の検証。PluginProcessor.cpp と同じ式・同じ定数。
//   g++ -O2 -std=c++17 dsp_ride.cpp -o dsp_ride && ./dsp_ride
// 合格条件:
//   A) 小声(-30dB台)と大声(-8dB台)のフレーズ差が 1/3 以下に縮む
//   B) 補正は±9dBを超えない
//   C) 無音区間のノイズ床(-70dB)が聞こえる音量まで持ち上がらない(出力< -55dB)
#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>

static const double SR = 48000.0;
static float tc (float ms) { return 1.0f - std::exp (-1.0f / (ms * 0.001f * (float) SR)); }

static std::vector<float> voice (double f0, double sec, double amp)
{
    int N = (int)(sec*SR); std::vector<float> v(N); double ph[40]={};
    unsigned rng=777;
    for (int n=0;n<N;++n){
        double s=0;
        for(int h=1;h<=30;++h){ double fh=f0*h; if(fh>10000)break;
            ph[h]+=2*M_PI*fh/SR; if(ph[h]>2*M_PI)ph[h]-=2*M_PI; s+=std::sin(ph[h])/std::pow(h,1.2);}
        rng=rng*1664525u+1013904223u;
        v[n]=(float)(amp*s*0.3*(0.8+0.2*std::sin(2*M_PI*0.9*n/SR)));
    }
    return v;
}
static double rmsDb(const std::vector<float>&x,int a,int b){
    double s=0;int c=0;for(int n=a;n<b&&n<(int)x.size();++n){s+=(double)x[n]*x[n];++c;}
    return 10*std::log10(s/std::max(1,c)+1e-20);
}

int main()
{
    // 構成: 小声3s → 大声3s → 無音(微小ノイズ)2s
    auto quiet = voice (140, 3.0, 0.25);   // 実際の歌の「小さいフレーズ」相当(-27dB台)
    auto loud  = voice (140, 3.0, 0.85);
    std::vector<float> in;
    in.insert(in.end(),quiet.begin(),quiet.end());
    in.insert(in.end(),loud.begin(),loud.end());
    {   unsigned rng=42; int N=(int)(2.0*SR);
        for(int n=0;n<N;++n){ rng=rng*1664525u+1013904223u;
            in.push_back((float)(((int)(rng>>9)/4194304.0-1.0)*3.16e-4)); } // -70dB
    }

    // ---- 本体と同じ処理 ----
    const float rideRmsA=tc(300.0f), rideSlewA=tc(700.0f), amt=1.0f, maxDb=9.0f;
    float env2=0, gDb=0, minG=0, maxG=0;
    std::vector<float> out(in.size());
    for (size_t n=0;n<in.size();++n){
        float x=in[n];
        env2 += rideRmsA*(x*x-env2);
        float rms=10.0f*std::log10(env2+1e-12f);
        if (rms>-45.0f){
            float want=std::clamp(-18.0f-rms,-maxDb,maxDb)*amt;
            gDb += rideSlewA*(want-gDb);
        }
        minG=std::min(minG,gDb); maxG=std::max(maxG,gDb);
        out[n]=x*std::pow(10.0f,gDb/20.0f);
    }

    const int s1=(int)(1.5*SR), e1=(int)(3.0*SR);          // 小声の後半
    const int s2=(int)(4.5*SR), e2=(int)(6.0*SR);          // 大声の後半
    const int s3=(int)(6.7*SR), e3=(int)(8.0*SR);          // 無音の後半
    const double spreadIn  = rmsDb(in,s2,e2)-rmsDb(in,s1,e1);
    const double spreadOut = rmsDb(out,s2,e2)-rmsDb(out,s1,e1);
    const double noiseOut  = rmsDb(out,s3,e3);
    printf("=== 音量キープ 検証 ===\n");
    printf("[A] フレーズ差: %.1f dB -> %.1f dB (1/3以下で合格)\n", spreadIn, spreadOut);
    printf("[B] 補正範囲: %.1f 〜 +%.1f dB (±9以内)\n", minG, maxG);
    printf("[C] 無音区間の出力: %.1f dB (< -55)\n", noiseOut);
    bool ok = (spreadOut < spreadIn/3.0) && (minG>=-9.01f && maxG<=9.01f) && (noiseOut < -55.0);
    printf("=== %s ===\n", ok?"ぜんぶ合格":"!! 不合格あり !!");
    return ok?0:1;
}
