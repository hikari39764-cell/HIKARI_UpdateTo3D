# Lighting Bake Boundary

This folder documents the boundary between future editor baking tools and runtime lighting.

- Editor baker will generate `LightingBakeManifest`.
- Runtime loader consumes the manifest and runtime-ready DDS artifacts.
- Phase 7 defined the schema and loader boundary.
- Phase 9 adds `LightingBakeService` and an editor `Lighting Bake` panel.
- Phase 9 can validate the current scene, create `Library/Generated/Lighting/<sceneGuid>/`, write `bake_manifest.json`, and clear that generated folder.
- Phase 9 intentionally writes empty reflection-probe, light-probe, and lightmap records.
- Phase 9 does not add a job system, DDS generation, SH generation, SSR, planar reflection, runtime probe capture, or render pipeline changes.

`Tools/Baking` is editor/tool producer code. Runtime systems should continue to consume data through `Assets/Lighting/HIKARI_LightingBakeManifest.*` and the lighting runtime loader, not depend on this folder directly.
