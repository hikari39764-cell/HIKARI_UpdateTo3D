#include "HIKARI_Audio.h"

#include "HIKARI_IAudioBackend.h"
#include "Backends/HIKARI_AudioBackend_Kamata.h"

namespace HIKARI::AUDIO {

static std::unique_ptr<IAudioBackend> g_backend{};

bool Initialize(BackendType type) {
    Shutdown();
    switch (type) {
    case BackendType::Kamata:
    default:
        g_backend = std::make_unique<AudioBackendKamata>();
        break;
    }
    return g_backend && g_backend->Initialize();
}

void Shutdown() {
    if (g_backend) {
        g_backend->Shutdown();
        g_backend.reset();
    }
}

int Load(const char* path) { return g_backend ? g_backend->Load(path) : -1; }
int Play(int soundHandle, bool loop, float volume) { return g_backend ? g_backend->Play(soundHandle, loop, volume) : -1; }
void Stop(int playHandle) { if (g_backend) g_backend->Stop(playHandle); }
void Pause(int playHandle) { if (g_backend) g_backend->Pause(playHandle); }
void Resume(int playHandle) { if (g_backend) g_backend->Resume(playHandle); }
void SetVolume(int playHandle, float volume) { if (g_backend) g_backend->SetVolume(playHandle, volume); }
bool IsPlaying(int playHandle) { return g_backend ? g_backend->IsPlaying(playHandle) : false; }

} // namespace HIKARI::AUDIO
