#include "ModernEnvironment.hpp"
#include "ModernGltfMesh.hpp"
#include "SequenceTransforms.hpp"
#include "World3DRenderer.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

// Presentation qualification only: no retail resource substitute, game state,
// sequencer simulation, or alternate renderer. All geometry is production GLB.
namespace
{
    using namespace monopoly;
    constexpr Uint32 Width = 1920, Height = 1080, Frames = 100;
    constexpr std::array<unsigned char, 3> Background{12, 16, 22};

    void require(bool condition, const std::string& message)
    {
        if (!condition) throw std::runtime_error(message + ": " + SDL_GetError());
    }

    struct SDLSession
    {
        SDLSession() { require(SDL_Init(SDL_INIT_VIDEO), "SDL_Init"); }
        ~SDLSession() { SDL_Quit(); }
    };

    struct GPUResources
    {
        SDL_GPUDevice* device{};
        SDL_GPUTexture* target{};
        SDL_GPUTransferBuffer* transfer{};
        ~GPUResources()
        {
            if (!device) return;
            (void)SDL_WaitForGPUIdle(device);
            if (transfer) SDL_ReleaseGPUTransferBuffer(device, transfer);
            if (target) SDL_ReleaseGPUTexture(device, target);
            SDL_DestroyGPUDevice(device);
        }
    };

    void submitAndWait(SDL_GPUDevice* device, SDL_GPUCommandBuffer* command)
    {
        auto* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(command);
        require(fence != nullptr, "GPU submit");
        SDL_GPUFence* fences[]{fence};
        const bool waited = SDL_WaitForGPUFences(device, true, fences, 1);
        SDL_ReleaseGPUFence(device, fence);
        require(waited, "GPU fence wait");
    }

    engine::World3DRenderStats draw(GPUResources& gpu,
        engine::World3DRenderer& renderer, const engine::SequenceWorld3DSlot& slot,
        bool readback)
    {
        auto* command = SDL_AcquireGPUCommandBuffer(gpu.device);
        require(command != nullptr, "GPU command acquisition");
        SDL_GPUColorTargetInfo color{};
        color.texture = gpu.target;
        color.clear_color = {Background[0] / 255.0F, Background[1] / 255.0F,
            Background[2] / 255.0F, 1.0F};
        color.load_op = SDL_GPU_LOADOP_CLEAR;
        color.store_op = SDL_GPU_STOREOP_STORE;
        color.cycle = true;
        auto* pass = SDL_BeginGPURenderPass(command, &color, 1, nullptr);
        if (!pass)
        {
            SDL_CancelGPUCommandBuffer(command);
            require(false, "GPU clear pass");
        }
        SDL_EndGPURenderPass(pass);
        const SDL_GPUViewport viewport{0, 0, static_cast<float>(Width),
            static_cast<float>(Height), 0, 1};
        auto stats = renderer.render(command, gpu.target, Width, Height, viewport, slot);
        if (!stats)
        {
            SDL_CancelGPUCommandBuffer(command);
            throw std::runtime_error("Production renderer: " + stats.error().detail);
        }
        if (readback)
        {
            auto* copy = SDL_BeginGPUCopyPass(command);
            if (!copy)
            {
                SDL_CancelGPUCommandBuffer(command);
                require(false, "GPU download pass");
            }
            SDL_GPUTextureRegion source{};
            source.texture = gpu.target;
            source.w = Width; source.h = Height; source.d = 1;
            SDL_GPUTextureTransferInfo destination{};
            destination.transfer_buffer = gpu.transfer;
            destination.pixels_per_row = Width;
            destination.rows_per_layer = Height;
            SDL_DownloadFromGPUTexture(copy, &source, &destination);
            SDL_EndGPUCopyPass(copy);
        }
        submitAndWait(gpu.device, command);
        return *stats;
    }

    void reportMaterials(const data::MeshRenderData& mesh, const std::string& name)
    {
        std::array<std::size_t, 5> maps{};
        std::size_t masked = 0;
        for (const auto& batch : mesh.batches)
        {
            const auto& m = batch.material;
            maps[0] += m.baseColorTexture.has_value();
            maps[1] += m.metallicRoughnessTexture.has_value();
            maps[2] += m.normalTexture.has_value();
            maps[3] += m.emissiveTexture.has_value();
            maps[4] += m.occlusionTexture.has_value();
            masked += m.alphaMode == data::ModernAlphaMode::Mask;
        }
        std::cout << "asset\t" << name << "\tvertices\t" << mesh.vertices.size()
            << "\ttriangles\t" << mesh.indices.size() / 3 << "\tbatches\t"
            << mesh.batches.size() << "\tmask_batches\t" << masked << '\n';
        std::cout << "map_bindings_base_mr_normal_emissive_occlusion\t";
        for (auto count : maps) std::cout << count << '\t';
        std::cout << '\n';
    }
}

