// GPU-driven opaque entry point for the meshlet/cluster mainline.
// MaterialFx surfaces are routed to the traditional StaticFx sidecar so
// ordinary opaque meshlet pixels do not pay per-pixel SurfaceGpuScene FX reads.
#include "Render3D_StaticPS.hlsl"
