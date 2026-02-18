#include "HIKARI_MultiTextureAnimator.h"
#include "HIKARI_Texture.h"

#include <filesystem>
#include <algorithm>
#include <cctype>
#include <climits>

namespace fs = std::filesystem;

namespace {

    int ExtractLeadingNumber(const std::string& stem) {
        int value = 0;
        bool hasDigit = false;

        for (char c : stem) {
            if (std::isdigit(static_cast<unsigned char>(c)) != 0) {
                hasDigit = true;
                value = value * 10 + (c - '0');
            } else {
                break;
            }
        }

        if (!hasDigit) {
            return INT_MAX;
        }

        return value;
    }

} // anonymous namespace

namespace HIKARI {
    namespace ANIM {


        MultiTextureAnimator::MultiTextureAnimator() {
        }

        MultiTextureAnimator::MultiTextureAnimator(const MultiTexAnimDesc& desc) {
            Reset(desc);
        }

        void MultiTextureAnimator::Reset(const MultiTexAnimDesc& desc) {
            desc_ = desc;
            time_ = 0.0f;
            currentFrame_ = 0;
            playing_ = true;
            paused_ = false;
            finished_ = false;
        }

        void MultiTextureAnimator::Update(float deltaSeconds) {
            if (!playing_ || paused_) {
                return;
            }

            int totalFrames = static_cast<int>(desc_.textureNames.size());
            if (totalFrames <= 0) {
                return;
            }

            if (desc_.fps <= 0.0f) {
                return;
            }

            time_ += deltaSeconds;

            float frameFloat = time_ * desc_.fps;
            int newFrame = static_cast<int>(frameFloat);

            if (newFrame >= totalFrames) {
                if (desc_.loop) {
                    newFrame = newFrame % totalFrames;
                    finished_ = false;
                } else {
                    newFrame = totalFrames - 1;
                    playing_ = false;
                    finished_ = true;
                }
            }

            if (newFrame < 0) {
                newFrame = 0;
            }

            currentFrame_ = newFrame;
        }

        void MultiTextureAnimator::Draw(
            const HIKARI::Transform2D& t,
            HIKARI::RENDERER::CameraMode cam,
            unsigned int rgba) const
        {
            int totalFrames = static_cast<int>(desc_.textureNames.size());
            if (totalFrames <= 0) {
                return;
            }

            if (currentFrame_ < 0 || currentFrame_ >= totalFrames) {
                return;
            }

            const std::string& texName = desc_.textureNames[currentFrame_];

            float w = static_cast<float>(desc_.frameWidth);
            float h = static_cast<float>(desc_.frameHeight);

            if (w <= 0.0f || h <= 0.0f) {
                return;
            }

            HIKARI::RENDERER::DrawSprite(
                texName,
                t,
                w,
                h,
                cam,
                rgba
            );
        }

        void MultiTextureAnimator::Play() {
            time_ = 0.0f;
            currentFrame_ = 0;
            playing_ = true;
            paused_ = false;
            finished_ = false;
        }

        void MultiTextureAnimator::Stop() {
            time_ = 0.0f;
            currentFrame_ = 0;
            playing_ = false;
            paused_ = false;
            finished_ = false;
        }

        void MultiTextureAnimator::Pause() {
            paused_ = true;
        }

        void MultiTextureAnimator::Resume() {
            paused_ = false;
        }

        MultiTexAnimDesc LoadAnimationFolder(
            const std::string& groupName,
            const std::string& folderPath,
            int frameWidth,
            int frameHeight,
            float fps,
            bool loop)
        {
            MultiTexAnimDesc desc{};
            desc.frameWidth = frameWidth;
            desc.frameHeight = frameHeight;
            desc.fps = fps;
            desc.loop = loop;

            std::vector<fs::directory_entry> entries;

            for (const auto& entry : fs::directory_iterator(folderPath)) {
                if (!entry.is_regular_file()) {
                    continue;
                }

                const fs::path& path = entry.path();
                std::string ext = path.extension().string();

                std::transform(
                    ext.begin(), ext.end(), ext.begin(),
                    [](unsigned char c) { return static_cast<char>(::tolower(c)); }
                );

                if (ext == ".png" || ext == ".jpg" || ext == ".jpeg") {
                    entries.push_back(entry);
                }
            }
            std::sort(
                entries.begin(), entries.end(),
                [](const fs::directory_entry& a, const fs::directory_entry& b) {
                    std::string aStem = a.path().stem().string();
                    std::string bStem = b.path().stem().string();

                    int na = ExtractLeadingNumber(aStem);
                    int nb = ExtractLeadingNumber(bStem);

                    if (na != nb) {
                        return na < nb;
                    }

                    return a.path().filename().string() < b.path().filename().string();
                });

            int index = 0;
            for (const auto& e : entries) {

                const fs::path& path = e.path();
                std::string filePath = path.string();

                std::string texName = groupName + "_" + std::to_string(index);

                HIKARI::TEXTURE::Register(texName, filePath, groupName);

                desc.textureNames.push_back(texName);
                index++;
            }

            return desc;
        }

    } // namespace ANIM
} // namespace HIKARI