int main(int argc, char** argv)
{
    try
    {
        if (argc < 4 || argc > 6)
            throw std::runtime_error("Usage: ModernSceneRenderProbe <modern-assets-root> "
                "<shader-directory> <existing-build-output-directory> "
                "[board/paris_board_runtime.glb] [--environment]");
        const std::filesystem::path assetRoot{argv[1]}, shaders{argv[2]};
        const auto outputDir = std::filesystem::canonical(argv[3]);
        require(std::filesystem::is_directory(outputDir), "Output directory must exist");
        // Never write captures to the locked retail reference corpus.
        for (const auto& part : outputDir)
        {
            auto text = part.string();
            std::transform(text.begin(), text.end(), text.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            require(text != "source", "Output directory must be outside Source");
        }
        std::filesystem::path relativeBoard{"board/paris_board_runtime.glb"};
        bool includeEnvironment = false, boardArgument = false;
        for (int i = 4; i < argc; ++i)
        {
            if (std::string{argv[i]} == "--environment") includeEnvironment = true;
            else if (!boardArgument) { relativeBoard = argv[i]; boardArgument = true; }
            else throw std::runtime_error("Unexpected argument");
        }
        require(!relativeBoard.is_absolute(), "Board asset path must be relative");
        for (const auto& part : relativeBoard)
            require(part != "..", "Board asset path must remain under assets root");
        data::ModernGltfLoadOptions options;
        options.unitsPerMeter = 200;
        options.localOffset = {2430, 0, 2430};
        options.groundToZero = false;
        auto loaded = data::loadModernGltfMesh(assetRoot / relativeBoard, options);
        if (!loaded) throw std::runtime_error("Production GLB loader: " + loaded.error().detail);
        auto asset = std::make_shared<data::MeshRuntimeAsset>();
        asset->dataId = data::packDataId(0xFFFC, 1);
        asset->origin = data::MeshAssetOrigin::ModernGltf;
        asset->renderData = *loaded;
        const auto boardMatrix = sequence::moveRySTxzTransform(0, 0.10F, 0, 0);
        sequence::SequenceMeshRenderItem board;
        board.node = 1; board.contentsDataId = asset->dataId;
        board.worldTransform = boardMatrix; board.asset = asset; board.renderData = *loaded;
        std::vector<sequence::SequenceMeshRenderItem> items{board};
        engine::ModernEnvironment environment(assetRoot, includeEnvironment);
        auto decorations = environment.items(boardMatrix, 0);
        items.insert(items.end(), decorations.begin(), decorations.end());
        std::cout << "scope\tpresentation-only offscreen production renderer; no retail DAT or gameplay\n"
            << "environment_requested\t" << includeEnvironment
            << "\tenvironment_loaded\t" << decorations.size() << '\n';
        if (includeEnvironment && decorations.size() != engine::ModernEnvironmentCount)
            throw std::runtime_error("Requested environment is incomplete; see loader diagnostics");
        for (const auto& item : items)
            reportMaterials(*item.renderData, std::to_string(item.contentsDataId));

        engine::SequenceWorld3DSlot slot;
        require(slot.sync(items).has_value(), "Production scene slot sync");
        const auto& bounds = (*loaded)->bounds;
        const std::array<float, 3> center{
            (bounds.minimum[0] + bounds.maximum[0]) * .05F,
            (bounds.minimum[1] + bounds.maximum[1]) * .05F,
            (bounds.minimum[2] + bounds.maximum[2]) * .05F};
        engine::World3DCamera camera;
        camera.location = {650, 650, -500};
        for (std::size_t axis = 0; axis < 3; ++axis)
            camera.forward[axis] = center[axis] - camera.location[axis];
        camera.up = {0, 1, 0}; camera.fieldOfView = .7853981633974483F;
        camera.nearPlane = 10; camera.farPlane = 2500;
        // The production projection uses horizontal FOV and multiplies Y by
        // viewport aspect. Fit all eight measured board corners in that basis.
        auto forward = camera.forward;
        const float distance = std::sqrt(forward[0]*forward[0] +
            forward[1]*forward[1] + forward[2]*forward[2]);
        for (auto& value : forward) value /= distance;
        std::array<float, 3> right{forward[2], 0, -forward[0]};
        const float rightLength = std::sqrt(right[0]*right[0] + right[2]*right[2]);
        for (auto& value : right) value /= rightLength;
        const std::array<float, 3> up{forward[1]*right[2],
            forward[2]*right[0] - forward[0]*right[2], -forward[1]*right[0]};
        const auto dot = [](const auto& a, const auto& b)
            { return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]; };
        float requiredTan = 0;
        for (unsigned corner = 0; corner < 8; ++corner)
        {
            std::array<float, 3> relative{};
            for (unsigned axis = 0; axis < 3; ++axis)
                relative[axis] = ((corner & (1U << axis)) ? bounds.maximum[axis] :
                    bounds.minimum[axis]) * .1F - camera.location[axis];
            const float depth = dot(relative, forward);
            require(depth > camera.nearPlane, "Measured board must be in front of camera");
            requiredTan = std::max(requiredTan, std::max(std::abs(dot(relative, right)),
                std::abs(dot(relative, up)) * static_cast<float>(Width) / Height) / depth);
        }
        camera.fieldOfView = 2 * std::atan(requiredTan * 1.08F);
        require(slot.configureView({0, 0, static_cast<int>(Width), static_cast<int>(Height)},
            camera).has_value(), "Production camera configuration");
        std::cout << "camera_target\t" << center[0] << ',' << center[1] << ',' << center[2]
            << "\tcamera_location\t650,650,-500\thorizontal_fov_radians\t"
            << camera.fieldOfView << '\n';
        const auto projected = engine::world3DMeshScreenRect(bounds, boardMatrix, *slot.view());
        if (projected)
            std::cout << "projected_board_rect\t" << projected->left << ',' << projected->top
                << ',' << projected->right << ',' << projected->bottom << '\n';

        SDLSession session;
        GPUResources gpu;
        gpu.device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_SPIRV |
            SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_METALLIB, false, nullptr);
        require(gpu.device != nullptr, "Real SDL_GPU device");
        std::cout << "backend\t" << SDL_GetGPUDeviceDriver(gpu.device) << '\n';
        SDL_GPUTextureCreateInfo texture{};
        texture.type = SDL_GPU_TEXTURETYPE_2D;
        texture.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        texture.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
        texture.width = Width; texture.height = Height;
        texture.layer_count_or_depth = 1; texture.num_levels = 1;
        texture.sample_count = SDL_GPU_SAMPLECOUNT_1;
        gpu.target = SDL_CreateGPUTexture(gpu.device, &texture);
        require(gpu.target != nullptr, "Offscreen target");
        SDL_GPUTransferBufferCreateInfo transfer{};
        transfer.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
        transfer.size = Width * Height * 4;
        gpu.transfer = SDL_CreateGPUTransferBuffer(gpu.device, &transfer);
        require(gpu.transfer != nullptr, "Readback transfer");
        // Renderer is declared after GPUResources so its resources release first.
        auto renderer = engine::World3DRenderer::load(gpu.device, shaders, texture.format);
        if (!renderer) throw std::runtime_error("Renderer shaders: " + renderer.error().detail);
        renderer->setBilinearFiltering(true);
        engine::World3DLighting lighting;
        lighting.ambient = {.53F, .53F, .53F};
        lighting.sun.enabled = true;
        lighting.sun.color = {.7F, .7F, .7F};
        lighting.sun.direction = {.3F, -1, .4F};
        renderer->setLighting(lighting);
        for (unsigned i = 0; i < 3; ++i) (void)draw(gpu, *renderer, slot, false);
        const auto start = std::chrono::steady_clock::now();
        engine::World3DRenderStats stats;
        for (unsigned i = 0; i < Frames; ++i) stats = draw(gpu, *renderer, slot, false);
        const double seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - start).count();
        (void)draw(gpu, *renderer, slot, true);
        auto* mapped = SDL_MapGPUTransferBuffer(gpu.device, gpu.transfer, false);
        require(mapped != nullptr, "GPU readback map");
        std::vector<unsigned char> pixels(Width * Height * 4);
        std::memcpy(pixels.data(), mapped, pixels.size());
        SDL_UnmapGPUTransferBuffer(gpu.device, gpu.transfer);
        const auto output = outputDir / "modern-scene-probe.ppm";
        std::ofstream image(output, std::ios::binary);
        image << "P6\n" << Width << ' ' << Height << "\n255\n";
        std::size_t foreground = 0, colored = 0, opaque = 0;
        for (std::size_t i = 0; i < pixels.size(); i += 4)
        {
            image.write(reinterpret_cast<const char*>(pixels.data() + i), 3);
            const int r = pixels[i], g = pixels[i + 1], b = pixels[i + 2];
            const bool different = std::abs(r - Background[0]) > 2 ||
                std::abs(g - Background[1]) > 2 || std::abs(b - Background[2]) > 2;
            foreground += different;
            colored += different && std::max({r,g,b}) - std::min({r,g,b}) > 16;
            opaque += pixels[i + 3] == 255;
        }
        image.close();
        require(image.good(), "PPM capture write");
        require(foreground > 0 && stats.triangles > 0, "Actual rendered scene pixels");
        std::cout << "capture\t" << output.string() << "\nsize\t" << Width << 'x' << Height
            << "\nforeground_pixels\t" << foreground << "\ncolored_foreground_pixels\t"
            << colored << "\nopaque_pixels\t" << opaque << "\nrendered_objects\t"
            << stats.objects << "\nrendered_batches\t" << stats.batches
            << "\nrendered_triangles\t" << stats.triangles << "\nmeasured_frames\t" << Frames
            << "\nfenced_offscreen_seconds\t" << seconds << "\nfenced_offscreen_fps\t"
            << Frames / seconds << "\nmeasurement_scope\t100 serial GPU-fenced frames after 3 warmups; "
                "excludes GLB load, shader setup and readback; not a full-game benchmark\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "ModernSceneRenderProbe failed: " << error.what() << '\n';
        return 1;
    }
}
