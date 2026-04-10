#pragma once

namespace HIKARI {

    struct SceneLighting;

    class LightingPanel {
    public:
        void Draw(SceneLighting& lighting) const;
    };

} // namespace HIKARI
