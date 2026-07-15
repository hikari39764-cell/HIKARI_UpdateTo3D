#include "Scene/Features/HIKARI_SceneScanFxFeature.h"

#include <memory>

#include "Scene/Components/HIKARI_SceneScanFxComponent.h"
#include "Scene/HIKARI_ComponentRegistry.h"
#include "Scene/HIKARI_SceneScanFxSystem.h"
#include "Scene/HIKARI_SystemTypeRegistry.h"

namespace HIKARI {

void RegisterSceneScanFxFeature(RuntimeFeatureContext& context) {
    if (!context.componentRegistry.Find("SceneScanFxComponent")) {
        context.componentRegistry.Register(ComponentTypeInfo{
            "SceneScanFxComponent",
            []() -> std::unique_ptr<IComponent> { return std::make_unique<SceneScanFxComponent>(); },
            {},
            {},
            {},
            false,
            [](const SceneObjectData&, nlohmann::json& properties) {
                properties["enabled"] = true;
                properties["triggerActionName"] = "PlaySceneScan";
                properties["autoPlay"] = false;
                properties["sourceObjectId"] = 0;
                properties["skipSourceObject"] = true;
                properties["overrideExistingFx"] = false;
                properties["restoreOnStop"] = true;
                properties["radius"] = 28.0f;
                properties["speed"] = 16.0f;
                properties["bandWidth"] = 3.2f;
                properties["triangleCellSize"] = 2.8f;
                properties["triangleLineWidth"] = 0.12f;
                properties["noiseScale"] = 0.65f;
                properties["flickerStrength"] = 0.35f;
                properties["intensity"] = 3.2f;
                properties["color"] = nlohmann::json::array({ 0.08f, 1.0f, 0.92f, 0.88f });
            }
        });
    }

    if (!context.systemTypeRegistry.Find("SceneScanFxSystem")) {
        context.systemTypeRegistry.Register(SystemTypeInfo{
            "SceneScanFxSystem",
            [](const nlohmann::json&) -> std::unique_ptr<ISystem> {
                return std::make_unique<SceneScanFxSystem>();
            }
        });
    }
}

} // namespace HIKARI
