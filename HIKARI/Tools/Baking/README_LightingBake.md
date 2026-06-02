# Lighting Bake Boundary

This folder documents the boundary between future editor baking tools and runtime lighting.

- Editor baker will generate `LightingBakeManifest`.
- Runtime loader consumes the manifest and runtime-ready DDS artifacts.
- Phase 7 defined the schema and loader boundary.
- Phase 9 added `LightingBakeService` and an editor `Lighting Bake` panel.
- Phase 9 can validate the current scene, create `Library/Generated/Lighting/<sceneGuid>/`, write `bake_manifest.json`, and clear that generated folder.
- Phase 11 adds scene-frame GPU capture for reflection probes and light probe volumes.
- Phase 11 light probe baking captures cubemaps in the scene layer, projects SH9 in `Tools/Baking`, and writes runtime-ready `.hlpv` data for the lighting runtime loader.
- Lightmaps, SSR, planar reflection, and runtime probe capture are still outside this folder's current scope.

`Tools/Baking` is editor/tool producer code. Runtime systems should continue to consume data through `Assets/Lighting/HIKARI_LightingBakeManifest.*` and the lighting runtime loader, not depend on this folder directly.
