#pragma once
#include <vector>
#include <functional>
#include <string>
#include <unordered_map>
#include "HIKARI_Transform2D.h"
#include "HIKARI_Renderer.h"

namespace HIKARI {
    namespace PARTICLE {

        // 物理パラメータ：重力 / 減衰
        struct PhysicsParam {
            Vector2 gravity{ 0.0f, 0.0f };
            float damping{ 0.0f };
            bool useDamping{ false };
        };

        // パーティクル本体
        struct Particle {
            HIKARI::Transform2D transform{};
            Vector2 velocity{ 0.0f, 0.0f };
            Vector2 acceleration{ 0.0f, 0.0f };

            float lifetime{ 1.0f };
            float age{ 0.0f };

            float size{ 8.0f };
            unsigned int color{ 0xFFFFFFFF }; // RGBA
            // 角速度（ラジアン / 秒）
            float angularVelocity{ 0.0f };

            float t{ 0.0f };

            // 汎用カスタムパラメータ（エフェクトごとに自由に使う）
            float user0{ 0.0f }; 
            float user1{ 0.0f }; 

            bool alive{ false };
        };

        // エミッターの共通設定
        struct EmitterConfig {
            int maxParticles{ 128 };

            // 連続発生のパラメータ（雨・トレイル用）
            float emitRate{ 0.0f };
            int burstCount{ 0 };

            // 寿命 / サイズの範囲
            float lifeMin{ 0.3f };
            float lifeMax{ 1.0f };
            float sizeMin{ 4.0f };
            float sizeMax{ 16.0f };

            // 直線発射のときに使う速度範囲
            Vector2 velMin{ -50.0f, -50.0f };
            Vector2 velMax{ 50.0f, 50.0f };

            float speedMin{ 50.0f };
            float speedMax{ 200.0f };

            // 色の補間
            unsigned int startColor{ 0xFFFFFFFF };
            unsigned int endColor{ 0xFFFFFF00 };

            // エミッターの基準位置（followTransform がないときに使う）
            Vector2 originPosition{ 0.0f, 0.0f };

            // 追従ターゲット
            const HIKARI::Transform2D* followTransform{ nullptr };
            Vector2 localOffset{ 0.0f, 0.0f };

            // 物理設定
            PhysicsParam physics{};

            Vector2 baseDirection{ 1.0f, 0.0f };  // ConeStream の基準方向
            float spreadDeg{ 0.0f };              // 発射の開き角
            Vector2 areaHalfSize{ 0.0f, 0.0f };   // Area 発射用のエリア半径（半径相当）
            const HIKARI::Transform2D* targetTransform{ nullptr }; // 吸い寄せターゲット（将来用）

            // 円環系 / 軌道系に使う
            float ringRadius{ 0.0f };   // 初期のリング半径
            int   ringSegments{ 32 };   // 円周上のパーティクル数

            // 吸い寄せ系 / 軌道系に使う
            float attractStrength{ 0.0f }; // 吸引強度（>0 なら Update で target 方向への加速度として使う）
        };

        // パーティクルを1個描画する関数
        using DrawFunc = std::function<void(const Particle&)>;

        // パーティクルを1個生成する関数（生成位置 / 速度 / 寿命などを設定する）
        using SpawnFunc = std::function<void(const EmitterConfig&,
            Particle&,
            const Vector2& emitterPos)>;

        // 1つのエミッター：パーティクルの集合 + 発生ロジック
        class Emitter {
        public:
            Emitter(const EmitterConfig& cfg, DrawFunc draw, SpawnFunc spawn);

            void Update(float dt);
            void Draw() const;

            // 一度だけのバースト発生
            void EmitBurst(int count);

            void SetActive(bool active_) { active = active_; }
            bool IsActive() const { return active; }

            const EmitterConfig& GetConfig() const { return config; }
            EmitterConfig& GetConfig() { return config; }

        private:
            EmitterConfig config;
            std::vector<Particle> particles;
            DrawFunc drawFunc;
            SpawnFunc spawnFunc;

            float emitTimer{ 0.0f };
            bool active{ true };

            void SpawnOne(const Vector2& emitterPos);
        };

        // 「エフェクトのひな型」：設定 + 描画方法 + 発生パターンのプリセット
        struct EffectPrototype {
            EmitterConfig config;
            DrawFunc drawFunc;
            SpawnFunc spawnFunc;
        };

        // パーティクルシステム
        class ParticleSystem {
        public:
            // エミッターの直接作成 / 破棄
            Emitter* CreateEmitter(const EmitterConfig& cfg, DrawFunc draw, SpawnFunc spawn);
            void DestroyEmitter(Emitter* emitter);

            // 「エフェクトのひな型」を登録する
            void RegisterEffect(const std::string& name, const EffectPrototype& proto);

