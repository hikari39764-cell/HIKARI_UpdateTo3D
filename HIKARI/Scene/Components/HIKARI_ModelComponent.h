#pragma once
#include "HIKARI_IComponent.h"

namespace HIKARI {

    class ModelAsset;

    class ModelComponent final : public IComponent {
    public:
        const char* GetTypeName() const override { return "ModelComponent"; }

        void SetAsset(ModelAsset* asset);
        ModelAsset* GetAsset();
        const ModelAsset* GetAsset() const;

        void SetVisible(bool visible);
        bool IsVisible() const;

        void RenderImGui() override;

    private:
        ModelAsset* asset_ = nullptr;
        bool visible_ = true;
    };

} // namespace HIKARI
