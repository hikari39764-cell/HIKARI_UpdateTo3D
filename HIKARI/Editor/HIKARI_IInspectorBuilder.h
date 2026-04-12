#pragma once

#include <string>
#include <string_view>

namespace HIKARI {

    class IInspectorBuilder {
    public:
        virtual ~IInspectorBuilder() = default;

        virtual bool Bool(std::string_view label, bool& value) = 0;
        virtual bool String(std::string_view label, std::string& value) = 0;
    };

} // namespace HIKARI
