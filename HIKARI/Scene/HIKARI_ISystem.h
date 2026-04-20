#pragma once

#include <string_view>

namespace HIKARI {

class World;
struct FrameContext;

class ISystem {
public:
    virtual ~ISystem() = default;

    virtual std::string_view GetName() const = 0;

    virtual void OnWorldAttached(World& world) {}
    virtual void OnWorldDetached(World& world) {}

    virtual void PreUpdate(World& world, const FrameContext& frame) {}
    virtual void Update(World& world, const FrameContext& frame) {}
    virtual void LateUpdate(World& world, const FrameContext& frame) {}

    virtual void PreRender(World& world, const FrameContext& frame) {}
    virtual void Render(World& world, const FrameContext& frame) {}
    virtual void PostRender(World& world, const FrameContext& frame) {}
};

} // namespace HIKARI
