#pragma once
#include "HIKARI_PostChain.h"
#include "HIKARI_PostEffect.h"

namespace HIKARI {
    namespace POST {

        class GlowManager
        {
        public:
            // 初始化：加载Shader，建立双层模糊链
            static void Init();
            
            // 清理资源
            static void Finalize();

            // --- 核心功能 ---
            
            // 开始发光层：在此之后 Draw 的东西都会发光
            static void Begin();
            
            // 结束发光层：将光晕混合回主画面
            static void End();

            // --- 参数调整 ---

            // 设置发光强度 (默认 1.0, 越大越亮)
            static void SetIntensity(float intensity);

            // 设置发光扩散半径 (默认 4.0)
            // 内部会自动处理多级模糊，防止出现像素块
            static void SetRadius(float radius);

        private:
            static void UpdateParams();

        private:
            static bool initialized_;
            
            static PostChain glowChain_;

            static PostEffect* blurSmallX_;
            static PostEffect* blurSmallY_;
            static PostEffect* blurBigX_;
            static PostEffect* blurBigY_;

            // 缓存参数
            static float currentIntensity_;
            static float currentRadius_;
        };

    } // POST
} // HIKARI