            // 名前から対応する Emitter を取り出す（まだなければひな型から作る）
            Emitter* GetEffectEmitter(const std::string& name);

            // 一度だけの瞬間的なエフェクトを再生する
            void PlayOneShot(const std::string& name, const Vector2& pos, int burstCount);

            // 毎フレームの更新 / 描画
            void Update(float dt);
            void Draw() const;

        private:
            std::vector<Emitter*> emitters;
            std::unordered_map<std::string, EffectPrototype> effectTable;
            std::unordered_map<std::string, Emitter*> effectEmitters;
        };

        namespace PRESET {
#pragma region $DrawFunc$
            // 円形パーティクル（光点）を描画
            DrawFunc MakeCircleDrawer(HIKARI::RENDERER::CameraMode camMode);

            // 矩形パーティクルを描画（枠線オプション付き）
            DrawFunc MakeBoxDrawer(bool outline, HIKARI::RENDERER::CameraMode camMode);

            // === 三角形パーティクル ===
            DrawFunc MakeTriangleDrawer(bool outline, HIKARI::RENDERER::CameraMode camMode);

            // === 1枚のテクスチャ全体をパーティクルとして描画 ===
            DrawFunc MakeSpriteDrawer(
                const std::string& textureName,
                float width,
                float height,
                HIKARI::RENDERER::CameraMode camMode
            );

            // === テクスチャアニメ（グリッド分割）のパーティクル ===
            DrawFunc MakeSpriteAnimDrawer(
                const std::string& textureName,
                int frameWidth,
                int frameHeight,
                int columns,
                int rows,
                float fps,
                HIKARI::RENDERER::CameraMode camMode
            );
            // === 速度方向に伸びる矩形パーティクル ===
            DrawFunc MakeVelocityStretchBoxDrawer(
                HIKARI::RENDERER::CameraMode camMode
            );

            // === リング状の輪郭を描くパーティクル ===
            DrawFunc MakeRingDrawer(
                HIKARI::RENDERER::CameraMode camMode
            );

            // === 速度ベースの線分パーティクル ===
            DrawFunc MakeVelocityLineDrawer(
                HIKARI::RENDERER::CameraMode camMode
            );

            // === 霧っぽいパーティクル ===
            DrawFunc MakeFogDiscDrawer(
                HIKARI::RENDERER::CameraMode camMode
            );

            // === 十字星のようなパーティクル ===
            DrawFunc MakeCrossStarDrawer(
                bool withDiagonal,
                HIKARI::RENDERER::CameraMode camMode
            );

            // === 稲妻（雷）っぽいポリラインパーティクル ===
            // segments : 稲妻を何分割するか
            // amplitude: 横方向の最大振れ幅
            DrawFunc MakeLightningBoltDrawer(
                int segments,
                float amplitude,
                HIKARI::RENDERER::CameraMode camMode
            );

            // === スピードライン（高速移動の演出用） ===
            DrawFunc MakeSpeedLineDrawer(
                HIKARI::RENDERER::CameraMode camMode
            );


#pragma endregion

#pragma region $SpawnFunc$
            // 拡散型：1点から四方へ飛び散る
            SpawnFunc MakeRadialBurstSpawn();
            // 集中型：特定方向付近の円錐（コーン）状に噴射
            SpawnFunc MakeConeStreamSpawn();
            // トレイル型：位置はエミッターに追従しつつ、少しランダム揺らぎをつける
            SpawnFunc MakeTrailSpawn();
            // 雪 / 雨のように降らせる
            SpawnFunc MakeSnowfallSpawn();
            // 衝撃波のリング
            SpawnFunc MakeShockwaveRingSpawn();
            // エリア内をふわふわ漂う
            SpawnFunc MakeAreaFogSpawn();
            // 噴水
            SpawnFunc MakeFountainSpawn();
            // 周囲を回る軌道
            SpawnFunc MakeOrbitSpawn();
            // 吸い寄せ
            SpawnFunc MakeHomingSpawn();
            // 雷撃：上から地面にビカッと落ちるようなパーティクル発生
            SpawnFunc MakeLightningStrikeSpawn();
            // 画面全体スピードラインのSpawn
            SpawnFunc MakeScreenSpeedLineSpawn();


#pragma endregion

#pragma region $EmitterConfig$
            // 拡散爆発用の設定
            EmitterConfig MakeRadialBurstConfig(
                const Vector2& origin,
                int maxParticles,
                float lifeMin,
                float lifeMax,
                float speedMin,
                float speedMax,
                unsigned int startColor,
                unsigned int endColor
            );

