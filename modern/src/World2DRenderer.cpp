#include "World2DRenderer.hpp"
#include "MeshGPUResources.hpp"
#include "LogicalViewport.hpp"
#include <SDL3/SDL.h>
#include <array>
#include <cstring>
#include <limits>
#include <set>
#include <cmath>

namespace monopoly::engine
{
    World2DRenderer::~World2DRenderer()
    {
        if (!device_) return;
        for (auto& [key, value] : textures_)
        { (void)key; SDL_ReleaseGPUTexture(device_, value.gpu); }
        if (quad_) SDL_ReleaseGPUBuffer(device_, quad_);
        if (sampler_) SDL_ReleaseGPUSampler(device_, sampler_);
        if (pipeline_) SDL_ReleaseGPUGraphicsPipeline(device_, pipeline_);
    }

    std::expected<std::unique_ptr<World2DRenderer>, std::string> World2DRenderer::load(
        SDL_GPUDevice* device, const std::filesystem::path& directory,
        SDL_GPUTextureFormat colorFormat)
    {
        if (!device || colorFormat == SDL_GPU_TEXTUREFORMAT_INVALID)
            return std::unexpected("invalid 2D GPU device or target format");
        auto shaders = World3DShaderSet::load(device, directory);
        if (!shaders) return std::unexpected(shaders.error().detail);
        auto result = std::unique_ptr<World2DRenderer>(new World2DRenderer);
        result->device_ = device;
        result->shaders_ = std::move(*shaders);
        // Reuse the existing texture shader with white material/ambient.
        // 2D owns a separate pipeline: no depth and straight-alpha blending.
        SDL_GPUVertexBufferDescription buffer{0, sizeof(MeshGPUVertex), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0};
        const std::array<SDL_GPUVertexAttribute,3> attributes{{
            {0,0,SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,0},
            {1,0,SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,12},
            {2,0,SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,24}}};
        SDL_GPUColorTargetDescription color{};
        color.format = colorFormat;
        color.blend_state.enable_blend = true;
        color.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        color.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        color.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
        color.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        color.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        color.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
        SDL_GPUGraphicsPipelineCreateInfo info{};
        info.vertex_shader = result->shaders_.vertex();
        info.fragment_shader = result->shaders_.fragment();
        info.vertex_input_state = {&buffer,1,attributes.data(),3};
        info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
        info.target_info.color_target_descriptions = &color;
        info.target_info.num_color_targets = 1;
        result->pipeline_ = SDL_CreateGPUGraphicsPipeline(device, &info);
        if (!result->pipeline_) return std::unexpected(std::string(SDL_GetError()));
        SDL_GPUSamplerCreateInfo sampler{};
        sampler.min_filter = sampler.mag_filter = SDL_GPU_FILTER_NEAREST;
        sampler.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        sampler.address_mode_u = sampler.address_mode_v = sampler.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        result->sampler_ = SDL_CreateGPUSampler(device, &sampler);
        if (!result->sampler_) return std::unexpected(std::string(SDL_GetError()));
        const std::array<MeshGPUVertex,6> vertices{{
            {{0,0,0},{0,0,1},{0,0}}, {{1,0,0},{0,0,1},{1,0}}, {{0,1,0},{0,0,1},{0,1}},
            {{0,1,0},{0,0,1},{0,1}}, {{1,0,0},{0,0,1},{1,0}}, {{1,1,0},{0,0,1},{1,1}}}};
        SDL_GPUBufferCreateInfo bi{}; bi.usage=SDL_GPU_BUFFERUSAGE_VERTEX; bi.size=sizeof(vertices);
        result->quad_=SDL_CreateGPUBuffer(device,&bi);
        if (!result->quad_) return std::unexpected(std::string(SDL_GetError()));
        SDL_GPUTransferBufferCreateInfo ti{}; ti.usage=SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD; ti.size=sizeof(vertices);
        auto* transfer=SDL_CreateGPUTransferBuffer(device,&ti);
        if (!transfer) return std::unexpected(std::string(SDL_GetError()));
        auto* mapped=SDL_MapGPUTransferBuffer(device,transfer,false);
        if (!mapped) { SDL_ReleaseGPUTransferBuffer(device,transfer); return std::unexpected(std::string(SDL_GetError())); }
        std::memcpy(mapped,vertices.data(),sizeof(vertices));
        SDL_UnmapGPUTransferBuffer(device,transfer);
        auto* command=SDL_AcquireGPUCommandBuffer(device);
        auto* copy=command ? SDL_BeginGPUCopyPass(command) : nullptr;
        if (!copy)
        {
            if (command) SDL_CancelGPUCommandBuffer(command);
            SDL_ReleaseGPUTransferBuffer(device,transfer);
            return std::unexpected(std::string(SDL_GetError()));
        }
        SDL_GPUTransferBufferLocation location{transfer,0};
        SDL_GPUBufferRegion region{result->quad_,0,sizeof(vertices)};
        SDL_UploadToGPUBuffer(copy,&location,&region,false);
        SDL_EndGPUCopyPass(copy);
        const bool submitted=SDL_SubmitGPUCommandBuffer(command);
        SDL_ReleaseGPUTransferBuffer(device,transfer);
        if (!submitted) return std::unexpected(std::string(SDL_GetError()));
        return result;
    }

