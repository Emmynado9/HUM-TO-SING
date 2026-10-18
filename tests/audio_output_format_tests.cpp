#include "AudioOutputFormat.hpp"
#include <iostream>
int main() {
    int failures=0;
    auto expect=[&](bool ok,const char* message){if(!ok){++failures;std::cerr<<message<<'\n';}};
    QAudioFormat preferred;preferred.setSampleRate(44100);preferred.setChannelCount(2);
    const auto floatOnly=[](const QAudioFormat& f){return f.sampleRate()==44100 && f.channelCount()==2 && f.sampleFormat()==QAudioFormat::Float;};
    const auto repaired=hum::app::chooseOutputFormat(preferred,floatOnly);
    expect(repaired && repaired->isValid() && repaired->sampleFormat()==QAudioFormat::Float,
        "Unknown preferred sample format must negotiate supported Float PCM");
    preferred.setSampleFormat(QAudioFormat::Int16);
    const auto preserved=hum::app::chooseOutputFormat(preferred,[](const QAudioFormat&){return true;});
    expect(preserved && *preserved==preferred,"Already-supported preferred format must be preserved");
    const auto stereo=hum::app::chooseOutputFormat(QAudioFormat{},[](const QAudioFormat& f){
        return f.sampleRate()==48000 && f.channelCount()==2 && f.sampleFormat()==QAudioFormat::Int16;});
    expect(stereo && stereo->sampleRate()==48000 && stereo->channelCount()==2
        && stereo->sampleFormat()==QAudioFormat::Int16,"Invalid preferred format must fall back to supported rate, channels and PCM type");
    const auto absent=hum::app::chooseOutputFormat(preferred,[](const QAudioFormat&){return false;});
    expect(!absent,"Unsupported output must fail explicitly");
    preferred.setChannelCount(16);
    const auto bounded=hum::app::chooseOutputFormat(preferred,[](const QAudioFormat& f){return f.channelCount()==2;});
    expect(bounded && bounded->channelCount()==2,"Output negotiation must respect render channel bounds");
    std::cout<<"Audio output format regressions: "<<failures<<" failure(s)\n";
    return failures?1:0;
}
