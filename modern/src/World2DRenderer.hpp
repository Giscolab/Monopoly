#pragma once
#include "SequenceWorld2DSlot.hpp"
#include "World3DShaderAssets.hpp"
#include <map>

namespace monopoly::engine
{
    class World2DRenderer final
    {
    public:
        ~World2DRenderer();
        World2DRenderer(const World2DRenderer&) = delete;
        World2DRenderer& operator=(const World2DRenderer&) = delete;
        [[nodiscard]] static std::expected<std::unique_ptr<World2DRenderer>, std::string>
            load(SDL_GPUDevice* device, const std::filesystem::path& shaders,
                SDL_GPUTextureFormat colorFormat);
        [[nodiscard]] std::expected<std::size_t, std::string> render(
            SDL_GPUCommandBuffer* command, SDL_GPUTexture* target,
            std::uint32_t width, std::uint32_t height, const SequenceWorld2DSlot& slot);
        [[nodiscard]] std::size_t textureCount() const noexcept { return textures_.size(); }
    private:
        World2DRenderer() = default;
        [[nodiscard]] std::expected<SDL_GPUTexture*, std::string> resolveTexture(
            const std::shared_ptr<const data::BitmapRuntimeAsset>& asset);
        struct Texture
        {
            SDL_GPUTexture* gpu{};
            std::shared_ptr<const data::BitmapRuntimeAsset> source;
        };
        SDL_GPUDevice* device_{};
        World3DShaderSet shaders_;
        SDL_GPUGraphicsPipeline* pipeline_{};
        SDL_GPUBuffer* quad_{};
        SDL_GPUSampler* sampler_{};
        std::map<const data::BitmapRuntimeAsset*, Texture> textures_;
    };
}
