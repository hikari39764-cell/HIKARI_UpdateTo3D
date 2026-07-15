#pragma once

#include <string_view>

namespace HIKARI {

class ComponentRegistry;
class SystemTypeRegistry;

struct RuntimeFeatureContext {
    ComponentRegistry& componentRegistry;
    SystemTypeRegistry& systemTypeRegistry;
};

using RuntimeFeatureRegisterFn = void(*)(RuntimeFeatureContext& context);

struct RuntimeFeatureDescriptor {
    std::string_view featureId{};
    RuntimeFeatureRegisterFn registerFeature = nullptr;
};

} // namespace HIKARI
