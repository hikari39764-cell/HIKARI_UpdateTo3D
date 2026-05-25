#include "HIKARI_AssetUsageAnalyzer.h"

#include "HIKARI_AssetDatabase.h"
#include "Scene/HIKARI_SceneDocument.h"

namespace HIKARI {

    namespace {
        void AddReference(
            const std::string& assetId,
            const std::string& owner,
            const std::string& role,
            const AssetDatabase& assetDatabase,
            AssetUsageSummary& summary) {

            if (assetId.empty()) {
                return;
            }

            if (const AssetRecord* record = assetDatabase.FindByGuid(AssetGuid{ assetId })) {
                summary.usedGuids.insert(record->guid.value);
                return;
            }

            summary.missingReferences.push_back(AssetMissingReference{
                assetId,
                owner,
                role
            });
        }
    }

    AssetUsageSummary AnalyzeAssetUsage(
        const SceneDocument& document,
        const AssetDatabase& assetDatabase) {

        AssetUsageSummary summary{};

        AddReference(
            document.environment.sky.skyAsset,
            "Scene Environment",
            "Sky",
            assetDatabase,
            summary);

        for (const SceneObjectData& object : document.objects) {
            const std::string owner = object.name.empty()
                ? ("Object " + std::to_string(object.id.value))
                : object.name;

            for (const SceneComponentData& component : object.components) {
                if (component.type == "ModelComponent") {
                    AddReference(
                        component.properties.value("assetId", std::string{}),
                        owner,
                        "Model",
                        assetDatabase,
                        summary);
                } else if (component.type == "VfxPlayerComponent") {
                    if (component.properties.contains("slots") && component.properties["slots"].is_array()) {
                        for (const nlohmann::json& slot : component.properties["slots"]) {
                            AddReference(
                                slot.value("effectAssetId", std::string{}),
                                owner,
                                "VFX",
                                assetDatabase,
                                summary);
                        }
                    }
                }
            }
        }

        return summary;
    }

} // namespace HIKARI
