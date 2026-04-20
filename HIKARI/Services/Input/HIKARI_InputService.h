#pragma once

#include "HIKARI_Input.h"

namespace HIKARI::SERVICES {

struct InputService {
    static void Update(float dt) { HINPUT::Update(dt); }
};

} // namespace HIKARI::SERVICES