            // 集中噴射用の設定（火炎 / 火花レイなど）
            EmitterConfig MakeConeStreamConfig(
                const Vector2& origin,
                int maxParticles,
                const Vector2& direction,
                float spreadDeg,
                float lifeMin,
                float lifeMax,
                float speedMin,
                float speedMax,
                unsigned int startColor,
                unsigned int endColor
            );

            // トレイル用設定（Transform 追従）
            EmitterConfig MakeTrailConfig(
                const HIKARI::Transform2D* follow,
                int maxParticles,
                float lifeMin,
                float lifeMax,
                float emitRate,
                unsigned int startColor,
                unsigned int endColor
            );


            // 降雪用設定
            EmitterConfig MakeSnowfallConfig(
                const Vector2& areaCenter,
                const Vector2& areaHalfSize,
                int maxParticles,
                float lifeMin,
                float lifeMax,
                float fallSpeedMin,
                float fallSpeedMax,
                unsigned int startColor,
                unsigned int endColor
            );

            // 衝撃波リング用設定
            EmitterConfig MakeShockwaveRingConfig(
                const Vector2& origin,
                int maxParticles,
                float lifeMin,
                float lifeMax,
                float radius,
                float speed,
                unsigned int startColor,
                unsigned int endColor,
                int ringSegments = 32
            );
            // エリア漂い用設定
            EmitterConfig MakeAreaFogConfig(
                const Vector2& areaCenter,
                const Vector2& areaHalfSize,
                int maxParticles,
                float lifeMin,
                float lifeMax,
                float moveSpeedMin,
                float moveSpeedMax,
                unsigned int startColor,
                unsigned int endColor
            );
            // 噴水用設定
            EmitterConfig MakeFountainConfig(
                const Vector2& origin,
                int maxParticles,
                float lifeMin,
                float lifeMax,
                float speedMin,
                float speedMax,
                float spreadDeg,
                unsigned int startColor,
                unsigned int endColor,
                float gravityY // 下向きの重力（>0）
            );
            // 周回軌道用設定
            EmitterConfig MakeOrbitConfig(
                const HIKARI::Transform2D* center,
                int maxParticles,
                float radius,
                float lifeMin,
                float lifeMax,
                float tangentialSpeedMin,
                float tangentialSpeedMax,
                unsigned int startColor,
                unsigned int endColor,
                float attractStrength  // 大きいほど中心の軌道にぴったり張り付く
            );
            // 吸い寄せ用設定
            EmitterConfig MakeHomingConfig(
                const Vector2& spawnCenter,
                const Vector2& spawnHalfSize,
                const HIKARI::Transform2D* target,
                int maxParticles,
                float lifeMin,
                float lifeMax,
                float speedMin,
                float speedMax,
                unsigned int startColor,
                unsigned int endColor,
                float attractStrength
            );
            // バースト（円上に並べて一斉発生）用設定
            EmitterConfig MakeBurstOnCircleConfig(
                const Vector2& origin,
                int maxParticles,
                float lifeMin,
                float lifeMax,
                float radius,
                float speed,
                unsigned int startColor,
                unsigned int endColor,
                int ringSegments = 32
            );

            // 雷撃用設定（上から下へ落ちる稲妻）
            // boltLengthMin/Max : 稲妻の長さ
            // thicknessMin/Max  : 見た目の太さ
            EmitterConfig MakeLightningStrikeConfig(
                const Vector2& origin,
                int maxParticles,
                float lifeMin,
                float lifeMax,
                float boltLengthMin,
                float boltLengthMax,
                float thicknessMin,
                float thicknessMax,
                unsigned int startColor,
                unsigned int endColor
            );

            // スピードライン・トレイル用設定
            // follow         : 追従対象
             // lifeMin/Max    : 1本の速度線の寿命
            // emitRate       : 1秒あたりの生成数
             // speedMin/Max   : 速度線パーティクルの初速の長さ
            // thicknessMin/Max : 線の太さベース
            EmitterConfig MakeSpeedLineTrailConfig(
                const HIKARI::Transform2D* follow,
                int maxParticles,
                float lifeMin,
                float lifeMax,
                float emitRate,
                float speedMin,
                float speedMax,
                float thicknessMin,
                float thicknessMax,
                unsigned int startColor,
                unsigned int endColor
            );
            // === 画面全体用スピードライン設定 ===
            EmitterConfig MakeScreenSpeedLineConfig(
                const Vector2& origin,
                int maxParticles,
                float lifeMin,
                float lifeMax,
                float emitRate,
                const Vector2& dir,
                const Vector2& areaHalfSize,
                float speedMin,
                float speedMax,
                float thicknessMin,
                float thicknessMax,
                unsigned int startColor,
                unsigned int endColor
            );

#pragma endregion
        }

    } // namespace PARTICLE
} // namespace HIKARI
