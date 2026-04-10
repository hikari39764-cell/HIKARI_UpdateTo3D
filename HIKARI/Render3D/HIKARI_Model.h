#pragma once

namespace HIKARI {

    class ModelAsset;

    class Model {
    public:
        void SetAsset(ModelAsset* asset) { asset_ = asset; }
        ModelAsset* GetAsset() { return asset_; }
        const ModelAsset* GetAsset() const { return asset_; }

    private:
        ModelAsset* asset_ = nullptr;
    };

} // namespace HIKARI
