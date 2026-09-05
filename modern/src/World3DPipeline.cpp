#include "World3DPipeline.hpp"

#include "MeshGPUResources.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cstddef>
#include <utility>

namespace monopoly::engine
{
    std::optional<SDL_GPUTextureFormat> chooseWorld3DDepthFormat(
        SDL_GPUDevice* device) noexcept
    {
        if (!device) return std::nullopt;
        constexpr std::array candidates{
            SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
            SDL_GPU_TEXTUREFORMAT_D24_UNORM,
            SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT,
            SDL_GPU_TEXTUREFORMAT_D16_UNORM
        };
        for (const auto format : candidates)
        {
            if (SDL_GPUTextureSupportsFormat(device, format,
                    SDL_GPU_TEXTURETYPE_2D,
                    SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET))
                return format;
        }
        return std::nullopt;
    }

    World3DPipeline::~World3DPipeline()
    {
        reset();
    }
    World3DPipeline::World3DPipeline(World3DPipeline&& other) noexcept
    {
        *this = std::move(other);
    }

    World3DPipeline& World3DPipeline::operator=(World3DPipeline&& other) noexcept
    {
        if (this == &other) return *this;
        reset();
        device_ = std::exchange(other.device_, nullptr);
        pipeline_ = std::exchange(other.pipeline_, nullptr);
        depthFormat_ = std::exchange(other.depthFormat_, SDL_GPU_TEXTUREFORMAT_INVALID);
        shaders_ = std::move(other.shaders_);
        return *this;
    }

    void World3DPipeline::reset() noexcept
    {
        if (device_ && pipeline_)
            SDL_ReleaseGPUGraphicsPipeline(device_, pipeline_);
        pipeline_ = nullptr;
        shaders_.reset();
        depthFormat_ = SDL_GPU_TEXTUREFORMAT_INVALID;
        device_ = nullptr;
    }

    std::expected<World3DPipeline, World3DPipelineError> World3DPipeline::load(
        SDL_GPUDevice* device,
        const std::filesystem::path& shaderDirectory,
        SDL_GPUTextureFormat colorFormat)
    {
        if (!device)
            return std::unexpected(World3DPipelineError{
                World3DPipelineErrorCode::MissingDevice, "missing SDL GPU device", {}});
        if (colorFormat == SDL_GPU_TEXTUREFORMAT_INVALID)
            return std::unexpected(World3DPipelineError{
                World3DPipelineErrorCode::InvalidColorFormat,
                "invalid World3D color target format", {}});

        const auto depthFormat = chooseWorld3DDepthFormat(device);
        if (!depthFormat)
            return std::unexpected(World3DPipelineError{
                World3DPipelineErrorCode::UnsupportedDepthFormat,
                "device exposes no supported World3D depth target", {}});

        auto shaders = World3DShaderSet::load(device, shaderDirectory);
        if (!shaders)
            return std::unexpected(World3DPipelineError{
                World3DPipelineErrorCode::ShaderLoadFailed,
                shaders.error().detail,
                shaders.error()});

        static_assert(sizeof(MeshGPUVertex) == sizeof(float) * 8U);
        static_assert(offsetof(MeshGPUVertex, position) == 0U);
        static_assert(offsetof(MeshGPUVertex, normal) == sizeof(float) * 3U);
        static_assert(offsetof(MeshGPUVertex, uv) == sizeof(float) * 6U);

        const SDL_GPUVertexBufferDescription vertexBuffer{
            0U,
            static_cast<Uint32>(sizeof(MeshGPUVertex)),
            SDL_GPU_VERTEXINPUTRATE_VERTEX,
            0U
        };
        const std::array<SDL_GPUVertexAttribute, 3> attributes{{
            {0U, 0U, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
                static_cast<Uint32>(offsetof(MeshGPUVertex, position))},
            {1U, 0U, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
                static_cast<Uint32>(offsetof(MeshGPUVertex, normal))},
            {2U, 0U, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
                static_cast<Uint32>(offsetof(MeshGPUVertex, uv))}
        }};

        SDL_GPUColorTargetDescription colorTarget{};
        colorTarget.format = colorFormat;
        colorTarget.blend_state.enable_blend = false;
        colorTarget.blend_state.enable_color_write_mask = false;

        SDL_GPUGraphicsPipelineCreateInfo info{};
        info.vertex_shader = shaders->vertex();
        info.fragment_shader = shaders->fragment();
        info.vertex_input_state.vertex_buffer_descriptions = &vertexBuffer;
        info.vertex_input_state.num_vertex_buffers = 1U;
        info.vertex_input_state.vertex_attributes = attributes.data();
        info.vertex_input_state.num_vertex_attributes =
            static_cast<Uint32>(attributes.size());
        info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;

        info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        // No explicit D3DCULL state exists in the historical initialization.
        // Until the inherited D3D7 default is audited, do not invent culling.
        info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_CLOCKWISE;
        info.rasterizer_state.enable_depth_clip = true;
        info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;

        info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
        info.depth_stencil_state.enable_depth_test = true;
        info.depth_stencil_state.enable_depth_write = true;
        info.depth_stencil_state.enable_stencil_test = false;

        info.target_info.color_target_descriptions = &colorTarget;
        info.target_info.num_color_targets = 1U;
        info.target_info.depth_stencil_format = *depthFormat;
        info.target_info.has_depth_stencil_target = true;

        SDL_GPUGraphicsPipeline* pipeline =
            SDL_CreateGPUGraphicsPipeline(device, &info);
        if (!pipeline)
            return std::unexpected(World3DPipelineError{
                World3DPipelineErrorCode::PipelineCreateFailed,
                SDL_GetError(), {}});

        World3DPipeline result;
        result.device_ = device;
        result.pipeline_ = pipeline;
        result.depthFormat_ = *depthFormat;
        result.shaders_ = std::move(*shaders);
        return result;
    }
}
