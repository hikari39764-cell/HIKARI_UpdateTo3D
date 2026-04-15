#include "HIKARI_ComponentDocumentEditor.h"

#include "Scene/Components/HIKARI_IComponent.h"
#include "Scene/HIKARI_ComponentRegistry.h"
#include "Scene/HIKARI_SceneDocument.h"

namespace HIKARI {

    bool ComponentDocumentEditor::DrawComponent(
        const ComponentRegistry& componentRegistry,
        SceneComponentData& componentData,
        IInspectorBuilder& inspectorBuilder,
        const InspectorContext& context) const {
        const ComponentTypeInfo* typeInfo = componentRegistry.Find(componentData.type);
        if (!typeInfo || !typeInfo->factory) {
            return false;
        }

        std::unique_ptr<IComponent> temporaryComponent = typeInfo->factory();
        if (!temporaryComponent) {
            return false;
        }

        temporaryComponent->Deserialize(componentData.properties);
        inspectorBuilder.SetContext(context);
        temporaryComponent->BuildInspector(inspectorBuilder);

        nlohmann::json serialized = nlohmann::json::object();
        temporaryComponent->Serialize(serialized);
        if (serialized == componentData.properties) {
            return false;
        }

        componentData.properties = std::move(serialized);
        return true;
    }

} // namespace HIKARI
