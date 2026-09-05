#include "World3DShaderAssets.hpp"

#include <SDL3/SDL.h>

#include <fstream>
#include <iterator>
#include <utility>
#include <vector>

namespace monopoly::engine
{
    namespace
    {
        struct ShaderAssetChoice
        {
            SDL_GPUShaderFormat format{};
            const char* suffix{};
            const char* entrypoint{};
            bool text{};
        };

        std::expected<ShaderAssetChoice, World3DShaderError>
        chooseFormat(SDL_GPUDevice* device)
        {
            if (!device)
                return std::unexpected(World3DShaderError{
                    World3DShaderErrorCode::MissingDevice, {}, "missing SDL GPU device"});

            const auto formats = SDL_GetGPUShaderFormats(device);
            if (formats & SDL_GPU_SHADERFORMAT_DXIL)
                return ShaderAssetChoice{SDL_GPU_SHADERFORMAT_DXIL, ".dxil", "main", false};
            if (formats & SDL_GPU_SHADERFORMAT_MSL)
                return ShaderAssetChoice{SDL_GPU_SHADERFORMAT_MSL, ".msl", "main0", true};
            if (formats & SDL_GPU_SHADERFORMAT_SPIRV)
                return std::unexpected(World3DShaderError{
                    World3DShaderErrorCode::UnsupportedFormat, {},
                    "SPIR-V World3D texture shader has not been regenerated; refusing the stale untextured asset"});

            return std::unexpected(World3DShaderError{
                World3DShaderErrorCode::UnsupportedFormat, {},
                "device exposes no current generated World3D shader format"});
        }

        std::expected<std::vector<Uint8>, World3DShaderError>
        readShader(const std::filesystem::path& path, bool text)
        {
            if (!std::filesystem::exists(path))
                return std::unexpected(World3DShaderError{
                    World3DShaderErrorCode::MissingAsset, path, "shader asset does not exist"});

            std::ifstream stream(path, std::ios::binary);
            if (!stream)
                return std::unexpected(World3DShaderError{
                    World3DShaderErrorCode::ReadFailed, path, "shader asset could not be opened"});

            std::vector<Uint8> bytes{
                std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
            if (!stream.eof() && stream.fail())
                return std::unexpected(World3DShaderError{
                    World3DShaderErrorCode::ReadFailed, path, "shader asset read failed"});
            if (text) bytes.push_back(0);
            return bytes;
        }
        std::expected<SDL_GPUShader*, World3DShaderError> createShader(
            SDL_GPUDevice* device,
            const std::filesystem::path& path,
            const ShaderAssetChoice& choice,
            SDL_GPUShaderStage stage)
        {
            auto bytes = readShader(path, choice.text);
            if (!bytes) return std::unexpected(bytes.error());

            SDL_GPUShaderCreateInfo info{};
            info.code_size = choice.text ? bytes->size() - 1U : bytes->size();
            info.code = bytes->data();
            info.entrypoint = choice.entrypoint;
            info.format = choice.format;
            info.stage = stage;
            info.num_uniform_buffers = 1;
            info.num_samplers = stage == SDL_GPU_SHADERSTAGE_FRAGMENT ? 1U : 0U;

            SDL_GPUShader* shader = SDL_CreateGPUShader(device, &info);
            if (!shader)
                return std::unexpected(World3DShaderError{
                    World3DShaderErrorCode::CreateFailed, path, SDL_GetError()});
            return shader;
        }
    }

    World3DShaderSet::~World3DShaderSet()
    {
        reset();
    }

    World3DShaderSet::World3DShaderSet(World3DShaderSet&& other) noexcept
    {
        *this = std::move(other);
    }
    World3DShaderSet& World3DShaderSet::operator=(World3DShaderSet&& other) noexcept
    {
        if (this == &other) return *this;
        reset();
        device_ = std::exchange(other.device_, nullptr);
        vertex_ = std::exchange(other.vertex_, nullptr);
        fragment_ = std::exchange(other.fragment_, nullptr);
        format_ = std::exchange(other.format_, SDL_GPU_SHADERFORMAT_INVALID);
        entrypoint_ = std::exchange(other.entrypoint_, "main");
        return *this;
    }

    void World3DShaderSet::reset() noexcept
    {
        if (device_ && fragment_) SDL_ReleaseGPUShader(device_, fragment_);
        if (device_ && vertex_) SDL_ReleaseGPUShader(device_, vertex_);
        fragment_ = nullptr;
        vertex_ = nullptr;
        device_ = nullptr;
        format_ = SDL_GPU_SHADERFORMAT_INVALID;
        entrypoint_ = "main";
    }

    std::expected<World3DShaderSet, World3DShaderError> World3DShaderSet::load(
        SDL_GPUDevice* device, const std::filesystem::path& shaderDirectory)
    {
        auto choice = chooseFormat(device);
        if (!choice) return std::unexpected(choice.error());

        const auto vertexPath = shaderDirectory / (std::string("World3D.vert") + choice->suffix);
        const auto fragmentPath = shaderDirectory / (std::string("World3D.frag") + choice->suffix);
        auto vertex = createShader(device, vertexPath, *choice, SDL_GPU_SHADERSTAGE_VERTEX);
        if (!vertex) return std::unexpected(vertex.error());

        auto fragment = createShader(device, fragmentPath, *choice, SDL_GPU_SHADERSTAGE_FRAGMENT);
        if (!fragment)
        {
            SDL_ReleaseGPUShader(device, *vertex);
            return std::unexpected(fragment.error());
        }

        World3DShaderSet result;
        result.device_ = device;
        result.vertex_ = *vertex;
        result.fragment_ = *fragment;
        result.format_ = choice->format;
        result.entrypoint_ = choice->entrypoint;
        return result;
    }
}
