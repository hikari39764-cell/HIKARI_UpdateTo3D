#pragma once

#include <memory>
#include <vector>

namespace HIKARI {

class ISystem;
class World;
struct FrameContext;

class SystemScheduler {
public:
    SystemScheduler() = default;
    ~SystemScheduler();

    SystemScheduler(SystemScheduler&&) noexcept;
    SystemScheduler& operator=(SystemScheduler&&) noexcept;

    SystemScheduler(const SystemScheduler&) = delete;
    SystemScheduler& operator=(const SystemScheduler&) = delete;

    void AttachWorld(World* world);
    void DetachWorld();

    void Register(std::unique_ptr<ISystem> system);

    void PreUpdate(const FrameContext& frame);
    void Update(const FrameContext& frame);
    void LateUpdate(const FrameContext& frame);

    void PreRender(const FrameContext& frame);
    void Render(const FrameContext& frame);
    void PostRender(const FrameContext& frame);

private:
    World* world_ = nullptr;
    std::vector<std::unique_ptr<ISystem>> systems_{};
};

} // namespace HIKARI