    std::expected<SDL_GPUTexture*, std::string> World2DRenderer::resolveTexture(
        const std::shared_ptr<const data::BitmapRuntimeAsset>& asset)
    {
        if (!asset) return std::unexpected("missing bitmap asset");
        if (auto found=textures_.find(asset.get()); found!=textures_.end()) return found->second.gpu;
        const auto& image=asset->image;
        const auto size=static_cast<std::uint64_t>(image.width)*image.height*4U;
        if (!image.width || !image.height || size!=image.pixels.size() || size>std::numeric_limits<Uint32>::max())
            return std::unexpected("invalid RGBA8 texture extent or payload");
        SDL_GPUTextureCreateInfo ti{};
        ti.type=SDL_GPU_TEXTURETYPE_2D; ti.format=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        ti.usage=SDL_GPU_TEXTUREUSAGE_SAMPLER;
        ti.width=image.width; ti.height=image.height; ti.layer_count_or_depth=1; ti.num_levels=1;
        auto* texture=SDL_CreateGPUTexture(device_,&ti);
        if (!texture) return std::unexpected(std::string(SDL_GetError()));
        SDL_GPUTransferBufferCreateInfo bi{};
        bi.usage=SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD; bi.size=static_cast<Uint32>(size);
        auto* transfer=SDL_CreateGPUTransferBuffer(device_,&bi);
        if (!transfer) { SDL_ReleaseGPUTexture(device_,texture); return std::unexpected(std::string(SDL_GetError())); }
        auto* mapped=SDL_MapGPUTransferBuffer(device_,transfer,false);
        if (!mapped)
        {
            SDL_ReleaseGPUTransferBuffer(device_,transfer); SDL_ReleaseGPUTexture(device_,texture);
            return std::unexpected(std::string(SDL_GetError()));
        }
        std::memcpy(mapped,image.pixels.data(),image.pixels.size());
        SDL_UnmapGPUTransferBuffer(device_,transfer);
        auto* command=SDL_AcquireGPUCommandBuffer(device_);
        auto* copy=command ? SDL_BeginGPUCopyPass(command) : nullptr;
        if (!copy)
        {
            if (command) SDL_CancelGPUCommandBuffer(command);
            SDL_ReleaseGPUTransferBuffer(device_,transfer); SDL_ReleaseGPUTexture(device_,texture);
            return std::unexpected(std::string(SDL_GetError()));
        }
        SDL_GPUTextureTransferInfo source{transfer,0,image.width,image.height};
        SDL_GPUTextureRegion destination{};
        destination.texture=texture; destination.w=image.width; destination.h=image.height; destination.d=1;
        SDL_UploadToGPUTexture(copy,&source,&destination,false);
        SDL_EndGPUCopyPass(copy);
        const bool submitted=SDL_SubmitGPUCommandBuffer(command);
        SDL_ReleaseGPUTransferBuffer(device_,transfer);
        if (!submitted) { SDL_ReleaseGPUTexture(device_,texture); return std::unexpected(std::string(SDL_GetError())); }
        textures_.emplace(asset.get(),Texture{texture,asset});
        return texture;
    }

