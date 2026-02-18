#include "HIKARI_AudioBackend_Novice.h"

#include <Novice.h>

namespace HIKARI::AUDIO {

bool AudioBackendNovice::Initialize() {
    return true;
}

void AudioBackendNovice::Shutdown() {
}

int AudioBackendNovice::Load(const char* path) {
    return Novice::LoadAudio(path);
}

int AudioBackendNovice::Play(int soundHandle, bool loop, float volume) {
    return Novice::PlayAudio(soundHandle, loop, volume);
}

void AudioBackendNovice::Stop(int playHandle) {
    Novice::StopAudio(playHandle);
}

void AudioBackendNovice::Pause(int playHandle) {
    Novice::StopAudio(playHandle);
}

void AudioBackendNovice::Resume(int playHandle) {
    (void)playHandle;
}

void AudioBackendNovice::SetVolume(int playHandle, float volume) {
    Novice::SetAudioVolume(playHandle, volume);
}

bool AudioBackendNovice::IsPlaying(int playHandle) {
    return Novice::IsPlayingAudio(playHandle);
}

} // namespace HIKARI::AUDIO
