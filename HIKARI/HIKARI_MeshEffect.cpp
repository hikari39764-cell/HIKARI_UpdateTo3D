#include "HIKARI_MeshEffect.h"
#include <algorithm>
#include <random>
// –¢’è‹`
#include <Windows.h>
#undef min
#undef max
namespace {
    constexpr float kTwoPi = 6.28318530717958647692f;

    inline float Clamp01(float v) { return std::max(0.0f, std::min(1.0f, v)); }

    inline Vector2 NormalizeSafe(const Vector2& v, const Vector2& fallback = { 1.0f, 0.0f }) {
        float lenSq = v.x * v.x + v.y * v.y;
        if (lenSq <= 1e-6f) {
            return fallback;
        }
        float invLen = 1.0f / std::sqrt(lenSq);
        return { v.x * invLen, v.y * invLen };
    }

    inline Vector2 Rotate(const Vector2& v, float angle) {
        float c = std::cosf(angle);
        float s = std::sinf(angle);
        return { v.x * c - v.y * s, v.x * s + v.y * c };
    }

    inline Vector2 Lerp(const Vector2& a, const Vector2& b, float t) {
        return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t };
    }

    inline float Hash(float n) {
        float s = std::sinf(n) * 43758.5453f;
        return s - std::floor(s);
    }

    inline float Noise2D(float x, float y) {
        float n = x * 12.9898f + y * 78.233f;
        float h = Hash(n);
        return h * 2.0f - 1.0f;
    }
}

namespace HIKARI {
namespace MESHFX {

    void MeshEffect::Init(const MeshEffectConfig& cfg, int dxHandle_, unsigned int rgba) {
        config = cfg;
        dxHandle = dxHandle_;
        color = rgba;
        loop = cfg.loop;
        duration = cfg.duration;
        autoDisable = cfg.autoDisable;
        time = 0.0f;
        state = PlayState::Stopped;
        active = false;
        transform.position = cfg.initialPosition;
        transform.scale = cfg.initialScale;
        transform.rotation = cfg.initialRotation;
        cameraMode = cfg.initialCameraMode;
        config.slice.normal = NormalizeSafe(config.slice.normal, { 1.0f, 0.0f });
        config.slice.separateDir = NormalizeSafe(config.slice.separateDir, config.slice.normal);

        InitGridRect(cfg.width, cfg.height, cfg.cols, cfg.rows);
        ResetShatterState();
    }

    void MeshEffect::Update(float dt) {
        if (!active || state != PlayState::Playing) {
            return;
        }

        time += dt;

        // reset to base positions
        grid.positions = basePos;

        bool finished = false;
        switch (config.type) {
        case MeshEffectType::Wave:
            ApplyWaveY(time);
            break;
        case MeshEffectType::Ripple:
            ApplyRipple(time);
            break;
        case MeshEffectType::BlackHole:
            finished = ApplyBlackHole(time);
            break;
        case MeshEffectType::NoiseDistortion:
            ApplyNoiseDistortion(time);
            break;
        case MeshEffectType::Shear:
            ApplyShear(time);
            break;
        case MeshEffectType::Slice:
            ApplySlice(time);
            break;
        case MeshEffectType::Shatter:
            ApplyShatter(dt);
            break;
        default:
            break;
        }

        if (finished && config.type == MeshEffectType::BlackHole) {
            state = PlayState::Stopped;
            active = false;
            return;
        }

        if (duration >= 0.0f && time >= duration) {
            if (loop) {
                time = 0.0f;
                ResetShatterState();
                grid.positions = basePos;
            } else {
                state = PlayState::Stopped;
                if (autoDisable) {
                    active = false;
                }
            }
        }
    }

