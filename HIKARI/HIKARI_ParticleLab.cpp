#include "HIKARI_ParticleLab.h"
#include "HIKARI_Renderer.h"
#if defined(HIKARI_ENABLE_IMGUI)
#include <imgui.h>
#endif
#include <json.hpp>
#include <fstream>

using namespace HIKARI;
using namespace HIKARI::PARTICLE;
using namespace HIKARI::PARTICLE::PRESET;
using nlohmann::json;

namespace {

    // ---- 逕滓・謌ｦ逡･荳隕ｧ ----
    struct SpawnPreset {
        const char* name;
        SpawnFunc(*makeFunc)();
    };

    SpawnPreset kSpawnPresets[] = {
        { "RadialBurst",     &MakeRadialBurstSpawn     },
        { "ConeStream",      &MakeConeStreamSpawn      },
        { "Trail",           &MakeTrailSpawn           },
        { "Snowfall",        &MakeSnowfallSpawn        },
        { "ShockwaveRing",   &MakeShockwaveRingSpawn   },
        { "AreaFog",         &MakeAreaFogSpawn         },
        { "Fountain",        &MakeFountainSpawn        },
        { "Orbit",           &MakeOrbitSpawn           },
        { "Homing",          &MakeHomingSpawn          },
        { "LightningStrike", &MakeLightningStrikeSpawn },
        { "SpeedLineScreen", &MakeScreenSpeedLineSpawn },
    };
    constexpr int kSpawnPresetCount = sizeof(kSpawnPresets) / sizeof(kSpawnPresets[0]);

    // ---- 謠冗判謌ｦ逡･蜷・----
    const char* kDrawNames[] = {
        "Circle",
        "Box Fill",
        "Box Outline",
        "Triangle Fill",
        "Triangle Outline",
        "Ring",
        "VelocityLine",
        "Fog Disc",
        "CrossStar",
        "CrossStar + Diagonal",
        "LightningBolt",
        "SpeedLine"
    };
    constexpr int kDrawPresetCount = sizeof(kDrawNames) / sizeof(kDrawNames[0]);

    // ---- RGBA縺九ｉ(ImGui) 螟画鋤 ----
    inline void RGBAu32ToFloat4(unsigned int rgba, float out[4]) {
        float r = float((rgba >> 24) & 0xFF) / 255.0f;
        float g = float((rgba >> 16) & 0xFF) / 255.0f;
        float b = float((rgba >> 8) & 0xFF) / 255.0f;
        float a = float((rgba) & 0xFF) / 255.0f;
        out[0] = r; out[1] = g; out[2] = b; out[3] = a;
    }

    inline unsigned int Float4ToRGBAu32(const float in[4]) {
        unsigned int r = unsigned int(in[0] * 255.0f + 0.5f) & 0xFF;
        unsigned int g = unsigned int(in[1] * 255.0f + 0.5f) & 0xFF;
        unsigned int b = unsigned int(in[2] * 255.0f + 0.5f) & 0xFF;
        unsigned int a = unsigned int(in[3] * 255.0f + 0.5f) & 0xFF;
        return (r << 24) | (g << 16) | (b << 8) | a;
    }

} // anonymous namespace


// ==================================================

void LAB::ParticleLab::Init() {
    // 繝・ヵ繧ｩ繝ｫ繝・
    currentConfig_ = MakeRadialBurstConfig(
        Vector2{ 640.0f, 360.0f },
        256,
        0.5f, 1.2f,
        150.0f, 450.0f,
        0xFFFFFFFF,
        0xFFAA00FF
    );

    spawnIndex_ = 0;
    drawIndex_ = 0;

    lightningSegments_ = 8;
    lightningAmplitude_ = 40.0f;

    UpdateCurrentSpawnFromIndex();
    UpdateCurrentDrawFromIndex();

    loopPlay_ = false;

    RebuildEmitter();
}

void LAB::ParticleLab::RebuildEmitter() {
    if (currentEmitter_) {
        particleSystem_.DestroyEmitter(currentEmitter_);
        currentEmitter_ = nullptr;
    }

    currentEmitter_ = particleSystem_.CreateEmitter(
        currentConfig_,
        currentDraw_,
        currentSpawn_
    );

    if (currentEmitter_) {
        currentEmitter_->SetActive(loopPlay_);
    }
}

