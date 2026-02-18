#include "HIKARI_TiledCollision.h"

namespace HIKARI {
    namespace TILED {

        using namespace HIKARI::COLLISION;

        std::vector<Rect>
            BuildTileCollisionRectsFromJson(const json& mapJson,
                const std::string& collisionLayerName) {
            std::vector<Rect> result;

            const int mapWidth = mapJson.value("width", 0);
            const int mapHeight = mapJson.value("height", 0);
            const int tileWidth = mapJson.value("tilewidth", 0);
            const int tileHeight = mapJson.value("tileheight", 0);

            if (!mapJson.contains("layers") || mapWidth <= 0 || mapHeight <= 0) {
                return result;
            }

            for (const auto& layer : mapJson["layers"]) {
                if (layer.value("type", std::string("")) != "tilelayer") {
                    continue;
                }

                std::string name = layer.value("name", std::string(""));
                if (!collisionLayerName.empty() && name != collisionLayerName) {
                    continue;
                }

                if (!layer.contains("data") || !layer["data"].is_array()) {
                    continue;
                }

                const auto& data = layer["data"];
                if (static_cast<int>(data.size()) != mapWidth * mapHeight) {
                    continue;
                }

                for (int y = 0; y < mapHeight; ++y) {
                    for (int x = 0; x < mapWidth; ++x) {
                        int index = y * mapWidth + x;
                        int gid = data[index].get<int>(); 

                        if (gid == 0) {
                            continue; 
                        }

                        Rect r;
                        r.x = static_cast<float>(x * tileWidth);
                        r.y = static_cast<float>(y * tileHeight);
                        r.width = static_cast<float>(tileWidth);
                        r.height = static_cast<float>(tileHeight);
                        result.push_back(r);
                    }
                }

                // break;
            }

            return result;
        }

        // 从 properties 里读某个名字的 string 值
        static std::string ReadStringProperty(const json& obj, const std::string& propName) {
            if (!obj.contains("properties")) {
                return "";
            }
            for (const auto& prop : obj["properties"]) {
                std::string name = prop.value("name", std::string(""));
                if (name == propName && prop.contains("value")) {
                    if (prop["value"].is_string()) {
                        return prop["value"].get<std::string>();
                    }
                }
            }
            return "";
        }

        // 从 properties 里读 int 值
        static int ReadIntProperty(const json& obj, const std::string& propName, int defaultValue) {
            if (!obj.contains("properties")) {
                return defaultValue;
            }
            for (const auto& prop : obj["properties"]) {
                std::string name = prop.value("name", std::string(""));
                if (name == propName && prop.contains("value")) {
                    if (prop["value"].is_number_integer()) {
                        return prop["value"].get<int>();
                    }
                }
            }
            return defaultValue;
        }

        std::vector<Collider>
            BuildObjectCollidersFromJson(const json& mapJson,
                const std::string& layerNameFilter) {
            std::vector<Collider> result;

            if (!mapJson.contains("layers")) {
                return result;
            }

            for (const auto& layer : mapJson["layers"]) {
                if (layer.value("type", std::string("")) != "objectgroup") {
                    continue;
                }

                std::string layerName = layer.value("name", std::string(""));


                if (!layerNameFilter.empty() && layerName != layerNameFilter) {
                    continue;
                }

                if (!layer.contains("objects") || !layer["objects"].is_array()) {
                    continue;
                }

                for (const auto& obj : layer["objects"]) {
                    Collider col;

                    float x = obj.value("x", 0.0f);
                    float y = obj.value("y", 0.0f);
                    float w = obj.value("width", 0.0f);
                    float h = obj.value("height", 0.0f);

                    bool isEllipse = obj.value("ellipse", false);

                    if (!isEllipse) {

                        col.shapeType = ShapeType::Rect;
                        col.rect.x = x;
                        col.rect.y = y;
                        col.rect.width = w;
                        col.rect.height = h;
                    } else {

                        col.shapeType = ShapeType::Circle;
                        float r = w * 0.5f;
                        col.circle.center.x = x + r;
                        col.circle.center.y = y + r;
                        col.circle.radius = r;
                    }

                    // 从 properties 里读 tag / layer
                    col.tag = ReadStringProperty(obj, "tag");
                    col.layer = ReadIntProperty(obj, "layer", 0);

                    result.push_back(col);
                }
            }

            return result;
        }

    } // namespace TILED
} // namespace HIKARI
