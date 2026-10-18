#include "PlaybackBuffer.hpp"
#include "hum/core/loop_range.hpp"
#include <iostream>
#include <cmath>
int main() {
    int failures=0;
    auto check=[&](bool ok,const char* text){if(!ok){++failures;std::cerr<<"FAIL "<<text<<'\n';}};
    const QByteArray pcm("00AABBCCDD99");
    hum::app::MelodyBuffer loop(pcm,true,nullptr,2,10);
    loop.open(QIODevice::ReadOnly|QIODevice::Unbuffered);
    check(loop.read(20)==QByteArray("AABBCCDDAABBCCDDAABB"),"loop pulls cross multiple boundaries without leaking audio outside the range");
    loop.setOffset(6);
    check(loop.read(12)==QByteArray("CCDDAABBCCDD"),"seek inside loop preserves frame alignment");
    check(!loop.atEnd() && loop.bytesAvailable()>0,"loop never signals end or starvation at a wrap");
    hum::app::MelodyBuffer full(pcm,true); full.open(QIODevice::ReadOnly|QIODevice::Unbuffered);
    check(full.read(24)==pcm+pcm,"whole-buffer looping still works");
    hum::app::MelodyBuffer finite(pcm,false);finite.open(QIODevice::ReadOnly|QIODevice::Unbuffered);
    check(finite.read(100)==pcm && finite.atEnd(),"normal playback remains finite");
    hum::app::MelodyBuffer empty(pcm,true,nullptr,4,4);empty.open(QIODevice::ReadOnly|QIODevice::Unbuffered);
    check(empty.read(20).isEmpty() && empty.atEnd(),"empty ranges cannot spin in the audio callback");
    const hum::core::LoopRange range{2,4};
    check(range.isValid() && std::abs(range.wrap(4.25)-2.25)<1e-9 && std::abs(range.wrap(2.75)-2.75)<1e-9,
        "audio-clock positions wrap correctly while retaining a paused position");
    check(!(hum::core::LoopRange{2,2}).isValid() && !(hum::core::LoopRange{-1,2}).isValid(),"invalid loop ranges are rejected");
    return failures?1:0;
}