    void MeshEffect::Draw() const {
        if (!active || dxHandle < 0 || grid.cols <= 0 || grid.rows <= 0) {
            return;
        }
        HIKARI::RENDERER::DeformGrid worldGrid = grid;
        const float c = std::cosf(transform.rotation);
        const float s = std::sinf(transform.rotation);

        for (size_t i = 0; i < worldGrid.positions.size(); ++i) {
            Vector2 pLocal = worldGrid.positions[i];
            pLocal.x *= transform.scale.x;
            pLocal.y *= transform.scale.y;
            Vector2 rotated{};
            rotated.x = pLocal.x * c - pLocal.y * s;
            rotated.y = pLocal.x * s + pLocal.y * c;
            rotated.x += transform.position.x;
            rotated.y += transform.position.y;
            worldGrid.positions[i] = rotated;
        }

        HIKARI::RENDERER::DrawDeformGridHandle(dxHandle, worldGrid, cameraMode, color);
    }

    void MeshEffect::Play() {
        if (state == PlayState::Stopped) {
            time = 0.0f;
            grid.positions = basePos;
            ResetShatterState();
        }
        active = true;
        state = PlayState::Playing;
    }

    void MeshEffect::Stop() {
        state = PlayState::Stopped;
        time = 0.0f;
        grid.positions = basePos;
        ResetShatterState();
        if (autoDisable) {
            active = false;
        }
    }

    void MeshEffect::Pause() {
        if (state == PlayState::Playing) {
            state = PlayState::Paused;
        }
    }

    void MeshEffect::Resume() {
        if (state == PlayState::Paused) {
            state = PlayState::Playing;
        }
    }

    void MeshEffect::ResetTime(float t) {
        time = t;
    }

    void MeshEffect::InitGridRect(float width, float height, int cols, int rows) {
        grid.cols = std::max(2, cols);
        grid.rows = std::max(2, rows);
        int vertexCount = grid.cols * grid.rows;
        grid.positions.resize(vertexCount);
        grid.uvs.resize(vertexCount);
        basePos.resize(vertexCount);

        float dx = (grid.cols > 1) ? width / static_cast<float>(grid.cols - 1) : width;
        float dy = (grid.rows > 1) ? height / static_cast<float>(grid.rows - 1) : height;
        float startX = -width * 0.5f;
        float startY = -height * 0.5f;

        for (int y = 0; y < grid.rows; ++y) {
            for (int x = 0; x < grid.cols; ++x) {
                int idx = y * grid.cols + x;
                float px = startX + dx * static_cast<float>(x);
                float py = startY + dy * static_cast<float>(y);
                Vector2 p{ px, py };
                grid.positions[idx] = p;
                basePos[idx] = p;

                float u = (grid.cols > 1) ? static_cast<float>(x) / static_cast<float>(grid.cols - 1) : 0.0f;
                float v = (grid.rows > 1) ? static_cast<float>(y) / static_cast<float>(grid.rows - 1) : 0.0f;
                grid.uvs[idx] = { u, v };
            }
        }
    }

    void MeshEffect::ApplyWaveY(float elapsed) {
        if (config.wave.wavelength == 0.0f) {
            return;
        }
        float k = kTwoPi / config.wave.wavelength;
        for (size_t i = 0; i < grid.positions.size(); ++i) {
            float phase = basePos[i].x * k + elapsed * config.wave.speed * kTwoPi;
            float offset = std::sinf(phase) * config.wave.amplitude;
            grid.positions[i].y = basePos[i].y + offset;
        }
    }

    void MeshEffect::ApplyRipple(float elapsed) {
        if (config.ripple.wavelength == 0.0f) {
            return;
        }
        float k = kTwoPi / config.ripple.wavelength;
        for (size_t i = 0; i < grid.positions.size(); ++i) {
            float dx = basePos[i].x - config.ripple.center.x;
            float dy = basePos[i].y - config.ripple.center.y;
            float distSq = dx * dx + dy * dy;
            float dist = std::sqrt(distSq);
            if (dist <= 0.0001f) {
                continue;
            }

            float attenuation = 1.0f;
            if (config.ripple.radius > 0.0f) {
                attenuation = 1.0f - Clamp01(dist / config.ripple.radius);
            }
            float phase = dist * k + elapsed * config.ripple.speed * kTwoPi;
            float disp = std::sinf(phase) * config.ripple.amplitude * attenuation;
            float invDist = 1.0f / dist;
            grid.positions[i].x = basePos[i].x + dx * invDist * disp;
            grid.positions[i].y = basePos[i].y + dy * invDist * disp;
        }
    }

