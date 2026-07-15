#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <json.hpp>

namespace HIKARI {

class ISystem;

struct SystemTypeInfo {
    using FactoryFn = std::function<std::unique_ptr<ISystem>(const nlohmann::json& settings)>;

    std::string systemId{};
    FactoryFn factory{};
};

class SystemTypeRegistry {
public:
    void Register(SystemTypeInfo info);
    const SystemTypeInfo* Find(std::string_view systemId) const;
    std::unique_ptr<ISystem> Create(std::string_view systemId, const nlohmann::json& settings) const;
    std::vector<std::string> GetTypeNames() const;

private:
    std::unordered_map<std::string, SystemTypeInfo> byName_{};
};

} // namespace HIKARI
