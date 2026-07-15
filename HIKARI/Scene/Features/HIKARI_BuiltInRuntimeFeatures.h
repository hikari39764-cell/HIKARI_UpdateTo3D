#pragma once

#include <span>

#include "Scene/Features/HIKARI_RuntimeFeature.h"

namespace HIKARI {

std::span<const RuntimeFeatureDescriptor> GetBuiltInRuntimeFeatures();
void RegisterBuiltInRuntimeFeatures(RuntimeFeatureContext& context);

} // namespace HIKARI