    bool MeshEffect::ApplyBlackHole(float elapsed) {
        const auto& p = config.blackHole;

        float s = 0.0f;
        if (duration > 0.0f) {
            s = Clamp01(elapsed / duration);
        } else {
            s = Clamp01(elapsed);
        }

        Vector2 center = p.center;
        float swirlStrength = 0.0f;
        if (!p.reverse) {

            swirlStrength = p.spin * s;
        } else {

            swirlStrength = p.spin * (1.0f - s);
        }

        for (size_t i = 0; i < grid.positions.size(); ++i) {
            const Vector2& start = p.reverse ? center : basePos[i];
            const Vector2& end = p.reverse ? basePos[i] : center;

            Vector2 pos = Lerp(start, end, s);

            if (p.spin != 0.0f) {
                float dist = std::sqrt((basePos[i].x - center.x) * (basePos[i].x - center.x) + (basePos[i].y - center.y) * (basePos[i].y - center.y));
                float falloff = 1.0f;
                if (p.radius > 0.0f) {
                    falloff = 1.0f - Clamp01(dist / p.radius);
                }
                float angle = swirlStrength * falloff;
                Vector2 rel{ pos.x - center.x, pos.y - center.y };
                rel = Rotate(rel, angle);
                pos.x = rel.x + center.x;
                pos.y = rel.y + center.y;
            }

            grid.positions[i] = pos;
        }

        return s >= 1.0f;
    }

    void MeshEffect::ApplyNoiseDistortion(float elapsed) {
        const auto& p = config.noise;
        for (size_t i = 0; i < grid.positions.size(); ++i) {
            float sx = basePos[i].x * p.frequency + elapsed * p.speed + static_cast<float>(p.seed) * 13.0f;
            float sy = basePos[i].y * p.frequency + elapsed * p.speed + static_cast<float>(p.seed) * 31.0f;
            float nx = Noise2D(sx, sy);
            float ny = Noise2D(sx + 100.0f, sy + 200.0f);
            grid.positions[i].x = basePos[i].x + nx * p.amplitude * p.anisotropyX;
            grid.positions[i].y = basePos[i].y + ny * p.amplitude * p.anisotropyY;
        }
    }

    void MeshEffect::ApplyShear(float elapsed) {
        const auto& p = config.shear;
        float a = p.amount;
        if (p.oscillateSpeed > 0.0f) {
            a = p.amount * std::sinf(elapsed * p.oscillateSpeed * kTwoPi);
        }

        for (size_t i = 0; i < grid.positions.size(); ++i) {
            const Vector2& src = basePos[i];
            grid.positions[i].x = src.x + p.shearXPerY * src.y * a;
            grid.positions[i].y = src.y + p.shearYPerX * src.x * a;
        }
    }

    void MeshEffect::ApplySlice(float elapsed) {
        const auto& p = config.slice;
        float progress = 0.0f;
        if (duration > 0.0f) {
            progress = Clamp01(elapsed / duration);
        } else {
            progress = Clamp01(elapsed * p.timeScale);
        }

        for (size_t i = 0; i < grid.positions.size(); ++i) {
            Vector2 v{ basePos[i].x - p.origin.x, basePos[i].y - p.origin.y };
            float side = v.x * p.normal.x + v.y * p.normal.y;
            float distToLine = std::fabs(side);

            float w = 1.0f;
            if (p.edgeSoftness > 0.0f) {
                w = Clamp01(distToLine / p.edgeSoftness);
            }

            float d = p.maxDistance * progress * w;
            Vector2 dir = p.separateDir;
            if (side < 0.0f) {
                dir.x = -dir.x;
                dir.y = -dir.y;
            }
            grid.positions[i].x = basePos[i].x + dir.x * d;
            grid.positions[i].y = basePos[i].y + dir.y * d;
        }
    }

