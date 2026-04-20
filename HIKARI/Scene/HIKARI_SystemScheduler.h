#pragma once

#include <memory>
#include <vector>
#include "Scene/HIKARI_ISystem.h"
namespace HIKARI {

class World;
struct FrameContext;

class SystemScheduler {
public:
    void Clear();

    void AddSystem(std::unique_ptr<ISystem> system);

    void AttachWorld(World& world);
    void DetachWorld(World& world);

    void PreUpdate(World& world, const FrameContext& frame);
    void Update(World& world, const FrameContext& frame);
    void LateUpdate(World& world, const FrameContext& frame);

    void PreRender(World& world, const FrameContext& frame);
    void Render(World& world, const FrameContext& frame);
    void PostRender(World& world, const FrameContext& frame);

private:
    std::vector<std::unique_ptr<ISystem>> systems_{};
};

} // namespace HIKARI