void LAB::ParticleLab::UpdateCurrentSpawnFromIndex() {
    if (spawnIndex_ < 0) spawnIndex_ = 0;
    if (spawnIndex_ >= kSpawnPresetCount) spawnIndex_ = kSpawnPresetCount - 1;

    currentSpawn_ = kSpawnPresets[spawnIndex_].makeFunc();
}

void LAB::ParticleLab::UpdateCurrentDrawFromIndex() {
    using HIKARI::RENDERER::CameraMode;

    if (drawIndex_ < 0) drawIndex_ = 0;
    if (drawIndex_ >= kDrawPresetCount) drawIndex_ = kDrawPresetCount - 1;

    switch (drawIndex_) {
    case 0: // Circle
        currentDraw_ = MakeCircleDrawer(CameraMode::Inherit);
        break;
    case 1: // Box Fill
        currentDraw_ = MakeBoxDrawer(false, CameraMode::Inherit);
        break;
    case 2: // Box Outline
        currentDraw_ = MakeBoxDrawer(true, CameraMode::Inherit);
        break;
    case 3: // Triangle Fill
        currentDraw_ = MakeTriangleDrawer(false, CameraMode::Inherit);
        break;
    case 4: // Triangle Outline
        currentDraw_ = MakeTriangleDrawer(true, CameraMode::Inherit);
        break;
    case 5: // Ring
        currentDraw_ = MakeRingDrawer(CameraMode::Inherit);
        break;
    case 6: // VelocityLine
        currentDraw_ = MakeVelocityLineDrawer(CameraMode::Inherit);
        break;
    case 7: // Fog Disc
        currentDraw_ = MakeFogDiscDrawer(CameraMode::Inherit);
        break;
    case 8: // CrossStar
        currentDraw_ = MakeCrossStarDrawer(false, CameraMode::Inherit);
        break;
    case 9: // CrossStar + Diagonal
        currentDraw_ = MakeCrossStarDrawer(true, CameraMode::Inherit);
        break;
    case 10: // LightningBolt
        currentDraw_ = MakeLightningBoltDrawer(
            lightningSegments_,
            lightningAmplitude_,
            CameraMode::Inherit
        );
        break;
    case 11: // SpeedLine    
        currentDraw_ = MakeSpeedLineDrawer(CameraMode::Inherit);
        break;
    default:
        break;
    }
}

void LAB::ParticleLab::Update(float dt) {
    particleSystem_.Update(dt);
}

void LAB::ParticleLab::Draw() {
    particleSystem_.Draw();
#if defined(HIKARI_ENABLE_IMGUI)
    ImGui::Begin("HIKARI_Particle Lab");

    DrawSpawnSelectorGui();
    DrawDrawSelectorGui();
    DrawConfigGui();
    DrawControlGui();

    ImGui::End();
#endif
}


// ========================= ImGui =========================

