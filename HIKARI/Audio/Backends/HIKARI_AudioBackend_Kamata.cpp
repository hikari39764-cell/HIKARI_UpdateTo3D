#include "HIKARI_AudioBackend_Kamata.h"

#include <KamataEngine.h>

namespace HIKARI::AUDIO {

namespace {
    KamataEngine::Audio* gAudio = nullptr;
}

bool AudioBackendKamata::Initialize() {
    gAudio = KamataEngine::Audio::GetInstance();
    if (!gAudio) {
        return false;
    }
    gAudio->Initialize("./NoviceResources/");
    return true;
}

void AudioBackendKamata::Shutdown() {
    gAudio = nullptr;
}

int AudioBackendKamata::Load(const char* path) {
    return gAudio ? static_cast<int>(gAudio->LoadWave(path)) : -1;
}

int AudioBackendKamata::Play(int soundHandle, bool loop, float volume) {
    return gAudio ? static_cast<int>(gAudio->PlayWave(static_cast<uint32_t>(soundHandle), loop, volume)) : -1;
}

void AudioBackendKamata::Stop(int playHandle) {
    if (gAudio) {
        gAudio->StopWave(static_cast<uint32_t>(playHandle));
    }
}

void AudioBackendKamata::Pause(int playHandle) {
    if (gAudio) {
        gAudio->PauseWave(static_cast<uint32_t>(playHandle));
    }
}

void AudioBackendKamata::Resume(int playHandle) {
    if (gAudio) {
        gAudio->ResumeWave(static_cast<uint32_t>(playHandle));
    }
}

void AudioBackendKamata::SetVolume(int playHandle, float volume) {
    if (gAudio) {
        gAudio->SetVolume(static_cast<uint32_t>(playHandle), volume);
    }
}

bool AudioBackendKamata::IsPlaying(int playHandle) {
    return gAudio ? gAudio->IsPlaying(static_cast<uint32_t>(playHandle)) : false;
}

} // namespace HIKARI::AUDIO
