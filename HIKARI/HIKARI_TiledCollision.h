#pragma once
#include <vector>
#include <string>
#include "HIKARI_Collision.h"
#include <json.hpp> 

namespace HIKARI {
    namespace TILED {

        using nlohmann::json;


        std::vector<COLLISION::Rect>
            BuildTileCollisionRectsFromJson(const json& mapJson,
                const std::string& collisionLayerName);


        std::vector<COLLISION::Collider>
            BuildObjectCollidersFromJson(const json& mapJson,
                const std::string& layerNameFilter = "");

    } // namespace TILED
} // namespace HIKARI
