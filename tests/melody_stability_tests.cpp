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
    for (const auto hop : {5,10,20}) {
        std::vector<RawPitchEstimate> frames;
        for (int t=0;t<2000;t+=hop) {
            const bool weak=t>=600 && t<760;
            const double pitch=t>=1100 && t<1320 ? 81.0 : 69.0;
            frames.push_back({weak?PitchEstimateStatus::no_periodic_candidate:PitchEstimateStatus::reliable,
                static_cast<std::uint64_t>(t),weak?0.0:440.0*std::pow(2.0,(pitch-69)/12),weak?.55:.98,.12});
        }
        auto notes=HysteresisNoteSegmenter{}.segment(frames,1000,40,hop,2.04,440,{});
        check(notes.size()==1 && notes[0].pitch==69 && notes[0].duration_seconds>1.95,
            "voiced dropout and returning octave error do not cut a sustain");
        for(auto& frame:frames) {
            if(frame.frame_start_sample>=600 && frame.frame_start_sample<760) {
                frame.status=PitchEstimateStatus::below_gate;frame.rms_level=0;
            }
        }
        notes=HysteresisNoteSegmenter{}.segment(frames,1000,40,hop,2.04,440,{});
        check(notes.size()==2 && notes[1].start_seconds-notes[0].start_seconds-notes[0].duration_seconds>.14,
            "a true 160-ms rest still separates repeated notes");
        for(auto& frame:frames) {
            frame.status=PitchEstimateStatus::reliable;frame.rms_level=.12;
            frame.frequency_hz=frame.frame_start_sample>=600 && frame.frame_start_sample<1000 ? 880 : 440;
        }
        notes=HysteresisNoteSegmenter{}.segment(frames,1000,40,hop,2.04,440,{});
        check(notes.size()==3 && notes[1].pitch==81, "a deliberate held octave and return remain three notes");
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
    std::vector<float> voice;
    double vocal_phase=0;
    for (int i=0;i<rate*3;++i) {
        const double t=static_cast<double>(i)/rate;
        const double frequency=146.832*std::pow(2.0,25*std::sin(2*std::numbers::pi*5*t)/1200);
        vocal_phase+=2*std::numbers::pi*frequency/rate;
        const double amplitude=.10*(1-.90*t/3);
        const bool harmonic_error=t>1.4 && t<1.62;
        const double fundamental=harmonic_error?0:std::sin(vocal_phase);
        voice.push_back(t<.1 || t>2.9 ? 0.0F : static_cast<float>(amplitude*(fundamental+.8*std::sin(2*vocal_phase)+.3*std::sin(3*vocal_phase))));
    }
    PitchDetectorConfig vocal_config;
    vocal_config.minimum_frequency_hz=55;vocal_config.input_gate_rms=.004F;
    const auto vocal=pipeline.convert(AudioRecording({rate,1},std::move(voice)),vocal_config,{},120,480);
    check(vocal.notes.size()==1 && vocal.notes[0].pitch==50 && vocal.notes[0].duration_seconds>2.65,
        "harmonic-rich fading hum retains its sustained pitch and quiet tail through actual YIN");
    auto invalid=NoteExtractionConfig{};
    invalid.maximum_voiced_gap_seconds=-1;
    check(!invalid.isValid(), "negative voiced-gap limits are rejected");
    invalid=NoteExtractionConfig{};invalid.maximum_octave_glitch_seconds=2;
    check(!invalid.isValid(), "unbounded octave repair is rejected");
    std::vector<RawPitchEstimate> unordered{{PitchEstimateStatus::reliable,100,440,.98,.2},
        {PitchEstimateStatus::reliable,100,440,.98,.2}};
    check(HysteresisNoteSegmenter{}.segment(unordered,1000,40,10,1,440,{}).empty(),
        "duplicate evidence timestamps cannot produce invalid interpolation");
    std::cout << "Harmonic fixture notes: " << vocal.notes.size() << '\n';
    for(const auto& n:vocal.notes) std::cout << static_cast<int>(n.pitch)<<' '<<n.start_seconds<<' '<<n.duration_seconds<<'\n';
    return failures?1:0;
}
