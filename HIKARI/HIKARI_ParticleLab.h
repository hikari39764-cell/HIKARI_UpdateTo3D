#pragma once
#include "HIKARI_Particle.h"

namespace HIKARI {
    namespace LAB {
        class ParticleLab {
        public:
            void Init();

            void Update(float dt);

            void Draw();

        private:
            // パーティクルシステム本体
            PARTICLE::ParticleSystem particleSystem_{};

            // 現在テスト中のエミッター
            PARTICLE::Emitter* currentEmitter_ = nullptr;

            // 編集中の設定と戦略
            PARTICLE::EmitterConfig currentConfig_{};
            PARTICLE::DrawFunc      currentDraw_{};
            PARTICLE::SpawnFunc     currentSpawn_{};

            // UI 用
            int spawnIndex_ = 0;
            int drawIndex_ = 0;

            bool loopPlay_ = false;

            // 雷用のパラメータ
            int   lightningSegments_ = 8;
            float lightningAmplitude_ = 40.0f;


            // 作り直す
            void RebuildEmitter();

            void UpdateCurrentSpawnFromIndex();

            void UpdateCurrentDrawFromIndex();

            void DrawSpawnSelectorGui();
            void DrawDrawSelectorGui();
            void DrawConfigGui();
            void DrawControlGui();

            // JSON出力
            char exportPath_[256] = "HIKARI_Particle_Export.json";
            void ExportCurrentConfigToFile();
        };

    } // namespace LAB
} // namespace HIKARI
