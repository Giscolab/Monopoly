#include "ModernEnvironment.hpp"
#include "ModernScenePresentation.hpp"
#include "PiecePlacement.hpp"
#include "ModernSceneCatalog.hpp"
#include "ModernGltfMesh.hpp"
#include "ModernTokenCatalog.hpp"
#include "ModernTokenVariants.hpp"
#include "ResourcePaths.hpp"
#include "ResourceRuntime.hpp"
#include "SequenceRenderData.hpp"
#include "SequenceRuntime.hpp"
#include "SequenceTransforms.hpp"
#include "World3DRenderer.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// Presentation qualification only: no retail resource substitute, game state,
// sequencer simulation, or alternate renderer. Token frames run decoded CNK.
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
        Uint32 width{Width}, height{Height};
        bool transparent{};
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
        if (gpu.transparent) color.clear_color = {0,0,0,0};
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
        const SDL_GPUViewport viewport{0, 0, static_cast<float>(gpu.width),
            static_cast<float>(gpu.height), 0, 1};
        auto stats = renderer.render(command, gpu.target, gpu.width, gpu.height, viewport, slot);
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
            source.w = gpu.width; source.h = gpu.height; source.d = 1;
            SDL_GPUTextureTransferInfo destination{};
            destination.transfer_buffer = gpu.transfer;
            destination.pixels_per_row = gpu.width;
            destination.rows_per_layer = gpu.height;
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

    std::uint32_t number(const char* text, std::uint32_t maximum)
    {
        char* end{};
        const auto value = std::strtoull(text, &end, 0);
        if (!*text || *text == '-' || *end || value > maximum)
            throw std::runtime_error("Invalid numeric probe argument: " + std::string{text});
        return static_cast<std::uint32_t>(value);
    }

    struct TokenFrame
    {
        std::filesystem::path runtimeRoot;
        data::DataId sequenceId{};
        std::uint32_t tick{};
        std::uint16_t priority{};
    };

    std::vector<sequence::SequenceMeshRenderItem> loadTokenFrame(
        const std::filesystem::path& assetRoot, const TokenFrame& request)
    {
        const std::array roots{std::filesystem::absolute(request.runtimeRoot)};
        auto paths = data::ResourcePaths::create(roots);
        if (!paths) throw std::runtime_error(paths.error().detail);
        data::ResourceRuntime resources;
        auto initialized = resources.initialize(*paths);
        if (!initialized) throw std::runtime_error(initialized.error().detail);
        auto snapshot = resources.snapshot();
        auto program = sequence::SequenceProgram::load(snapshot, request.sequenceId,
            0, {32, 1024, 8192});
        if (!program) throw std::runtime_error(program.error().detail);
        data::ModernTokenVariantCache variants(assetRoot);
        std::array<bool, data::ModernTokenCount> attempted{};
        std::array<std::shared_ptr<const data::MeshRenderData>, data::ModernTokenCount> rigid;
        data::ModernMeshResolver resolver = [&](data::DataId id,
            std::optional<data::DataId> rootId, std::uint16_t priority)
            -> std::expected<std::optional<std::shared_ptr<const data::MeshRenderData>>,
                data::MeshRuntimeError>
        {
            if (data::qualifiedModernTokenVariantSequence(id, rootId, priority))
            {
                if (auto mesh = variants.resolve(id, rootId, priority))
                    return std::optional{std::move(mesh)};
                return std::optional<std::shared_ptr<const data::MeshRenderData>>{};
            }
            if (!data::qualifiedModernTokenSequence(id, rootId, priority))
                return std::optional<std::shared_ptr<const data::MeshRenderData>>{};
            const auto* definition = data::modernTokenForLegacyMesh(id);
            if (!definition || definition->token >= rigid.size())
                return std::optional<std::shared_ptr<const data::MeshRenderData>>{};
            const auto token = definition->token;
            if (!attempted[token])
            {
                attempted[token] = true;
                data::ModernGltfLoadOptions options;
                options.unitsPerMeter = definition->unitsPerMeter;
                options.yawDegrees = definition->yawDegrees;
                options.localOffset = definition->localOffset;
                options.groundToZero = true;
                auto mesh = data::loadModernGltfMesh(assetRoot /
                    std::filesystem::path{definition->relativeGlbPath}.lexically_relative("assets/modern"), options);
                if (mesh) rigid[token] = *mesh;
                else std::cerr << "modern rigid fallback: " << mesh.error().detail << '\n';
            }
            if (rigid[token]) return std::optional{rigid[token]};
            return std::optional<std::shared_ptr<const data::MeshRenderData>>{};
        };
        // Complete packs prepare transactionally before the requested frame.
        for (const auto& description : (*program)->descriptions())
            if (description.contentsDataId && data::tokenForLegacyMesh(*description.contentsDataId))
            {
                auto prepared = resolver(*description.contentsDataId,
                    request.sequenceId, request.priority);
                if (!prepared) throw std::runtime_error(prepared.error().detail);
            }
        if (variants.loadError(request.sequenceId))
            std::cerr << "modern variant fallback: " << variants.loadError(request.sequenceId)->detail << '\n';
        data::MeshRuntimeCache meshes(snapshot, data::MeshTextureResolver{},
            data::MeshRuntimeLimits{}, std::move(resolver));
        sequence::SequenceRuntime runtime({4096, 8192});
        auto root = runtime.start(*program, request.priority);
        if (!root) throw std::runtime_error(root.error().detail);
        // Preserve disk ending actions and each 60Hz parent-clock transition.
        for (std::uint32_t tick = 0; tick <= request.tick; ++tick)
        {
            auto updated = runtime.update(static_cast<std::int32_t>(tick));
            if (!updated) throw std::runtime_error(updated.error().detail);
        }
        auto collected = sequence::collectSequenceMeshRenderData(runtime, meshes);
        if (!collected) throw std::runtime_error(collected.error().cause.detail);
        if (collected->empty()) throw std::runtime_error("Requested CNK frame has no live mesh items");
        std::cout << "scope\tpresentation-only actual decoded CNK frame; no gameplay\n"
            << "sequence_id\t" << request.sequenceId << "\troot_node\t" << *root
            << "\trequested_tick\t" << request.tick << "\troot_priority\t" << request.priority << '\n';
        for (const auto& item : *collected)
        {
            std::cout << "cnk_item\tnode\t" << item.node << "\thmd\t" << item.contentsDataId
                << "\troot_id\t" << item.rootSequenceDataId << "\tleaf_priority\t" << item.priority
                << "\tclock\t" << item.clock << "\torigin\t"
                << (item.asset->origin == data::MeshAssetOrigin::ModernGltf ? "ModernGltf" : "LegacyHmd")
                << "\tworld_matrix\t";
            for (const auto value : item.worldTransform.values) std::cout << value << ',';
            std::cout << '\n';
        }
        return std::move(*collected);
    }

    data::MeshBounds sceneBounds(const std::vector<sequence::SequenceMeshRenderItem>& items)
    {
        data::MeshBounds result;
        result.minimum.fill(std::numeric_limits<float>::infinity());
        result.maximum.fill(-std::numeric_limits<float>::infinity());
        for (const auto& item : items)
        {
            const auto& mesh = item.renderData ? item.renderData : item.asset->renderData;
            const auto& m = item.worldTransform.values;
            for (unsigned corner = 0; corner < 8; ++corner)
            {
                std::array<float, 3> p{};
                for (unsigned axis = 0; axis < 3; ++axis)
                    p[axis] = (corner & (1U << axis)) ? mesh->bounds.maximum[axis] : mesh->bounds.minimum[axis];
                for (unsigned axis = 0; axis < 3; ++axis)
                {
                    const float value = p[0]*m[axis] + p[1]*m[4+axis] + p[2]*m[8+axis] + m[12+axis];
                    require(std::isfinite(value), "Finite transformed scene bounds");
                    result.minimum[axis] = std::min(result.minimum[axis], value);
                    result.maximum[axis] = std::max(result.maximum[axis], value);
                }
            }
        }
        return result;
    }

    void tokenTurntable(const std::filesystem::path& assetRoot,
        const std::filesystem::path& shaders, const std::filesystem::path& output,
        const std::string& requested)
    {
        const data::ModernTokenDefinition* definition=nullptr;
        for(const auto& entry:data::modernTokenDefinitions())
            if(entry.slug==requested || std::to_string(entry.token)==requested) definition=&entry;
        if(!definition) throw std::runtime_error("Unknown token catalog slug/index: "+requested);
        data::ModernGltfLoadOptions options;
        options.unitsPerMeter=definition->unitsPerMeter; options.yawDegrees=definition->yawDegrees;
        options.localOffset=definition->localOffset; options.groundToZero=true;
        auto loaded=data::loadModernGltfMesh(assetRoot/std::filesystem::path{definition->relativeGlbPath}.lexically_relative("assets/modern"),options);
        if(!loaded) throw std::runtime_error(loaded.error().detail);
        reportMaterials(**loaded,std::string{definition->slug});
        auto asset=std::make_shared<data::MeshRuntimeAsset>();
        asset->dataId=data::representativeLegacyMesh(definition->token);
        asset->origin=data::MeshAssetOrigin::ModernGltf; asset->renderData=*loaded;
        const auto& native=(*loaded)->bounds;
        const float cx=(native.minimum[0]+native.maximum[0])*.5F;
        const float cz=(native.minimum[2]+native.maximum[2])*.5F;
        std::vector<sequence::SequenceMeshRenderItem> poses;
        for(unsigned frame=0;frame<28;++frame)
        {
            sequence::SequenceMeshRenderItem item;
            item.node=frame+1;item.asset=asset;item.renderData=*loaded;item.contentsDataId=asset->dataId;
            item.worldTransform=sequence::multiply(sequence::translate3D(-cx,0,-cz),
                sequence::moveRySTxzTransform(6.283185307179586F*frame/28,1,cx,cz));
            poses.push_back(item);
        }
        const auto bounds=sceneBounds(poses);
        std::array<float,3> center{};float r2=0;
        for(unsigned axis=0;axis<3;++axis)
        {center[axis]=(bounds.minimum[axis]+bounds.maximum[axis])*.5F;r2+=std::pow((bounds.maximum[axis]-bounds.minimum[axis])*.5F,2);}
        const float radius=std::max(.01F,std::sqrt(r2));
        engine::World3DCamera camera;
        camera.location={center[0]+radius*2.1F,center[1]+radius*.9F,center[2]-radius*2.1F};
        camera.up={0,1,0};camera.nearPlane=radius*.01F;camera.farPlane=radius*8;
        std::array<float,3> forward{};float distance2=0;
        for(unsigned axis=0;axis<3;++axis) {camera.forward[axis]=center[axis]-camera.location[axis];distance2+=camera.forward[axis]*camera.forward[axis];}
        for(unsigned axis=0;axis<3;++axis) forward[axis]=camera.forward[axis]/std::sqrt(distance2);
        std::array<float,3> right{forward[2],0,-forward[0]};
        const float rl=std::sqrt(right[0]*right[0]+right[2]*right[2]);for(auto& value:right)value/=rl;
        const std::array<float,3> up{forward[1]*right[2],forward[2]*right[0]-forward[0]*right[2],-forward[1]*right[0]};
        auto dot=[](const auto& x,const auto& y){return x[0]*y[0]+x[1]*y[1]+x[2]*y[2];};
        // Fit actual vertices over every yaw, not empty corners of the union
        // AABB. One FOV/camera for all frames prevents scale breathing.
        float fit=0;
        for(const auto& pose:poses)
            for(const auto& vertex:(*loaded)->vertices)
            {
                const auto& m=pose.worldTransform.values;
                const auto& p=vertex.position;
                std::array<float,3> relative{};
                for(unsigned axis=0;axis<3;++axis)
                    relative[axis]=p[0]*m[axis]+p[1]*m[4+axis]+p[2]*m[8+axis]+m[12+axis]-camera.location[axis];
                const float depth=dot(relative,forward);
                require(std::isfinite(depth) && depth>camera.nearPlane && depth<camera.farPlane,"Turntable vertex fit depth");
                fit=std::max(fit,std::max(std::abs(dot(relative,right)),std::abs(dot(relative,up))*768/640)/depth);
            }
        require(std::isfinite(fit) && fit>0,"Turntable nonempty projected vertices");
        camera.fieldOfView=2*std::atan(fit*1.12F);
        SDLSession session;GPUResources gpu;gpu.width=768;gpu.height=640;gpu.transparent=true;
        gpu.device=SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_DXIL|SDL_GPU_SHADERFORMAT_SPIRV|SDL_GPU_SHADERFORMAT_MSL|SDL_GPU_SHADERFORMAT_METALLIB,false,nullptr);
        require(gpu.device!=nullptr,"Turntable real GPU device");
        SDL_GPUTextureCreateInfo target{};target.type=SDL_GPU_TEXTURETYPE_2D;target.format=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        target.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;target.width=gpu.width;target.height=gpu.height;
        target.layer_count_or_depth=1;target.num_levels=1;target.sample_count=SDL_GPU_SAMPLECOUNT_1;
        gpu.target=SDL_CreateGPUTexture(gpu.device,&target);require(gpu.target!=nullptr,"Turntable target");
        SDL_GPUTransferBufferCreateInfo transfer{};transfer.usage=SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;transfer.size=gpu.width*gpu.height*4;
        gpu.transfer=SDL_CreateGPUTransferBuffer(gpu.device,&transfer);require(gpu.transfer!=nullptr,"Turntable readback");
        auto renderer=engine::World3DRenderer::load(gpu.device,shaders,target.format,assetRoot/"lighting/studio_environment.mstudio");
        if(!renderer)throw std::runtime_error(renderer.error().detail);
        // Current production MSAA target clears alpha to1; preserve genuine alpha
        // with the single-sample production path rather than inventing a matte.
        renderer->setModernPresentation(true);renderer->setPresentationShadows(false);renderer->setPresentationAntialiasing(false);
        renderer->setBilinearFiltering(true);renderer->setLighting(engine::modernBoardPresentationLighting());
        engine::SequenceWorld3DSlot slot;
        require(slot.configureView({0,0,int(gpu.width),int(gpu.height)},camera).has_value(),"Turntable fixed camera");
        const auto prefix="token-turntable-"+std::string{definition->slug};
        std::ofstream manifest(output/(prefix+".tsv"));
        manifest<<std::setprecision(std::numeric_limits<float>::max_digits10)
            <<"scope\tpresentation-only calibrated base GLB; no CNK/gameplay\nsize\t768\t640\nframes\t28\n"
            <<"token\t"<<unsigned(definition->token)<<"\tslug\t"<<definition->slug<<"\nunits_per_meter\t"<<definition->unitsPerMeter
            <<"\ncatalog_yaw\t"<<definition->yawDegrees<<"\nfit\tall actual vertices across28yaw poses; fixed camera;12percent margin\ncamera\t";
        for(auto value:camera.location)manifest<<value<<'\t';manifest<<"\nfov\t"<<camera.fieldOfView<<"\nbounds\t";
        for(auto value:bounds.minimum)manifest<<value<<'\t';for(auto value:bounds.maximum)manifest<<value<<'\t';manifest<<'\n';
        for(unsigned frame=0;frame<28;++frame)
        {
            require(slot.sync({poses[frame]}).has_value(),"Turntable production slot");
            const auto stats=draw(gpu,*renderer,slot,true);
            if(!renderer->modernPipeline()) throw std::runtime_error("Turntable actual PBR pipeline unavailable after production render");
            if(frame==0)
                manifest<<"backend\t"<<SDL_GetGPUDeviceDriver(gpu.device)
                    <<"\nstudio_enabled\t"<<renderer->studioEnvironmentEnabled()
                    <<"\nmsaa_samples\t"<<unsigned(renderer->presentationSampleCount())<<'\n';
            auto* mapped=SDL_MapGPUTransferBuffer(gpu.device,gpu.transfer,false);require(mapped!=nullptr,"Turntable map");
            std::vector<unsigned char> pixels(gpu.width*gpu.height*4);std::memcpy(pixels.data(),mapped,pixels.size());SDL_UnmapGPUTransferBuffer(gpu.device,gpu.transfer);
            std::size_t opaque=0,clear=0;
            for(std::size_t i=3;i<pixels.size();i+=4){opaque+=pixels[i]!=0;clear+=pixels[i]==0;}
            require(opaque>0 && clear>0 && stats.triangles>0,"Turntable actual alpha foreground/background");
            std::ostringstream suffix;suffix<<'-'<<std::setw(2)<<std::setfill('0')<<frame;
            const auto stem=prefix+suffix.str();
            auto write=[&](const std::string& name)
            {
                std::ofstream rgba(output/(name+".rgba"),std::ios::binary);
                rgba.write(reinterpret_cast<const char*>(pixels.data()),pixels.size());rgba.close();require(rgba.good(),"Turntable RGBA write");
                std::ofstream ppm(output/(name+".ppm"),std::ios::binary);ppm<<"P6\n768 640\n255\n";
                for(std::size_t i=0;i<pixels.size();i+=4)ppm.write(reinterpret_cast<const char*>(pixels.data()+i),3);
                ppm.close();require(ppm.good(),"Turntable PPM write");
            };
            write(stem);if(frame==0)write(prefix+"-thumbnail");
            manifest<<"frame\t"<<frame<<"\tyaw\t"<<360.0F*frame/28<<"\talpha_nonzero\t"<<opaque<<"\talpha_zero\t"<<clear
                <<"\ttriangles\t"<<stats.triangles<<"\tfile\t"<<stem<<'\n';
        }
        manifest.close();require(manifest.good(),"Turntable manifest write");
        std::cout<<"turntable\t"<<prefix<<"\tframes\t28\tsize\t768x640\tbackend\t"<<SDL_GetGPUDeviceDriver(gpu.device)
            <<"\tstudio_enabled\t"<<renderer->studioEnvironmentEnabled()<<"\tmsaa_samples\t"<<unsigned(renderer->presentationSampleCount())<<'\n';
    }
}

