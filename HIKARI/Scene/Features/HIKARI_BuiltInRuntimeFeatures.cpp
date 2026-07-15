#include "Scene/Features/HIKARI_BuiltInRuntimeFeatures.h"

#include <array>

#include "Scene/Features/HIKARI_SceneScanFxFeature.h"

namespace HIKARI {
namespace {

constexpr std::array<RuntimeFeatureDescriptor, 1> kBuiltInRuntimeFeatures = {{
    RuntimeFeatureDescriptor{ "SceneScanFx", &RegisterSceneScanFxFeature }
}};

} // namespace

std::span<const RuntimeFeatureDescriptor> GetBuiltInRuntimeFeatures() {
    return kBuiltInRuntimeFeatures;
}

void RegisterBuiltInRuntimeFeatures(RuntimeFeatureContext& context) {
    for (const RuntimeFeatureDescriptor& descriptor : kBuiltInRuntimeFeatures) {
        if (descriptor.registerFeature) {
            descriptor.registerFeature(context);
        }
    }
}

} // namespace HIKARI
