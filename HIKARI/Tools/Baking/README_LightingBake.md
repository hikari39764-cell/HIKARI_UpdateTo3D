# Lighting Bake Boundary

This folder documents the boundary between future editor baking tools and runtime lighting.

- Editor baker will generate `LightingBakeManifest`.
- Runtime loader consumes the manifest and runtime-ready DDS artifacts.
- Phase 7 only defines the schema and loader boundary.
- Phase 7 does not add a Bake Lighting command, job system, DDS generation, SSR, planar reflection, or runtime probe capture.
