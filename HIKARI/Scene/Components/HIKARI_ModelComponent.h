#pragma once
#include <string>
#include <cstdint>

#include "HIKARI_IComponent.h"

namespace HIKARI {

    class ModelAsset;

    class ModelComponent final : public IComponent {
    public:
        std::string_view GetTypeName() const override { return "ModelComponent"; }

        void SetAsset(ModelAsset* asset);
        ModelAsset* GetAsset();
        const ModelAsset* GetAsset() const;

        void SetVisible(bool visible);
        bool IsVisible() const;

        void RenderImGui() override;
        void Serialize(nlohmann::json& out) const override;
        void Deserialize(const nlohmann::json& in) override;
        void BuildInspector(IInspectorBuilder& builder) override;

        const std::string& GetAssetId() const;
        void SetAssetId(std::string assetId);

        void SetPostGroupMask(uint32_t mask);
        uint32_t GetPostGroupMask() const;

        void SetMaterialFxProfileId(std::string profileId);
        const std::string& GetMaterialFxProfileId() const;

    private:
        ModelAsset* asset_ = nullptr;
        std::string assetId_{};
        bool visible_ = true;
        uint32_t postGroupMask_ = 0;
        std::string materialFxProfileId_{};
    };

} // namespace HIKARI
