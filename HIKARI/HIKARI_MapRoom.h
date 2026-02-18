#pragma once
#include <string>
#include <vector>
#include <memory>

#include <json.hpp>

#include "HIKARI_Collision.h"
#include "HIKARI_TiledCollision.h"
#include "HIKARI_Renderer.h" 
#include "HIKARI_Transform2D.h"

namespace HIKARI {
    namespace MAP {

        using nlohmann::json;
        using HIKARI::COLLISION::Rect;
        using HIKARI::COLLISION::Collider;


        struct TilesetInfo {
            int firstgid = 0;
            int tileCount = 0;
            int columns = 0;
            int tileWidth = 0;
            int tileHeight = 0;

            std::string imagePath; 
            std::string textureName; 
        };

        struct TileLayer {
            std::string name;
            bool        visible = true;
            float       opacity = 1.0f;
            std::vector<int> data;
        };

        class MapRoom {
        public:
            MapRoom() = default;

            /// 从 Tiled 导出的 JSON 文件加载地图
            /// - path: "./maps/stage1.json"
            /// - collisionLayerName: 瓦片碰撞层的layer 名（例如 "Collision"）
            bool LoadFromJson(const std::string& path,
                const std::string& collisionLayerName = "Collision",
                const std::string& objectLayerFilter = "");

            void Draw(HIKARI::RENDERER::CameraMode cam = HIKARI::RENDERER::CameraMode::Inherit,
                unsigned int rgba = 0xFFFFFFFF) const;

            // ------------ 数据访问接口 ------------

            int GetMapWidth()  const { return mapWidth_; }
            int GetMapHeight() const { return mapHeight_; }
            int GetTileWidth() const { return tileWidth_; }
            int GetTileHeight()const { return tileHeight_; }

            const std::vector<TilesetInfo>& GetTilesets() const { return tilesets_; }
            const std::vector<TileLayer>& GetTileLayers() const { return layers_; }

            /// 瓦片碰撞矩形
            const std::vector<Rect>& GetTileCollisionRects() const {
                return tileCollisionRects_;
            }

            /// 对象层的 Collider
            const std::vector<Collider>& GetObjectColliders() const {
                return objectColliders_;
            }

        private:
            const TilesetInfo* FindTileset(int gid) const;

        private:
            // 原始 JSON
            json mapJson_;

            // 基本信息
            int mapWidth_ = 0;
            int mapHeight_ = 0;
            int tileWidth_ = 0;
            int tileHeight_ = 0;

            // Tileset 信息
            std::vector<TilesetInfo> tilesets_;

            // Tile layers
            std::vector<TileLayer> layers_;

            // 碰撞数据缓存
            std::vector<Rect>     tileCollisionRects_;
            std::vector<Collider> objectColliders_;

            // 碰撞用的 tile layer 名
            std::string collisionLayerName_;
        };

    } // namespace MAP
} // namespace HIKARI