    void MeshEffect::ApplyShatter(float dt) {
        if (!shatterBuilt_) {
            BuildShatterPieces();
        }

        if (shatterPieces_.empty()) {
            return;
        }

        for (auto& piece : shatterPieces_) {
            piece.vel.y += config.shatter.gravityY * dt;
            piece.pos.x += piece.vel.x * dt;
            piece.pos.y += piece.vel.y * dt;
            piece.angle += piece.angularVel * dt;
        }

        for (size_t i = 0; i < grid.positions.size(); ++i) {
            int pid = (i < vertexToPiece_.size()) ? vertexToPiece_[i] : -1;
            if (pid < 0 || pid >= static_cast<int>(shatterPieces_.size())) {
                grid.positions[i] = basePos[i];
                continue;
            }
            const auto& piece = shatterPieces_[pid];
            Vector2 rotated = Rotate(vertexLocalInPiece_[i], piece.angle);
            grid.positions[i].x = piece.pos.x + rotated.x;
            grid.positions[i].y = piece.pos.y + rotated.y;
        }
    }

    void MeshEffect::ResetShatterState() {
        shatterPieces_.clear();
        vertexToPiece_.clear();
        vertexLocalInPiece_.clear();
        shatterBuilt_ = false;
    }

    void MeshEffect::BuildShatterPieces() {
        ResetShatterState();
        if (basePos.empty()) {
            return;
        }

        float minX = basePos[0].x, maxX = basePos[0].x;
        float minY = basePos[0].y, maxY = basePos[0].y;
        for (const auto& p : basePos) {
            minX = std::min(minX, p.x);
            maxX = std::max(maxX, p.x);
            minY = std::min(minY, p.y);
            maxY = std::max(maxY, p.y);
        }

        float pieceSizeX = std::max(1.0f, config.shatter.pieceSizeX);
        float pieceSizeY = std::max(1.0f, config.shatter.pieceSizeY);
        int pieceCountX = std::max(1, static_cast<int>(std::ceil((maxX - minX) / pieceSizeX)));
        int pieceCountY = std::max(1, static_cast<int>(std::ceil((maxY - minY) / pieceSizeY)));

        std::vector<std::vector<int>> pieceVertices(pieceCountX * pieceCountY);
        vertexToPiece_.assign(basePos.size(), -1);
        vertexLocalInPiece_.assign(basePos.size(), {});

        for (size_t i = 0; i < basePos.size(); ++i) {
            int px = std::min(pieceCountX - 1, std::max(0, static_cast<int>((basePos[i].x - minX) / pieceSizeX)));
            int py = std::min(pieceCountY - 1, std::max(0, static_cast<int>((basePos[i].y - minY) / pieceSizeY)));
            int idx = py * pieceCountX + px;
            pieceVertices[idx].push_back(static_cast<int>(i));
            vertexToPiece_[i] = static_cast<int>(idx);
        }

        float speedMin = std::min(config.shatter.speedMin, config.shatter.speedMax);
        float speedMax = std::max(config.shatter.speedMin, config.shatter.speedMax);
        float angMin = std::min(config.shatter.angularVelMin, config.shatter.angularVelMax);
        float angMax = std::max(config.shatter.angularVelMin, config.shatter.angularVelMax);
        std::mt19937 rng(static_cast<uint32_t>(config.cols * 73856093u ^ config.rows * 19349663u ^ static_cast<int>(pieceSizeX + pieceSizeY)));
        std::uniform_real_distribution<float> speedDist(speedMin, speedMax);
        std::uniform_real_distribution<float> angDist(angMin, angMax);

        for (size_t i = 0; i < pieceVertices.size(); ++i) {
            const auto& verts = pieceVertices[i];
            if (verts.empty()) {
                continue;
            }

            Vector2 center{ 0.0f, 0.0f };
            for (int vi : verts) {
                center.x += basePos[vi].x;
                center.y += basePos[vi].y;
            }
            float invCount = 1.0f / static_cast<float>(verts.size());
            center.x *= invCount;
            center.y *= invCount;

            Vector2 dir = NormalizeSafe(center, { 1.0f, 0.0f });
            float speed = speedDist(rng);

            ShatterPiece piece{};
            piece.centerLocal = center;
            piece.pos = center;
            piece.vel = { dir.x * speed, dir.y * speed };
            piece.angularVel = angDist(rng);
            shatterPieces_.push_back(piece);

            int storedIndex = static_cast<int>(shatterPieces_.size() - 1);
            for (int vi : verts) {
                vertexToPiece_[vi] = storedIndex;
                vertexLocalInPiece_[vi].x = basePos[vi].x - center.x;
                vertexLocalInPiece_[vi].y = basePos[vi].y - center.y;
            }
        }

        shatterBuilt_ = true;
    }

