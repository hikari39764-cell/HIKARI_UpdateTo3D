#include "HIKARI_GfxDebugConfig.h"

namespace HIKARI::GFX {
    namespace {
	// グローバルなグラフィックスデバッグ設定を保持する変数
        GfxDebugConfig gConfig{};
    }
	// グローバルなグラフィックスデバッグ設定を取得する
    const GfxDebugConfig& GetGfxDebugConfig() {
        return gConfig;
    }
	// グローバルなグラフィックスデバッグ設定を更新する
    void SetGfxDebugConfig(const GfxDebugConfig& config) {
        gConfig = config;
    }
}
