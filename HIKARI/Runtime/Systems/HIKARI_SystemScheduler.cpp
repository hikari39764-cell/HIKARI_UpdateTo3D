#include "HIKARI_SystemScheduler.h"

#include "Runtime/Core/HIKARI_World.h"
#include "Runtime/Systems/HIKARI_ISystem.h"
#include "Services/Frame/HIKARI_FrameContext.h"

namespace HIKARI {

SystemScheduler::~SystemScheduler() = default;
SystemScheduler::SystemScheduler(SystemScheduler&&) noexcept = default;
SystemScheduler& SystemScheduler::operator=(SystemScheduler&&) noexcept = default;

void SystemScheduler::AttachWorld(World* world) {
    if (world_ == world) {
        return;
    }

    if (world_) {
        for (const auto& system : systems_) {
            system->OnWorldDetached(*world_);
        }
    }

    world_ = world;
    if (world_) {
        for (const auto& system : systems_) {
            system->OnWorldAttached(*world_);
        }
    }
}

void SystemScheduler::DetachWorld() {
    AttachWorld(nullptr);
}

void SystemScheduler::Register(std::unique_ptr<ISystem> system) {
    if (!system) {
        return;
    }

    if (world_) {
        system->OnWorldAttached(*world_);
    }

    systems_.push_back(std::move(system));
}

void SystemScheduler::PreUpdate(const FrameContext& frame) {
    if (!world_) return;
    for (const auto& system : systems_) system->PreUpdate(*world_, frame);
}

void SystemScheduler::Update(const FrameContext& frame) {
    if (!world_) return;
    for (const auto& system : systems_) system->Update(*world_, frame);
}

void SystemScheduler::LateUpdate(const FrameContext& frame) {
    if (!world_) return;
    for (const auto& system : systems_) system->LateUpdate(*world_, frame);
}

void SystemScheduler::PreRender(const FrameContext& frame) {
    if (!world_) return;
    for (const auto& system : systems_) system->PreRender(*world_, frame);
}

void SystemScheduler::Render(const FrameContext& frame) {
    if (!world_) return;
    for (const auto& system : systems_) system->Render(*world_, frame);
}

void SystemScheduler::PostRender(const FrameContext& frame) {
    if (!world_) return;
    for (const auto& system : systems_) system->PostRender(*world_, frame);
}

} // namespace HIKARI
