#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <cmath>
#include "HIKARI_Renderer.h"
#include "HIKARI_Utility.h"

namespace HIKARI {
namespace MESHFX {

    enum class MeshEffectType {
        None = 0,
        Wave,
        Ripple,
        BlackHole,
        NoiseDistortion,
        Shear,
        Slice,
        Shatter,
    };

    enum class PlayState {
        Stopped = 0,
        Playing,
        Paused,
    };

    struct WaveParam {
        float amplitude = 10.0f;
        float wavelength = 160.0f;
        float speed = 1.0f;
    };

    struct RippleParam {
        float amplitude = 12.0f;
        float wavelength = 140.0f;
        float speed = 1.0f;
        float radius = 400.0f;
        Vector2 center{ 0.0f, 0.0f };
    };

    struct BlackHoleParam {
        Vector2 center{ 0.0f, 0.0f };
        float spin = 3.0f;
        float radius = 300.0f;
        bool reverse = false; // false = Out (收缩消失), true = In (从中心长出)
    };

    struct NoiseParam {
        float amplitude = 10.0f;
        float frequency = 0.01f;
        float speed = 1.0f;
        float anisotropyX = 1.0f;
        float anisotropyY = 1.0f;
        unsigned int seed = 0;
    };

    struct ShearParam {
        float shearXPerY = 0.0f;
        float shearYPerX = 0.0f;
        float amount = 1.0f;
        float oscillateSpeed = 0.0f;
    };

    struct SliceParam {
        Vector2 origin{ 0.0f, 0.0f };
        Vector2 normal{ 1.0f, 0.0f };
        Vector2 separateDir{ 1.0f, 0.0f };
        float maxDistance = 80.0f;
        float edgeSoftness = 10.0f;
        float timeScale = 1.0f;
    };

    struct ShatterParam {
        float pieceSizeX = 64.0f;
        float pieceSizeY = 64.0f;
        float speedMin = 200.0f;
        float speedMax = 400.0f;
        float gravityY = 600.0f;
        float angularVelMin = -4.0f;
        float angularVelMax = 4.0f;
    };

    struct MeshEffectConfig {
        MeshEffectType type = MeshEffectType::Wave;
        int cols = 8;
        int rows = 8;
        float width = 512.0f;
        float height = 512.0f;
        WaveParam wave{};
        RippleParam ripple{};
        BlackHoleParam blackHole{};
        NoiseParam noise{};
        ShearParam shear{};
        SliceParam slice{};
        ShatterParam shatter{};
        bool loop = true;
        float duration = -1.0f; // < 0 means infinite
        bool autoDisable = true;
        Vector2 initialPosition{ 0.0f, 0.0f };
        Vector2 initialScale{ 1.0f, 1.0f };
        float initialRotation = 0.0f;
        RENDERER::CameraMode initialCameraMode = RENDERER::CameraMode::Inherit;
    };

    struct MeshEffect {
        RENDERER::DeformGrid grid{};
        std::vector<Vector2> basePos{};
        int dxHandle = -1;
        unsigned int color = 0xFFFFFFFF;
        MeshEffectConfig config{};
        float time = 0.0f;
        bool active = false;
        PlayState state = PlayState::Stopped;
        bool loop = true;
        float duration = -1.0f;
        bool autoDisable = true;
        Transform2D transform{};
        RENDERER::CameraMode cameraMode = RENDERER::CameraMode::Inherit;

        void Init(const MeshEffectConfig& cfg, int dxHandle_, unsigned int rgba = 0xFFFFFFFF);
        void Update(float dt);
        void Draw() const;

        void Play();
        void Stop();
        void Pause();
        void Resume();
        void ResetTime(float t = 0.0f);

        void SetPosition(const Vector2& pos) { transform.position = pos; }
        void AddPosition(const Vector2& delta) { transform.position.x += delta.x; transform.position.y += delta.y; }
        void SetScale(const Vector2& s) { transform.scale = s; }
        void SetRotation(float angleRad) { transform.rotation = angleRad; }
        void SetCameraMode(RENDERER::CameraMode mode) { cameraMode = mode; }

    private:
        struct ShatterPiece {
            Vector2 centerLocal{};
            Vector2 pos{};
            Vector2 vel{};
            float angle = 0.0f;
            float angularVel = 0.0f;
        };

        void InitGridRect(float width, float height, int cols, int rows);
        void ApplyWaveY(float elapsed);
        void ApplyRipple(float elapsed);
        bool ApplyBlackHole(float elapsed);
        void ApplyNoiseDistortion(float elapsed);
        void ApplyShear(float elapsed);
        void ApplySlice(float elapsed);
        void ApplyShatter(float dt);
        void BuildShatterPieces();
        void ResetShatterState();

        std::vector<ShatterPiece> shatterPieces_{};
        std::vector<int> vertexToPiece_{};
        std::vector<Vector2> vertexLocalInPiece_{};
        bool shatterBuilt_ = false;
    };

    class MeshEffectManager {
    public:
        int CreateFromConfig(const MeshEffectConfig& cfg, int dxHandle, unsigned int rgba = 0xFFFFFFFF);
        int SpawnPreset(const std::string& name, int dxHandle, unsigned int rgba = 0xFFFFFFFF);
        void Destroy(int id);

        MeshEffect* Get(int id);
        const MeshEffect* Get(int id) const;

        void UpdateAll(float dt);
        void DrawAll() const;

        void Play(int id);
        void Stop(int id);
        void Pause(int id);
        void Resume(int id);
        void ResetTime(int id, float t = 0.0f);

        void SetPosition(int id, const Vector2& pos);
        void AddPosition(int id, const Vector2& delta);
        void SetScale(int id, const Vector2& s);
        void SetRotation(int id, float angleRad);
        void SetCameraMode(int id, RENDERER::CameraMode mode);

        void RegisterPreset(const std::string& name, const MeshEffectConfig& cfg);

    private:
        int AllocateSlot();

        std::vector<MeshEffect> effects_{};
        std::vector<int> freeList_{};
        std::unordered_map<std::string, MeshEffectConfig> presets_{};
    };

} // namespace MESHFX
} // namespace HIKARI

