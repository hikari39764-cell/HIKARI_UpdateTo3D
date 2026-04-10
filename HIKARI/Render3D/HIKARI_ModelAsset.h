#pragma once
#include <string>

namespace HIKARI {

    class ModelAsset {
    public:
        enum class State {
            Unloaded,
            Loaded,
            Failed
        };

        const std::string& GetName() const;
        const std::string& GetSourcePath() const;
        State GetState() const;

        void SetName(std::string name);
        void SetSourcePath(std::string path);
        void SetState(State state);

    private:
        std::string name_;
        std::string sourcePath_;
        State state_ = State::Unloaded;
    };

} // namespace HIKARI
