#include "hum/core/note_extraction.hpp"
#include <cmath>
#include <iostream>
#include <numbers>
using namespace hum::core;
int failures=0;
void check(bool ok, const char* text) { if(!ok){++failures;std::cerr<<"FAIL "<<text<<'\n';} }
template<class Pitch, class Voiced>
std::vector<TimedMidiNote> segment(Pitch pitch, Voiced voiced, int hop=10) {
    std::vector<RawPitchEstimate> frames;
    for(int t=0;t<1600;t+=hop) {
        const auto reliable=voiced(t);
        frames.push_back({reliable?PitchEstimateStatus::reliable:PitchEstimateStatus::below_gate,
            static_cast<std::uint64_t>(t),reliable?440.0*std::pow(2.0,(pitch(t)-69.0)/12.0):0.0,.98,.2});
    }
    return HysteresisNoteSegmenter{}.segment(frames,1000,40,static_cast<std::size_t>(hop),1.64,440,{});
}
int main() {
    for (const auto hop : {5,10,20}) {
        const auto stable=segment([](int t){
            if(t>=900 && t<1020) return 81.0;
            return t<150?69.0:69.0+.85*std::sin(2*std::numbers::pi*5*t/1000.0);
        },[](int t){return !(t>=300 && t<380);},hop);
        check(stable.size()==1 && stable[0].pitch==69 && stable[0].duration_seconds>1.5,
            "vibrato, brief loss and short octave errors retain the sustained note at different hop sizes");
        const auto changed=segment([](int t){return t<800?69.0:70.0;},[](int){return true;},hop);
        check(changed.size()==2 && changed[0].pitch==69 && changed[1].pitch==70,
            "an intentional semitone change is retained");
        check(changed.size()==2 && std::abs(changed[1].start_seconds-.815)<.025,
            "confirmed changes keep the original transition timing rather than adding confirmation latency");
        const auto octave=segment([](int t){return t<800?69.0:81.0;},[](int){return true;},hop);
        check(octave.size()==2 && octave[1].pitch==81,"a sustained intentional octave change is retained");
        const auto rests=segment([](int){return 69.0;},[](int t){return t<700||t>=950;},hop);
        check(rests.size()==2 && rests[1].start_seconds-rests[0].start_seconds-rests[0].duration_seconds>.2,
            "real rests still separate repeated notes");
    }
    std::vector<float> samples;
    double phase=0;
    constexpr int rate=48000;
    for(int i=0;i<rate*2;++i) {
        const double t=static_cast<double>(i)/rate;
        const double cents=t<.35?0:80*std::sin(2*std::numbers::pi*5*t);
        phase+=2*std::numbers::pi*440*std::pow(2.0,cents/1200)/rate;
        const bool voiced=t>=.15 && t<1.8 && !(t>=.8&&t<.86);
        samples.push_back(voiced?static_cast<float>(.3*std::sin(phase)):0.0F);
    }
    PitchDetectorConfig pitch; pitch.frame_size=2048; pitch.hop_size=256;
    const PhraseConversionPipeline pipeline(std::make_shared<YinPitchDetector>(),std::make_shared<HysteresisNoteSegmenter>());
    const auto result=pipeline.convert(AudioRecording({rate,1},std::move(samples)),pitch,{},120,480);
    check(result.notes.size()==1 && result.notes[0].pitch==69 && result.notes[0].duration_seconds>1.5,
        "actual YIN audio conversion holds a vibrating hum through a short dropout");
    return failures?1:0;
}
