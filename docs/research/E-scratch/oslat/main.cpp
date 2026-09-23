#include "AppConfig.h"
#include <juce_dsp/juce_dsp.h>
#include <cstdio>
#include <chrono>
using OS = juce::dsp::Oversampling<float>;
int main() {
  for (int type = 0; type < 2; ++type)
  for (int maxQ = 0; maxQ < 2; ++maxQ)
  for (int f = 1; f <= 3; ++f) {
    OS os(2, (size_t) f, type == 0 ? OS::filterHalfBandFIREquiripple : OS::filterHalfBandPolyphaseIIR, maxQ != 0, false);
    os.initProcessing(512);
    OS os2(2, (size_t) f, type == 0 ? OS::filterHalfBandFIREquiripple : OS::filterHalfBandPolyphaseIIR, maxQ != 0, true);
    os2.initProcessing(512);
    // time up+down for 1 s of stereo at 48k
    juce::AudioBuffer<float> buf(2, 512); buf.clear();
    for (int i=0;i<512;++i){ buf.setSample(0,i,0.1f*std::sin(0.05f*i)); buf.setSample(1,i,0.1f*std::cos(0.05f*i)); }
    auto t0 = std::chrono::high_resolution_clock::now();
    int blocks = 48000*20/512;
    for (int b=0;b<blocks;++b){ juce::dsp::AudioBlock<float> blk(buf); auto up = os.processSamplesUp(blk); os.processSamplesDown(blk);} 
    auto t1 = std::chrono::high_resolution_clock::now();
    double ns = std::chrono::duration<double,std::nano>(t1-t0).count() / (blocks*512.0*2.0);
    std::printf("%s maxQ=%d x%d latency=%.3f (int %.0f) up+down ns/sample/ch=%.2f\n", type==0?"FIR":"IIR", maxQ, 1<<f, (double) os.getLatencyInSamples(), (double) os2.getLatencyInSamples(), ns);
  }
}
