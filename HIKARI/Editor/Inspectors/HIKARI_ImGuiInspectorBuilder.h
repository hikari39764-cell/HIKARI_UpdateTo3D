#pragma once

#include "HIKARI_IInspectorBuilder.h"

namespace HIKARI {

    class ImGuiInspectorBuilder final : public IInspectorBuilder {
    public:
        void SetContext(const InspectorContext& context) override;

        bool Bool(std::string_view label, bool& value) override;
        bool Int(std::string_view label, int& value) override;
        bool Float(std::string_view label, float& value) override;
        bool String(std::string_view label, std::string& value) override;
        bool Vec2(std::string_view label, float& x, float& y) override;
        bool AssetIdPicker(std::string_view label, AssetType assetType, std::string& value) override;
        bool SceneIdPicker(std::string_view label, std::string& value) override;

    private:
        InspectorContext context_{};
    };

} // namespace HIKARI