    std::expected<std::size_t, std::string> World2DRenderer::render(
        SDL_GPUCommandBuffer* command, SDL_GPUTexture* target,
        std::uint32_t width, std::uint32_t height, const SequenceWorld2DSlot& slot)
    {
        if (!command || !target || !width || !height ||
            width>std::numeric_limits<int>::max() || height>std::numeric_limits<int>::max())
            return std::unexpected("invalid 2D command, target or dimensions");
        const auto matrixFor = [](const SequenceWorld2DObject& object) {
            const auto& m=object.worldTransform.values;
            const auto w=static_cast<float>(object.asset->image.width);
            const auto h=static_cast<float>(object.asset->image.height);
            return std::array<float,16>{
                w*m[0]/400.0F,-w*m[1]/300.0F,0,0,
                h*m[3]/400.0F,-h*m[4]/300.0F,0,0,
                0,0,1,0,
                m[6]/400.0F-1.0F,1.0F-m[7]/300.0F,0,1};
        };
        std::set<const data::BitmapRuntimeAsset*> used;
        for (const auto node:slot.order())
        {
            const auto* object=slot.find(node);
            if (!object) return std::unexpected("missing 2D slot node");
            auto texture=resolveTexture(object->asset);
            if (!texture) return std::unexpected(texture.error());
            for (const auto value : matrixFor(*object))
                if (!std::isfinite(value)) return std::unexpected("2D projected matrix exceeds finite GPU range");
            used.insert(object->asset.get());
        }
        for (auto it=textures_.begin();it!=textures_.end();)
        {
            if (!used.contains(it->first))
            { SDL_ReleaseGPUTexture(device_,it->second.gpu); it=textures_.erase(it); }
            else ++it;
        }
        if (slot.size()==0) return 0;
        const auto fit=logicalviewport::makeTransform(static_cast<int>(width),static_cast<int>(height));
        const SDL_GPUViewport viewport{static_cast<float>(fit.offsetX),static_cast<float>(fit.offsetY),
            static_cast<float>(800.0*fit.scale),static_cast<float>(600.0*fit.scale),0,1};
        const SDL_Rect scissor{static_cast<int>(std::ceil(fit.offsetX)),static_cast<int>(std::ceil(fit.offsetY)),
            static_cast<int>(std::floor(fit.offsetX+800.0*fit.scale)-std::ceil(fit.offsetX)),
            static_cast<int>(std::floor(fit.offsetY+600.0*fit.scale)-std::ceil(fit.offsetY))};
        SDL_GPUColorTargetInfo color{}; color.texture=target;
        color.load_op=SDL_GPU_LOADOP_LOAD; color.store_op=SDL_GPU_STOREOP_STORE;
        auto* pass=SDL_BeginGPURenderPass(command,&color,1,nullptr);
        if (!pass) return std::unexpected(std::string(SDL_GetError()));
        SDL_BindGPUGraphicsPipeline(pass,pipeline_);
        SDL_SetGPUViewport(pass,&viewport); SDL_SetGPUScissor(pass,&scissor);
        SDL_GPUBufferBinding binding{quad_,0}; SDL_BindGPUVertexBuffers(pass,0,&binding,1);
        const std::array<float,8> white{1,1,1,1,1,1,1,1};
        SDL_PushGPUFragmentUniformData(command,0,white.data(),sizeof(white));
        for (const auto node:slot.order())
        {
            const auto& object=*slot.find(node);
            // Row-vector Matrix2D into the original 800x600 logical canvas.
            // DataBMP origin is (0,0); no dice-specific anchor is inserted.
            const auto matrix=matrixFor(object);
            SDL_PushGPUVertexUniformData(command,0,matrix.data(),sizeof(matrix));
            SDL_GPUTextureSamplerBinding sample{textures_.at(object.asset.get()).gpu,sampler_};
            SDL_BindGPUFragmentSamplers(pass,0,&sample,1);
            SDL_DrawGPUPrimitives(pass,6,1,0,0);
        }
        SDL_EndGPURenderPass(pass);
        return slot.size();
    }
}