void LAB::ParticleLab::DrawSpawnSelectorGui() {
#if defined(HIKARI_ENABLE_IMGUI)
    const char* currentName = kSpawnPresets[spawnIndex_].name;
    if (ImGui::BeginCombo("Spawn Strategy", currentName)) {
        for (int i = 0; i < kSpawnPresetCount; ++i) {
            bool selected = (i == spawnIndex_);
            if (ImGui::Selectable(kSpawnPresets[i].name, selected)) {
                spawnIndex_ = i;
                UpdateCurrentSpawnFromIndex();
                RebuildEmitter(); // 菴懊ｊ逶ｴ縺・
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
#endif
}

void LAB::ParticleLab::DrawDrawSelectorGui() {
#if defined(HIKARI_ENABLE_IMGUI)
    const char* currentName = kDrawNames[drawIndex_];
    if (ImGui::BeginCombo("Draw Strategy", currentName)) {
        for (int i = 0; i < kDrawPresetCount; ++i) {
            bool selected = (i == drawIndex_);
            if (ImGui::Selectable(kDrawNames[i], selected)) {
                drawIndex_ = i;
                UpdateCurrentDrawFromIndex();
                RebuildEmitter();
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    // 髮ｷ蟆ら畑繝代Λ繝｡繝ｼ繧ｿ
    ImGui::SeparatorText("LightningBolt Params");
    ImGui::DragInt("Segments", &lightningSegments_, 1, 2, 64);
    ImGui::DragFloat("Amplitude", &lightningAmplitude_, 1.0f, 1.0f, 200.0f);

    if (ImGui::Button("Apply Lightning Params")) {
        if (drawIndex_ == 10) {
            UpdateCurrentDrawFromIndex();
            RebuildEmitter();
        }
    }
#endif
}

void LAB::ParticleLab::DrawConfigGui() {
#if defined(HIKARI_ENABLE_IMGUI)
    ImGui::SeparatorText("Basic Config");
    ImGui::DragInt("Max Particles", &currentConfig_.maxParticles, 1, 1, 4096);

    ImGui::SeparatorText("Life & Size");
    ImGui::DragFloat("Life Min", &currentConfig_.lifeMin, 0.01f, 0.0f, 10.0f);
    ImGui::DragFloat("Life Max", &currentConfig_.lifeMax, 0.01f, 0.0f, 10.0f);
    ImGui::DragFloat("Size Min", &currentConfig_.sizeMin, 0.1f, 0.0f, 512.0f);
    ImGui::DragFloat("Size Max", &currentConfig_.sizeMax, 0.1f, 0.0f, 512.0f);

    ImGui::SeparatorText("Speed");
    ImGui::DragFloat("Speed Min", &currentConfig_.speedMin, 1.0f, 0.0f, 2000.0f);
    ImGui::DragFloat("Speed Max", &currentConfig_.speedMax, 1.0f, 0.0f, 2000.0f);

    ImGui::SeparatorText("Emit");
    ImGui::DragFloat("Emit Rate", &currentConfig_.emitRate, 1.0f, 0.0f, 500.0f);
    ImGui::DragInt("Burst Count", &currentConfig_.burstCount, 1, 0, 4096);

    ImGui::SeparatorText("Velocity Range");
    ImGui::DragFloat2("Vel Min", &currentConfig_.velMin.x, 1.0f, -2000.0f, 2000.0f);
    ImGui::DragFloat2("Vel Max", &currentConfig_.velMax.x, 1.0f, -2000.0f, 2000.0f);

    ImGui::SeparatorText("Origin / Area");
    ImGui::DragFloat2("Origin Pos", &currentConfig_.originPosition.x, 1.0f, -5000.0f, 5000.0f);
    ImGui::DragFloat2("Area Half Size", &currentConfig_.areaHalfSize.x, 1.0f, 0.0f, 5000.0f);

    ImGui::SeparatorText("Direction / Spread");
    ImGui::DragFloat2("Base Direction", &currentConfig_.baseDirection.x, 0.01f, -1.0f, 1.0f);
    ImGui::DragFloat("Spread Deg", &currentConfig_.spreadDeg, 1.0f, 0.0f, 180.0f);

    ImGui::SeparatorText("Ring / Orbit");
    ImGui::DragFloat("Ring Radius", &currentConfig_.ringRadius, 1.0f, 0.0f, 5000.0f);
    ImGui::DragInt("Ring Segments", &currentConfig_.ringSegments, 1, 0, 512);
    ImGui::DragFloat("Attract Strength", &currentConfig_.attractStrength, 0.1f, 0.0f, 2000.0f);

    ImGui::SeparatorText("Physics");
    ImGui::DragFloat2("Gravity", &currentConfig_.physics.gravity.x, 1.0f, -2000.0f, 2000.0f);
    ImGui::Checkbox("Use Damping", &currentConfig_.physics.useDamping);
    ImGui::DragFloat("Damping", &currentConfig_.physics.damping, 0.001f, 0.0f, 5.0f);

    ImGui::SeparatorText("Color");
    float startCol[4], endCol[4];
    RGBAu32ToFloat4(currentConfig_.startColor, startCol);
    RGBAu32ToFloat4(currentConfig_.endColor, endCol);
    if (ImGui::ColorEdit4("Start Color", startCol)) {
        currentConfig_.startColor = Float4ToRGBAu32(startCol);
    }
    if (ImGui::ColorEdit4("End Color", endCol)) {
        currentConfig_.endColor = Float4ToRGBAu32(endCol);
    }
#endif
}

void LAB::ParticleLab::DrawControlGui() {
#if defined(HIKARI_ENABLE_IMGUI)
    ImGui::SeparatorText("Control");

    if (ImGui::Checkbox("Loop Play", &loopPlay_)) {
        if (currentEmitter_) {
            currentEmitter_->SetActive(loopPlay_);
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Burst")) {
        if (currentEmitter_) {
            int count = currentConfig_.burstCount > 0 ? currentConfig_.burstCount : 1;
            currentEmitter_->EmitBurst(count);
        }
    }

    if (ImGui::Button("Apply Config & Rebuild")) {
        RebuildEmitter();
    }

    ImGui::SameLine();
    if (ImGui::Button("Reset RadialBurst")) {
        currentConfig_ = MakeRadialBurstConfig(
            Vector2{ 640.0f, 360.0f },
            256,
            0.5f, 1.2f,
            150.0f, 450.0f,
            0xFFFFFFFF,
            0xFFAA00FF
        );
        RebuildEmitter();
    }

    ImGui::SeparatorText("Export");

    ImGui::InputText("Output Path", exportPath_, sizeof(exportPath_));

    if (ImGui::Button("Export JSON")) {
        ExportCurrentConfigToFile();
    }
#endif
}


// ========================= JSON 蜃ｺ蜉・=========================

void LAB::ParticleLab::ExportCurrentConfigToFile() {
    json j;

    // ---- 蝓ｺ譛ｬ繝代Λ繝｡繝ｼ繧ｿ ----
    j["maxParticles"] = currentConfig_.maxParticles;

    j["emitRate"] = currentConfig_.emitRate;
    j["burstCount"] = currentConfig_.burstCount;

    j["lifeMin"] = currentConfig_.lifeMin;
    j["lifeMax"] = currentConfig_.lifeMax;

    j["sizeMin"] = currentConfig_.sizeMin;
    j["sizeMax"] = currentConfig_.sizeMax;

    j["speedMin"] = currentConfig_.speedMin;
    j["speedMax"] = currentConfig_.speedMax;

    // ---- 繝吶け繝医Ν邉ｻ ----
    j["velMin"] = { currentConfig_.velMin.x, currentConfig_.velMin.y };
    j["velMax"] = { currentConfig_.velMax.x, currentConfig_.velMax.y };

    j["originPosition"] = {
        currentConfig_.originPosition.x,
        currentConfig_.originPosition.y
    };

    j["areaHalfSize"] = {
        currentConfig_.areaHalfSize.x,
        currentConfig_.areaHalfSize.y
    };

    j["baseDirection"] = {
        currentConfig_.baseDirection.x,
        currentConfig_.baseDirection.y
    };

    j["spreadDeg"] = currentConfig_.spreadDeg;

    j["ringRadius"] = currentConfig_.ringRadius;
    j["ringSegments"] = currentConfig_.ringSegments;

    j["attractStrength"] = currentConfig_.attractStrength;

    // ---- 迚ｩ逅・----
    j["physics"] = {
        { "gravity", {
            currentConfig_.physics.gravity.x,
            currentConfig_.physics.gravity.y
        }},
        { "damping",    currentConfig_.physics.damping },
        { "useDamping", currentConfig_.physics.useDamping }
    };

    // ---- 濶ｲ----
    j["startColor"] = currentConfig_.startColor;
    j["endColor"] = currentConfig_.endColor;

    // ---- 迴ｾ蝨ｨ縺ｮ Spawn / Draw 蜷・----
    const char* spawnName = (spawnIndex_ >= 0 && spawnIndex_ < kSpawnPresetCount)
        ? kSpawnPresets[spawnIndex_].name
        : "UnknownSpawn";
    const char* drawName = (drawIndex_ >= 0 && drawIndex_ < kDrawPresetCount)
        ? kDrawNames[drawIndex_]
        : "UnknownDraw";

    j["spawnStrategy"] = spawnName;
    j["drawStrategy"] = drawName;

    j["lightningSegments"] = lightningSegments_;
    j["lightningAmplitude"] = lightningAmplitude_;

    // ---- 繝輔ぃ繧､繝ｫ蜃ｺ蜉・----
    std::ofstream ofs(exportPath_);
    if (ofs.is_open()) {
        ofs << j.dump(4);
        ofs.close();
    }
}
