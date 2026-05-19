#pragma once

namespace HIKARI::RENDER3D {

    class RenderPipeline {
    public:
        void Initialize();
        void Shutdown();

        void BeginFrame();
        void RenderFrame();
        void EndFrame();

    private:
        bool initialized_ = false;
    };

} // namespace HIKARI::RENDER3D
