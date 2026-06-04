#include "HIKARI_RuntimeLaunchConfig.h"

#include <Windows.h>

#include <array>
#include <fstream>
#include <system_error>
#include <utility>
#include <vector>

#include <json.hpp>

#include "HIKARI_Services.h"

namespace HIKARI {

    namespace {

        std::filesystem::path ExeDirectory() {
            std::array<wchar_t, MAX_PATH> buffer{};
            const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
            if (length == 0 || length >= buffer.size()) {
                return std::filesystem::current_path();
            }
            return std::filesystem::path(buffer.data()).parent_path();
        }

        const nlohmann::json& RuntimeNode(const nlohmann::json& root) {
            if (root.contains("runtime") && root["runtime"].is_object()) {
                return root["runtime"];
            }
            return root;
        }

        std::optional<bool> ReadBool(const nlohmann::json& node, const char* key) {
            if (!node.contains(key) || !node[key].is_boolean()) {
                return std::nullopt;
            }
            return node[key].get<bool>();
        }

        std::optional<int> ReadInt(const nlohmann::json& node, const char* key) {
            if (!node.contains(key) || !node[key].is_number_integer()) {
                return std::nullopt;
            }
            return node[key].get<int>();
        }

        std::optional<std::string> ReadString(const nlohmann::json& node, const char* key) {
            if (!node.contains(key) || !node[key].is_string()) {
                return std::nullopt;
            }
            return node[key].get<std::string>();
        }

        std::optional<std::vector<std::string>> ReadStringArray(const nlohmann::json& node, const char* key) {
            if (!node.contains(key) || !node[key].is_array()) {
                return std::nullopt;
            }

            std::vector<std::string> values{};
            for (const nlohmann::json& entry : node[key]) {
                if (!entry.is_string()) {
                    continue;
                }
                std::string value = entry.get<std::string>();
                if (!value.empty()) {
                    values.push_back(std::move(value));
                }
            }
            return values;
        }

        void AddCandidate(std::vector<std::filesystem::path>& candidates, const std::filesystem::path& root) {
            if (root.empty()) {
                return;
            }
            candidates.push_back(root / "HIKARI" / "runtime_config.json");
            candidates.push_back(root / "runtime_config.json");
        }

        std::filesystem::path ResolveConfigProjectRoot(const std::filesystem::path& path) {
            std::error_code ec{};
            std::filesystem::path normalized = std::filesystem::weakly_canonical(path, ec);
            if (ec || normalized.empty()) {
                ec.clear();
                normalized = std::filesystem::absolute(path, ec);
            }
            if (ec || normalized.empty()) {
                normalized = path;
            }

            std::filesystem::path configDirectory = normalized.lexically_normal().parent_path();
            if (configDirectory.filename() == "HIKARI") {
                configDirectory = configDirectory.parent_path();
            }
            return configDirectory.lexically_normal();
        }
    }

    RuntimeLaunchConfig LoadRuntimeLaunchConfigFromFile(const std::filesystem::path& path) {
        RuntimeLaunchConfig cfg{};
        std::ifstream ifs(path);
        if (!ifs) {
            return cfg;
        }

        nlohmann::json root = nlohmann::json::parse(ifs, nullptr, false);
        if (root.is_discarded() || !root.is_object()) {
            return cfg;
        }

        const nlohmann::json& runtime = RuntimeNode(root);
        if (!runtime.is_object()) {
            return cfg;
        }

        if (runtime.contains("hostMode") && runtime["hostMode"].is_string()) {
            RuntimeHostMode mode{};
            if (TryParseRuntimeHostMode(runtime["hostMode"].get<std::string>(), mode)) {
                cfg.hostMode = mode;
            }
        }

        cfg.enableImGui = ReadBool(runtime, "enableImGui");
        cfg.enableEditorUI = ReadBool(runtime, "enableEditorUI");
        cfg.enablePortableObjectTools = ReadBool(runtime, "enablePortableObjectTools");
        cfg.enableDebugLayer = ReadBool(runtime, "enableDebugLayer");
        cfg.enableDebugCamera = ReadBool(runtime, "enableDebugCamera");
        cfg.resizableWindow = ReadBool(runtime, "resizableWindow");
        cfg.windowWidth = ReadInt(runtime, "windowWidth");
        cfg.windowHeight = ReadInt(runtime, "windowHeight");
        cfg.startupSceneGuid = ReadString(runtime, "startupSceneGuid");
        cfg.exportedSceneGuids = ReadStringArray(runtime, "exportedSceneGuids");
        cfg.loaded = true;
        cfg.sourcePath = path;
        cfg.projectRoot = ResolveConfigProjectRoot(path);
        return cfg;
    }

    RuntimeLaunchConfig LoadRuntimeLaunchConfig() {
        std::vector<std::filesystem::path> candidates{};
        AddCandidate(candidates, std::filesystem::current_path());
        AddCandidate(candidates, ExeDirectory());

        std::error_code ec{};
        for (const std::filesystem::path& candidate : candidates) {
            if (std::filesystem::exists(candidate, ec) && !ec) {
                RuntimeLaunchConfig cfg = LoadRuntimeLaunchConfigFromFile(candidate);
                if (cfg.loaded) {
                    return cfg;
                }
            }
            ec.clear();
        }
        return {};
    }

    bool ApplyRuntimeWorkingDirectory(const RuntimeLaunchConfig& runtimeCfg) {
        if (!runtimeCfg.loaded || runtimeCfg.projectRoot.empty()) {
            return false;
        }

        std::error_code ec{};
        std::filesystem::current_path(runtimeCfg.projectRoot, ec);
        return !ec;
    }

    void ApplyRuntimeLaunchConfig(const RuntimeLaunchConfig& runtimeCfg, SERVICES::BootstrapConfig& servicesCfg) {
        if (runtimeCfg.hostMode) {
            servicesCfg.hostMode = *runtimeCfg.hostMode;
        }
        if (runtimeCfg.enableImGui) {
            servicesCfg.enableImGui = *runtimeCfg.enableImGui;
        }
        if (runtimeCfg.enableEditorUI) {
            servicesCfg.enableEditorUI = *runtimeCfg.enableEditorUI;
        }
        if (runtimeCfg.enablePortableObjectTools) {
            servicesCfg.enablePortableObjectTools = *runtimeCfg.enablePortableObjectTools;
        }
        if (runtimeCfg.enableDebugLayer) {
            servicesCfg.enableDebugLayer = *runtimeCfg.enableDebugLayer;
        }
        if (runtimeCfg.enableDebugCamera) {
            servicesCfg.enableDebugCamera = *runtimeCfg.enableDebugCamera;
        }
        if (runtimeCfg.resizableWindow) {
            servicesCfg.resizableWindow = *runtimeCfg.resizableWindow;
        }
        if (runtimeCfg.windowWidth && *runtimeCfg.windowWidth > 0) {
            servicesCfg.windowWidth = *runtimeCfg.windowWidth;
        }
        if (runtimeCfg.windowHeight && *runtimeCfg.windowHeight > 0) {
            servicesCfg.windowHeight = *runtimeCfg.windowHeight;
        }
        if (runtimeCfg.startupSceneGuid) {
            servicesCfg.startupSceneGuid = *runtimeCfg.startupSceneGuid;
        }
        if (runtimeCfg.exportedSceneGuids) {
            servicesCfg.exportedSceneGuids = *runtimeCfg.exportedSceneGuids;
        }
    }

} // namespace HIKARI