    int MeshEffectManager::CreateFromConfig(const MeshEffectConfig& cfg, int dxHandle, unsigned int rgba) {
        int id = AllocateSlot();
        effects_[id].Init(cfg, dxHandle, rgba);
        return id;
    }

    int MeshEffectManager::SpawnPreset(const std::string& name, int dxHandle, unsigned int rgba) {
        auto it = presets_.find(name);
        if (it == presets_.end()) {
            return -1;
        }
        return CreateFromConfig(it->second, dxHandle, rgba);
    }

    void MeshEffectManager::Destroy(int id) {
        if (id < 0 || id >= static_cast<int>(effects_.size())) {
            return;
        }
        effects_[id] = MeshEffect{};
        freeList_.push_back(id);
    }

    MeshEffect* MeshEffectManager::Get(int id) {
        if (id < 0 || id >= static_cast<int>(effects_.size())) {
            return nullptr;
        }
        return &effects_[id];
    }

    const MeshEffect* MeshEffectManager::Get(int id) const {
        if (id < 0 || id >= static_cast<int>(effects_.size())) {
            return nullptr;
        }
        return &effects_[id];
    }

    void MeshEffectManager::UpdateAll(float dt) {
        for (auto& fx : effects_) {
            fx.Update(dt);
        }
    }

    void MeshEffectManager::DrawAll() const {
        for (const auto& fx : effects_) {
            fx.Draw();
        }
    }

    void MeshEffectManager::Play(int id) {
        if (auto* fx = Get(id)) {
            fx->Play();
        }
    }

    void MeshEffectManager::Stop(int id) {
        if (auto* fx = Get(id)) {
            fx->Stop();
        }
    }

    void MeshEffectManager::Pause(int id) {
        if (auto* fx = Get(id)) {
            fx->Pause();
        }
    }

    void MeshEffectManager::Resume(int id) {
        if (auto* fx = Get(id)) {
            fx->Resume();
        }
    }

    void MeshEffectManager::ResetTime(int id, float t) {
        if (auto* fx = Get(id)) {
            fx->ResetTime(t);
        }
    }

    void MeshEffectManager::SetPosition(int id, const Vector2& pos) {
        if (auto* fx = Get(id)) {
            fx->SetPosition(pos);
        }
    }

    void MeshEffectManager::AddPosition(int id, const Vector2& delta) {
        if (auto* fx = Get(id)) {
            fx->AddPosition(delta);
        }
    }

    void MeshEffectManager::SetScale(int id, const Vector2& s) {
        if (auto* fx = Get(id)) {
            fx->SetScale(s);
        }
    }

    void MeshEffectManager::SetRotation(int id, float angleRad) {
        if (auto* fx = Get(id)) {
            fx->SetRotation(angleRad);
        }
    }

    void MeshEffectManager::SetCameraMode(int id, RENDERER::CameraMode mode) {
        if (auto* fx = Get(id)) {
            fx->SetCameraMode(mode);
        }
    }

    void MeshEffectManager::RegisterPreset(const std::string& name, const MeshEffectConfig& cfg) {
        presets_[name] = cfg;
    }

    int MeshEffectManager::AllocateSlot() {
        if (!freeList_.empty()) {
            int id = freeList_.back();
            freeList_.pop_back();
            return id;
        }
        effects_.emplace_back();
        return static_cast<int>(effects_.size() - 1);
    }

} // namespace MESHFX
} // namespace HIKARI

