#pragma once

#include "../HIKARI_IAudioBackend.h"

namespace HIKARI::AUDIO {

class AudioBackendNovice : public IAudioBackend {
public:
    bool Initialize() override;
    void Shutdown() override;

    int Load(const char* path) override;
    int Play(int soundHandle, bool loop, float volume) override;
    void Stop(int playHandle) override;
    void Pause(int playHandle) override;
    void Resume(int playHandle) override;
    void SetVolume(int playHandle, float volume) override;
    bool IsPlaying(int playHandle) override;
};

} // namespace HIKARI::AUDIO
