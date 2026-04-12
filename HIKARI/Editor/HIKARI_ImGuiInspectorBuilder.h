#pragma once

#include "HIKARI_IInspectorBuilder.h"

namespace HIKARI {

    class ImGuiInspectorBuilder final : public IInspectorBuilder {
    public:
        bool Bool(std::string_view label, bool& value) override;
        bool String(std::string_view label, std::string& value) override;
    };

} // namespace HIKARI
