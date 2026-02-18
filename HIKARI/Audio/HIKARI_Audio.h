#pragma once

#include <memory>

namespace HIKARI::AUDIO {

class IAudioBackend;

enum class BackendType {
    Novice
};

bool Initialize(BackendType type = BackendType::Novice);
void Shutdown();

int Load(const char* path);
int Play(int soundHandle, bool loop, float volume);
void Stop(int playHandle);
void Pause(int playHandle);
void Resume(int playHandle);
void SetVolume(int playHandle, float volume);
bool IsPlaying(int playHandle);

} // namespace HIKARI::AUDIO
