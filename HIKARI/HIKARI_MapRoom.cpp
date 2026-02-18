#include "HIKARI_MapRoom.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include "HIKARI_Camera.h"
#include "HIKARI_Texture.h"
#include <Windows.h>
#undef min
#undef max
namespace HIKARI {
    namespace MAP {

        using namespace HIKARI::COLLISION;
        using namespace HIKARI::RENDERER;
        using nlohmann::json;

        bool MapRoom::LoadFromJson(const std::string& path,
            const std::string& collisionLayerName,
            const std::string& objectLayerFilter)
        {
            collisionLayerName_ = collisionLayerName;

            // 1) 读 JSON 文件
            std::ifstream ifs(path);
            if (!ifs) {
                return false;
            }
            std::stringstream ss;
            ss << ifs.rdbuf();

            mapJson_ = json::parse(ss.str(), nullptr, false);
            if (mapJson_.is_discarded()) {
                return false;
            }

            // 2) 基本信息
            mapWidth_ = mapJson_.value("width", 0);
            mapHeight_ = mapJson_.value("height", 0);
            tileWidth_ = mapJson_.value("tilewidth", 0);
            tileHeight_ = mapJson_.value("tileheight", 0);

            // 3) Tileset (渲染 + 贴图名 注册)
            tilesets_.clear();
            if (mapJson_.contains("tilesets") && mapJson_["tilesets"].is_array()) {
                for (const auto& ts : mapJson_["tilesets"]) {
                    TilesetInfo info;
                    info.firstgid = ts.value("firstgid", 0);
                    info.tileCount = ts.value("tilecount", 0);
                    info.columns = ts.value("columns", 0);
                    info.tileWidth = ts.value("tilewidth", tileWidth_);
                    info.tileHeight = ts.value("tileheight", tileHeight_);

                    info.imagePath = ts.value("image", std::string(""));

                    // 直接用 imagePath 当成 textureName（保持路径一致，避免你贴图路径乱掉）
                    info.textureName = info.imagePath;

                    // 在 HIKARI::TEXTURE 注册一下（group 给个 "map"）
                    if (!info.imagePath.empty()) {
                        HIKARI::TEXTURE::Register(info.textureName, info.imagePath, "map");
                    }

                    tilesets_.push_back(info);
                }
            }

            // 4) tile layers（只负责保存 data，画的时候再用）
            layers_.clear();
            if (mapJson_.contains("layers") && mapJson_["layers"].is_array()) {
                for (const auto& layer : mapJson_["layers"]) {
                    std::string type = layer.value("type", std::string(""));
                    if (type != "tilelayer") {
                        continue;
                    }

                    TileLayer tl;
                    tl.name = layer.value("name", std::string(""));
                    tl.visible = layer.value("visible", true);
                    tl.opacity = layer.value("opacity", 1.0f);

                    if (layer.contains("data") && layer["data"].is_array()) {
                        const auto& data = layer["data"];
                        tl.data.reserve(data.size());
                        for (const auto& cell : data) {
                            tl.data.push_back(cell.get<int>());
                        }
                    }

                    layers_.push_back(tl);
                }
            }

            // 5) 碰撞数据：用之前做好的 Tiled helper 来抽
            // 5-1 瓦片碰撞（tile layer: collisionLayerName）
            tileCollisionRects_ = TILED::BuildTileCollisionRectsFromJson(
                mapJson_,
                collisionLayerName_
            );

            // 5-2 Object layer Collider（带 tag / layer 的那种）
            objectColliders_ = TILED::BuildObjectCollidersFromJson(
                mapJson_,
                objectLayerFilter
            );

            return true;
        }

        const TilesetInfo* MapRoom::FindTileset(int gid) const {
            const TilesetInfo* result = nullptr;
            for (const auto& ts : tilesets_) {
                if (gid >= ts.firstgid) {
                    result = &ts;
                } else {
                    break;
                }
            }
            return result;
        }

        void MapRoom::Draw(CameraMode cam, unsigned int rgba) const {
            if (mapWidth_ <= 0 || mapHeight_ <= 0) {
                return;
            }

            CAMERA::State camState = CAMERA::GetState();
            float screenW = static_cast<float>(CAMERA::GetScreenWidth());
            float screenH = static_cast<float>(CAMERA::GetScreenHeight());

            float halfViewW = (screenW * 0.5f) / camState.scale.x;
            float halfViewH = (screenH * 0.5f) / camState.scale.y;

            float camX = camState.position.x;
            float camY = camState.position.y;

            float worldLeft = camX - halfViewW;
            float worldRight = camX + halfViewW;
            float worldTop = camY - halfViewH;
            float worldBottom = camY + halfViewH;

            int x0 = static_cast<int>(std::floor(worldLeft / tileWidth_));
            int x1 = static_cast<int>(std::ceil(worldRight / tileWidth_)) - 1;
            int y0 = static_cast<int>(std::floor(worldTop / tileHeight_));
            int y1 = static_cast<int>(std::ceil(worldBottom / tileHeight_)) - 1;

            x0 = std::max(0, x0);
            y0 = std::max(0, y0);
            x1 = std::min(mapWidth_ - 1, x1);
            y1 = std::min(mapHeight_ - 1, y1);

            if (x0 > x1 || y0 > y1) {
                return;
            }

            for (const auto& layer : layers_) {
                if (!layer.visible) {
                    continue;
                }

                if (layer.data.size() != static_cast<size_t>(mapWidth_ * mapHeight_)) {
                    continue;
                }

                for (int y = y0; y <= y1; ++y) {
                    for (int x = x0; x <= x1; ++x) {
                        int index = y * mapWidth_ + x;
                        int gid = layer.data[index];
                        if (gid == 0) { continue; }

                        const TilesetInfo* ts = FindTileset(gid);
                        if (!ts || ts->textureName.empty()) { continue; }

                        int localId = gid - ts->firstgid;
                        if (localId < 0) { continue; }

                        int col = 0;
                        int row = 0;
                        if (ts->columns > 0) {
                            col = localId % ts->columns;
                            row = localId / ts->columns;
                        }

                        int srcX = col * ts->tileWidth;
                        int srcY = row * ts->tileHeight;
                        int srcW = ts->tileWidth;
                        int srcH = ts->tileHeight;

                        float worldX = static_cast<float>(x * tileWidth_);
                        float worldY = static_cast<float>(y * tileHeight_);

                        Transform2D t{};
                        t.position = { worldX, worldY };
                        t.scale = { 1.0f, 1.0f };
                        t.rotation = 0.0f;
                        t.pivotPx = { 0.0f, 0.0f };

                        int texHandle = TEXTURE::GetHandle(ts->textureName);
                        if (texHandle < 0) { continue; }

                        RENDERER::DrawSpriteRectHandle(
                            texHandle,
                            srcX, srcY, srcW, srcH,
                            t,
                            static_cast<float>(tileWidth_),
                            static_cast<float>(tileHeight_),
                            cam,
                            rgba
                        );
                    }
                }
            }
        }


    } // namespace MAP
} // namespace HIKARI
