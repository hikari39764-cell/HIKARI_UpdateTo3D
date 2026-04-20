#include "Scene/HIKARI_SystemScheduler.h"

#include "Scene/HIKARI_ISystem.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI {

void SystemScheduler::Clear() {
    systems_.clear();
}

void SystemScheduler::AddSystem(std::unique_ptr<ISystem> system) {
    if (!system) {
        return;
    }
    systems_.push_back(std::move(system));
}

void SystemScheduler::AttachWorld(World& world) {
    for (const auto& system : systems_) {
        system->OnWorldAttached(world);
    }
}

void SystemScheduler::DetachWorld(World& world) {
    for (const auto& system : systems_) {
        system->OnWorldDetached(world);
    }
}

void SystemScheduler::PreUpdate(World& world, const FrameContext& frame) {
    for (const auto& system : systems_) {
        system->PreUpdate(world, frame);
    }
}

void SystemScheduler::Update(World& world, const FrameContext& frame) {
    for (const auto& system : systems_) {
        system->Update(world, frame);
    }
}

void SystemScheduler::LateUpdate(World& world, const FrameContext& frame) {
    for (const auto& system : systems_) {
        system->LateUpdate(world, frame);
    }
}

void SystemScheduler::PreRender(World& world, const FrameContext& frame) {
    for (const auto& system : systems_) {
        system->PreRender(world, frame);
    }
}

void SystemScheduler::Render(World& world, const FrameContext& frame) {
    for (const auto& system : systems_) {
        system->Render(world, frame);
    }
}

void SystemScheduler::PostRender(World& world, const FrameContext& frame) {
    for (const auto& system : systems_) {
        system->PostRender(world, frame);
    }
}

} // namespace HIKARI