int main(int argc, char** argv)
{
    try
    {
        std::cout << std::setprecision(std::numeric_limits<float>::max_digits10);
        if (argc < 4)
            throw std::runtime_error("Usage: ModernSceneRenderProbe <modern-assets-root> "
                "<shader-directory> <existing-build-output-directory> "
                "[board/paris_board_runtime.glb] [--environment] OR "
                "--token-frame <runtime-root> <sequence-id> <tick:0..36000> <root-priority> [--benchmark] OR --token-turntable <slug-or-index>");
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
        bool includeEnvironment = false, boardArgument = false, includeCity = false;
        bool benchmarkToken = false;
        unsigned polishLevel = 0;
        unsigned cameraYaw = 28, cameraElevation = 55, uiSafePercent = 0;
        std::optional<std::filesystem::path> tabletopSamples;
        unsigned animationTick = 0;
        std::optional<TokenFrame> tokenFrame;
        std::optional<std::string> turntable;
        for (int i = 4; i < argc; ++i)
        {
            if (std::string{argv[i]} == "--environment") includeEnvironment = true;
            else if (std::string{argv[i]} == "--procedural-city")
            { includeEnvironment = true; includeCity = true; }
            else if (std::string{argv[i]} == "--polish")
            { if (++i >= argc) throw std::runtime_error("--polish requires 0..6"); polishLevel = number(argv[i], 6); }
            else if (std::string{argv[i]} == "--camera-yaw")
            { if (++i >= argc) throw std::runtime_error("--camera-yaw requires 0..359"); cameraYaw = number(argv[i],359); }
            else if (std::string{argv[i]} == "--camera-elevation")
            { if (++i >= argc) throw std::runtime_error("--camera-elevation requires 35..75"); cameraElevation = number(argv[i],75); if(cameraElevation < 35) throw std::runtime_error("elevation below 35"); }
            else if (std::string{argv[i]} == "--ui-safe-percent")
            { if (++i >= argc) throw std::runtime_error("--ui-safe-percent requires 0..40"); uiSafePercent = number(argv[i],40); }
            else if (std::string{argv[i]} == "--tabletop-samples")
            { if (++i >= argc) throw std::runtime_error("--tabletop-samples requires runtime-root"); tabletopSamples = argv[i]; }
            else if (std::string{argv[i]} == "--animation-tick")
            { if (++i >= argc) throw std::runtime_error("--animation-tick requires 0..600"); animationTick = number(argv[i],600); }
            else if (std::string{argv[i]} == "--benchmark") benchmarkToken = true;
            else if (std::string{argv[i]} == "--token-turntable")
            { if (++i >= argc || turntable) throw std::runtime_error("--token-turntable requires one slug/index"); turntable=argv[i]; }
            else if (std::string{argv[i]} == "--token-frame")
            {
                if (tokenFrame || i + 4 >= argc) throw std::runtime_error("--token-frame requires four arguments");
                TokenFrame request;
                request.runtimeRoot = argv[++i];
                request.sequenceId = number(argv[++i], std::numeric_limits<std::uint32_t>::max());
                request.tick = number(argv[++i], 36000);
                request.priority = static_cast<std::uint16_t>(number(argv[++i], 65535));
                tokenFrame = std::move(request);
            }
            else if (!boardArgument) { relativeBoard = argv[i]; boardArgument = true; }
            else throw std::runtime_error("Unexpected argument");
        }
        if(turntable)
        {
            if(tokenFrame || includeEnvironment || boardArgument || tabletopSamples || benchmarkToken || polishLevel || animationTick)
                throw std::runtime_error("Turntable cannot be combined with board/CNK/polish modes");
            tokenTurntable(assetRoot,shaders,outputDir,*turntable);return 0;
        }
        if (tokenFrame && (includeEnvironment || boardArgument))
            throw std::runtime_error("Token-frame and board presentation modes cannot be combined");
        std::vector<sequence::SequenceMeshRenderItem> items;
        if (tokenFrame) items = loadTokenFrame(assetRoot, *tokenFrame);
        else
        {
        require(!relativeBoard.is_absolute(), "Board asset path must be relative");
        for (const auto& part : relativeBoard)
            require(part != "..", "Board asset path must remain under assets root");
        data::ModernGltfLoadOptions options;
        options.unitsPerMeter = 200;
        options.localOffset = {2430, 0, 2430};
        options.groundToZero = false;
        if(relativeBoard.generic_string() == "board/usa_board_runtime.glb")
        { options.unitsPerMeter = 1; options.localOffset = {0,0,0}; }
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
        items.push_back(board);
        engine::ModernEnvironment environment(assetRoot, includeEnvironment);
        auto decorations = environment.items(boardMatrix, 0, polishLevel >= 2, !includeCity, includeCity);
        items.insert(items.end(), decorations.begin(), decorations.end());
        std::cout << "scope\tpresentation-only offscreen production renderer; no retail DAT or gameplay\n"
            << "environment_requested\t" << includeEnvironment
            << "\tenvironment_loaded\t" << decorations.size() << '\n';
        if (includeEnvironment && decorations.size() !=
            (includeCity ? engine::ModernCityCount : engine::ModernEnvironmentCount) + (polishLevel >= 2 ? 2U : 0U))
            throw std::runtime_error("Requested environment is incomplete; see loader diagnostics");
        }
        if (tokenFrame && tabletopSamples) throw std::runtime_error("tabletop samples require board mode");
        if (tabletopSamples)
        {
            // A labelled asset/animation qualification layout, not a saved game.
            // Use real decoded clocks and historical cell placement without
            // advancing rules or fabricating a Paris resource bank.
            constexpr std::array<unsigned,4> tokens{1,2,3,6};
            constexpr std::array<unsigned,4> squares{3,8,14,27};
            for(unsigned sample=0;sample<tokens.size();++sample)
            {
                const auto pose = pieces::tokenOrientation(static_cast<std::uint8_t>(squares[sample]));
                require(pose.has_value(),"Historical token placement");
                auto frames=loadTokenFrame(assetRoot,{*tabletopSamples,0x8010DU+tokens[sample]*0x63U,animationTick,224});
                const float originX=frames.front().worldTransform.values[12],originZ=frames.front().worldTransform.values[14];
                for(auto& item:frames)
                {
                    require(item.asset && item.asset->origin==data::MeshAssetOrigin::ModernGltf,"Tabletop sample modern token");
                    item.node = 0x8000FFF900000000ULL | (static_cast<std::uint64_t>(sample)<<16U) | item.node;
                    item.worldTransform=sequence::multiply(item.worldTransform,sequence::translate3D(pose->x-originX,0,pose->z-originZ));
                    items.push_back(std::move(item));
                }
            }
            for(unsigned sample=0;sample<6;++sample)
            {
                const bool hotel=sample==5;
                const unsigned square=sample<3?1:sample<5?6:24;
                const auto pose=hotel?pieces::hotelPosition(static_cast<std::uint8_t>(square)):
                    pieces::housePosition(static_cast<std::uint8_t>(square),static_cast<std::uint8_t>(sample<3?sample:sample-3));
                require(pose.has_value(),"Historical building placement");
                const auto kind=hotel?data::ModernSceneKind::Hotel:data::ModernSceneKind::House;
                auto loaded=data::loadModernGltfMesh(assetRoot/data::modernSceneDefinition(kind).relativeGlbPath,
                    *data::modernSceneLoadOptions(kind));
                if(!loaded)throw std::runtime_error(loaded.error().detail);
                auto asset=std::make_shared<data::MeshRuntimeAsset>();asset->dataId=data::modernSceneDefinition(kind).legacyMeshId;
                asset->origin=data::MeshAssetOrigin::ModernGltf;asset->renderData=*loaded;
                sequence::SequenceMeshRenderItem item;item.node=0x8000FFF800000000ULL|sample;
                item.contentsDataId=asset->dataId;item.asset=asset;item.renderData=*loaded;
                item.worldTransform=sequence::moveRySTxzTransform(pose->yaw,.1F,pose->x,pose->z);
                items.push_back(std::move(item));
            }
            std::cout << "tabletop_samples_scope\tqualification layout with four decoded retail CNKs and six historically placed modern buildings; not gameplay\n";
        }
        for (const auto& item : items)
            reportMaterials(*(item.renderData ? item.renderData : item.asset->renderData),
                std::to_string(item.contentsDataId));

        engine::SequenceWorld3DSlot slot;
        require(slot.sync(items).has_value(), "Production scene slot sync");
        std::vector<sequence::SequenceMeshRenderItem> framingItems=items;
        if(!tokenFrame && polishLevel >= 2)
        {
            std::erase_if(framingItems,[](const auto& item)
            {
                const auto node=item.node;
                return node == (engine::ModernEnvironmentNodeBase|4U) ||
                    node == (engine::ModernEnvironmentNodeBase|5U) ||
                    (node & 0xFFFFFFFF00000000ULL) == 0x8000FFF800000000ULL;
            });
        }
        const auto bounds = sceneBounds(framingItems);
        const std::array<float, 3> center{
            (bounds.minimum[0] + bounds.maximum[0]) * .5F,
            (bounds.minimum[1] + bounds.maximum[1]) * .5F,
            (bounds.minimum[2] + bounds.maximum[2]) * .5F};
        engine::World3DCamera camera;
        camera.location = {650, 650, -500};
        float radiusSquared = 0;
        for (unsigned axis = 0; axis < 3; ++axis)
            radiusSquared += std::pow((bounds.maximum[axis] - bounds.minimum[axis]) * .5F, 2.0F);
        const float radius = std::max(.01F, std::sqrt(radiusSquared));
        if (tokenFrame)
        {
            camera.location = {center[0] + radius * 1.3F,
                center[1] + radius * .9F, center[2] - radius * 2.7F};
        }
        for (std::size_t axis = 0; axis < 3; ++axis)
            camera.forward[axis] = center[axis] - camera.location[axis];
        camera.up = {0, 1, 0}; camera.fieldOfView = .7853981633974483F;
        camera.nearPlane = 10; camera.farPlane = 2500;
        if (tokenFrame)
        {
            camera.nearPlane = radius * .01F;
            camera.farPlane = radius * 8;
        }
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
                    bounds.minimum[axis]) - camera.location[axis];
            const float depth = dot(relative, forward);
            require(depth > camera.nearPlane, "Measured board must be in front of camera");
            requiredTan = std::max(requiredTan, std::max(std::abs(dot(relative, right)),
                std::abs(dot(relative, up)) * static_cast<float>(Width) / Height) / depth);
        }
        camera.fieldOfView = 2 * std::atan(requiredTan * 1.08F);
        if (polishLevel && !tokenFrame) camera = engine::modernBoardPresentationCamera(bounds, static_cast<float>(Width)/Height, static_cast<float>(cameraElevation), static_cast<float>(cameraYaw), uiSafePercent/100.0F);
        std::cout << "polish_level\t" << polishLevel << '\n';
        require(slot.configureView({0, 0, static_cast<int>(Width), static_cast<int>(Height)},
            camera).has_value(), "Production camera configuration");
        std::cout << "camera_target\t" << center[0] << ',' << center[1] << ',' << center[2]
            << "\tcamera_location\t" << camera.location[0] << ',' << camera.location[1]
            << ',' << camera.location[2] << "\thorizontal_fov_radians\t"
            << camera.fieldOfView << '\n';
        const auto projected = engine::world3DMeshScreenRect(bounds, sequence::identity3D(), *slot.view());
        if (projected)
            std::cout << "projected_scene_rect\t" << projected->left << ',' << projected->top
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
        auto renderer = engine::World3DRenderer::load(gpu.device, shaders, texture.format,
            assetRoot / "lighting/studio_environment.mstudio");
        if (!renderer) throw std::runtime_error("Renderer shaders: " + renderer.error().detail);
        std::cout << "studio_environment_enabled\t"
            << renderer->studioEnvironmentEnabled() << '\n';
        renderer->setModernPresentation(polishLevel >= 3);
        renderer->setPresentationShadows(polishLevel >= 4);
        renderer->setPresentationAntialiasing(polishLevel >= 5);
        renderer->setBilinearFiltering(true);
        engine::World3DLighting lighting;
        lighting.ambient = {.53F, .53F, .53F};
        lighting.sun.enabled = true;
        lighting.sun.color = {.7F, .7F, .7F};
        lighting.sun.direction = {.3F, -1, .4F};
        if (polishLevel >= 3) lighting = engine::modernBoardPresentationLighting();
        renderer->setLighting(lighting);
        const unsigned measuredFrames = tokenFrame && !benchmarkToken ? 0 : Frames;
        if (measuredFrames)
            for (unsigned i = 0; i < 3; ++i) (void)draw(gpu, *renderer, slot, false);
        const auto start = std::chrono::steady_clock::now();
        engine::World3DRenderStats stats;
        for (unsigned i = 0; i < measuredFrames; ++i) stats = draw(gpu, *renderer, slot, false);
        const double seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - start).count();
        stats = draw(gpu, *renderer, slot, true);
        std::cout << "presentation_shadows_enabled\t" << renderer->presentationShadowsActive()
            << "\npresentation_msaa_samples\t" << static_cast<unsigned>(renderer->presentationSampleCount()) << '\n';
        std::cout << "modern_pbr_pipeline_loaded\t" << (renderer->modernPipeline() != nullptr) << '\n';
        const bool hasModern = std::any_of(items.begin(), items.end(), [](const auto& item)
            { return item.asset->origin == data::MeshAssetOrigin::ModernGltf; });
        require(!hasModern || renderer->modernPipeline() != nullptr,
            "Modern geometry qualification requires the actual PBR pipeline");
        auto* mapped = SDL_MapGPUTransferBuffer(gpu.device, gpu.transfer, false);
        require(mapped != nullptr, "GPU readback map");
        std::vector<unsigned char> pixels(Width * Height * 4);
        std::memcpy(pixels.data(), mapped, pixels.size());
        SDL_UnmapGPUTransferBuffer(gpu.device, gpu.transfer);
        const auto output = outputDir / (tokenFrame ? "modern-token-probe.ppm" : "modern-scene-probe.ppm");
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
            << "\nrendered_triangles\t" << stats.triangles << "\nmeasured_frames\t" << measuredFrames
            << "\nfenced_offscreen_seconds\t" << seconds << "\nfenced_offscreen_fps\t"
            << (measuredFrames ? measuredFrames / seconds : 0) << "\nmeasurement_scope\t"
            << (measuredFrames ? "100 serial GPU-fenced frames after 3 warmups; excludes resource load, shader setup and readback; not a full-game benchmark"
                : "single requested CNK frame capture; no benchmark") << '\n';
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "ModernSceneRenderProbe failed: " << error.what() << '\n';
        return 1;
    }
}
