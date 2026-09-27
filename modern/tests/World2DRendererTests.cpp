#include "World2DRenderer.hpp"
#include "SequencePlayback.hpp"
#include "DiceDisplay.hpp"
#include "SyntheticSequenceResources.hpp"
#include <SDL3/SDL.h>
#include <iostream>
#include <vector>
#include <array>
#include <cstring>
#include <stdexcept>

namespace
{
    using namespace monopoly;
    void require(bool ok, const char* message)
    {
        std::cout << (ok ? "[PASS] " : "[FAIL] ") << message << '\n';
        if (!ok) throw std::runtime_error(message);
    }
    std::vector<std::uint8_t> capture(SDL_GPUDevice* device, engine::World2DRenderer& renderer,
        const engine::SequenceWorld2DSlot& slot, unsigned width=800, unsigned height=600)
    {
        SDL_GPUTextureCreateInfo ti{};
        ti.type=SDL_GPU_TEXTURETYPE_2D; ti.format=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        ti.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET; ti.width=width; ti.height=height;
        ti.layer_count_or_depth=1; ti.num_levels=1;
        auto* target=SDL_CreateGPUTexture(device,&ti);
        require(target!=nullptr,"readback color target allocated");
        SDL_GPUTransferBufferCreateInfo bi{};
        bi.usage=SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;bi.size=width*height*4;
        auto* transfer=SDL_CreateGPUTransferBuffer(device,&bi);
        auto* command=SDL_AcquireGPUCommandBuffer(device);
        require(transfer && command,"readback command and transfer allocated");
        SDL_GPUColorTargetInfo color{};color.texture=target;
        color.clear_color={0,0,0,1};color.load_op=SDL_GPU_LOADOP_CLEAR;color.store_op=SDL_GPU_STOREOP_STORE;
        auto* pass=SDL_BeginGPURenderPass(command,&color,1,nullptr);
        require(pass!=nullptr,"target clear begins");SDL_EndGPURenderPass(pass);
        const auto drawn=renderer.render(command,target,width,height,slot);
        if (!drawn) std::cout << drawn.error() << '\n';
        require(drawn && *drawn==slot.size(),"real quad renderer records every bitmap node");
        auto* copy=SDL_BeginGPUCopyPass(command);require(copy!=nullptr,"readback copy begins");
        SDL_GPUTextureRegion source{};source.texture=target;source.w=width;source.h=height;source.d=1;
        SDL_GPUTextureTransferInfo destination{transfer,0,width,height};
        SDL_DownloadFromGPUTexture(copy,&source,&destination);SDL_EndGPUCopyPass(copy);
        auto* fence=SDL_SubmitGPUCommandBufferAndAcquireFence(command);
        require(fence && SDL_WaitForGPUFences(device,true,&fence,1),"GPU fence completes actual rasterization");
        auto* mapped=SDL_MapGPUTransferBuffer(device,transfer,false);require(mapped!=nullptr,"GPU pixels mapped");
        std::vector<std::uint8_t> pixels(bi.size);std::memcpy(pixels.data(),mapped,pixels.size());
        SDL_UnmapGPUTransferBuffer(device,transfer);SDL_ReleaseGPUFence(device,fence);
        SDL_ReleaseGPUTransferBuffer(device,transfer);SDL_ReleaseGPUTexture(device,target);
        return pixels;
    }
    std::array<std::uint8_t,4> pixel(const std::vector<std::uint8_t>& p,unsigned x,unsigned y,unsigned width=800)
    { const auto i=(y*width+x)*4;return {p.at(i),p.at(i+1),p.at(i+2),p.at(i+3)}; }

    void testLabeledCamera(SDL_GPUDevice* device, engine::World2DRenderer& renderer)
    {
        SyntheticSequenceResources resources;
        resources.service.shutdown();
        // Valid CNKs exercise Main's label 1 through the real DAT/program path.
        // A non-camera group uses its transformed bounding-box center; a type 7
        // camera additionally supplies the scale through its FOV attribute.
        const auto group = SyntheticSequenceResources::words({
            0x01000024, 0, 0x04000000, 2, 0x88000014, 0, 0, 20, 20});
        auto camera = SyntheticSequenceResources::words({
            0x07000035, 0, 0x04000000, 2, 0x3F800000, 0x459C4000});
        camera.push_back(std::byte{1});
        const auto cameraAttributes = SyntheticSequenceResources::words({
            0x88000014, 0xFFFFFFF6, 0xFFFFFFF6, 10, 10, 0x90000008, 0x40000000});
        camera.insert(camera.end(), cameraAttributes.begin(), cameraAttributes.end());
        const auto leaf = SyntheticSequenceResources::words({0x03000014, 0, 0x04000000, 2, 0});
        const std::array items{
            data::ArchiveBuildItem{data::LegacyDataType::Bitmap, SyntheticSequenceResources::bitmap24()},
            data::ArchiveBuildItem{data::LegacyDataType::Chunky, group},
            data::ArchiveBuildItem{data::LegacyDataType::Chunky, camera},
            data::ArchiveBuildItem{data::LegacyDataType::Chunky, leaf}};
        require(data::writeLegacyDataArchive(resources.directory / "Dat_Mon/dat_main.dat", items).has_value(),
            "camera fixture writes bitmap, generic group and camera CNKs");
        const auto paths = data::ResourcePaths::create(std::array{resources.directory});
        require(paths && resources.service.initialize(*paths), "camera fixture reloads its real DAT bank");
        engine::SequencePlayback playback(resources.service.snapshot());
        const auto bitmapId = data::packDataId(data::LegacyGroupId::Main, 3);
        const auto groupId = data::packDataId(data::LegacyGroupId::Main, 1);
        const auto cameraId = data::packDataId(data::LegacyGroupId::Main, 2);
        const std::array<std::uint8_t,4> blue{0,0,255,255}, white{255,255,255,255},
            red{255,0,0,255}, green{0,255,0,255}, black{0,0,0,255};
        require(playback.startXY(bitmapId, 5, 100, 100) && playback.update(0),
            "camera fixture starts the bitmap in world coordinates");
        const auto initial = capture(device, renderer, playback.world2D());
        require(pixel(initial,100,100)==blue, "default 2D camera preserves the original logical canvas");
        require(playback.startXY(groupId, 10, 410, 290, false, 1) && playback.update(1),
            "a generic 2D group takes camera label one");
        auto pixels = capture(device, renderer, playback.world2D());
        require(pixel(pixels,80,100)==blue && pixel(pixels,100,100)==black,
            "label-one bbox center (420,300) moves actual GPU pixels from x100 to x80");

        require(playback.stop(bitmapId, 5) && playback.startXY(bitmapId, 5, 410, 310) &&
            playback.startXY(cameraId, 20, 400, 300) && playback.update(2),
            "real 2D camera replaces the generic label and supplies a twofold zoom");
        pixels = capture(device, renderer, playback.world2D());
        require(pixel(pixels,420,320)==blue && pixel(pixels,421,321)==blue &&
            pixel(pixels,422,320)==white && pixel(pixels,420,322)==red && pixel(pixels,422,322)==green,
            "camera FOV zooms each bitmap texel around the viewport center");

        require(playback.startXY(groupId, 11, 430, 290, false, 1) && playback.update(3),
            "a second generic object replaces the camera label without supplying scale");
        pixels = capture(device, renderer, playback.world2D());
        require(pixel(pixels,340,320)==blue && pixel(pixels,342,322)==green && pixel(pixels,420,320)==black,
            "generic label replacement keeps the previous twofold camera scale");
        require(playback.stop(groupId, 11) && playback.update(4), "the newest label owner stops");
        pixels = capture(device, renderer, playback.world2D());
        require(pixel(pixels,340,320)==blue && pixel(pixels,420,320)==black,
            "removing the owner retains the camera instead of reviving an older same-label sequence");

        require(playback.stop(cameraId, 20) &&
            playback.startXYSR(cameraId, 20, 400, 300, 1.0F, 1.5707963267948966F) && playback.update(5),
            "a quarter-turned camera reclaims the label through its CNK");
        pixels = capture(device, renderer, playback.world2D());
        require(pixel(pixels,420,279)==blue && pixel(pixels,420,277)==white &&
            pixel(pixels,422,279)==red && pixel(pixels,422,277)==green,
            "inverse camera rotation and zoom preserve the four source colors on the GPU");
    }
}
int main()
{
    std::cout << std::unitbuf;
    SDL_GPUDevice* device=nullptr;
    try
    {
        require(SDL_Init(SDL_INIT_VIDEO),"SDL video initialized");
        device=SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_DXIL,false,"direct3d12");
        require(device!=nullptr,"real Direct3D12 device is required; no passing skip");
        auto loaded=engine::World2DRenderer::load(device,MONOPOLY_SHADER_DIR,SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
        if (!loaded) std::cout << loaded.error() << '\n';
        require(loaded.has_value(),"2D pipeline and shared quad upload succeed");
        auto renderer=std::move(*loaded);
        SyntheticSequenceResources resources(true);
        engine::SequencePlayback playback(resources.service.snapshot());
        const auto face=data::packDataId(data::LegacyGroupId::Main,0x96);
        const auto yellow=data::packDataId(data::LegacyGroupId::Main,0x97);
        require(playback.startXY(face,256,0,0) && playback.update(0),"nested synthetic bitmap reaches production World2D slot");
        auto baseline=capture(device,*renderer,playback.world2D());
        const std::array<std::uint8_t,4> blue{0,0,255,255}, white{255,255,255,255},
            red{255,0,0,255},green{0,255,0,255},black{0,0,0,255},gold{255,255,0,255};
        require(pixel(baseline,400,300)==blue && pixel(baseline,401,300)==white &&
            pixel(baseline,400,301)==red && pixel(baseline,401,301)==green,
            "BMP 2x2 exact RGBA pixels and opaque green survive GPU sampling");
        dice::TwoDPlayback dice;
        bool notification=false;
        require(playback.stop(face,256) && dice.sync({1,1},false,true,notification,playback) &&
            playback.update(1),"DiceDisplay::plan2D reaches GPU slot through exact dice playback");
        auto moved=capture(device,*renderer,playback.world2D());
        require(pixel(moved,400,300)==black && pixel(moved,365,300)==blue && pixel(moved,389,300)==blue &&
            pixel(moved,366,301)==green && pixel(moved,390,301)==green,
            "GPU proof: original pixel erased, dice pixels moved exactly by -35 and -11");
        require(renderer->textureCount()==1,"two dice share a single cached GPU texture");
        require(playback.startXY(yellow,258,-35,0) && playback.update(2),"higher priority bitmap overlaps first die");
        auto overlap=capture(device,*renderer,playback.world2D());
        require(pixel(overlap,365,300)==gold,"higher sequencer priority paints over lower priority");
        require(playback.startXY(face,258,-35,0) && playback.update(3),"equal-priority newer sequence inserted first");
        overlap=capture(device,*renderer,playback.world2D());
        require(pixel(overlap,365,300)==gold,"equal-priority traversal keeps older sibling drawn last");
        auto scaled=capture(device,*renderer,playback.world2D(),1600,1200);
        require(pixel(scaled,730,600,1600)==gold && pixel(scaled,778,600,1600)==blue,
            "logical coordinates scale correctly to 1600x1200");
        auto letterbox=capture(device,*renderer,playback.world2D(),1000,600);
        require(pixel(letterbox,465,300,1000)==gold && pixel(letterbox,489,300,1000)==blue &&
            pixel(letterbox,0,300,1000)==black,"800x600 logical content is centered with preserved side bars");
        require(playback.stop(face,256) && playback.stop(face,257) && playback.stop(face,258) &&
            playback.stop(yellow,258) && playback.update(4),"all synthetic dice stop");
        auto empty=capture(device,*renderer,playback.world2D());
        require(renderer->textureCount()==0 && pixel(empty,365,300)==black,"shutdown prunes GPU texture cache and old pixels");
        testLabeledCamera(device, *renderer);
        renderer.reset();
        SDL_DestroyGPUDevice(device);device=nullptr;SDL_Quit();return 0;
    }
    catch(const std::exception& e)
    {
        std::cerr << "[FAIL] " << e.what() << " SDL: " << SDL_GetError() << '\n';
        if(device) SDL_DestroyGPUDevice(device);SDL_Quit();return 1;
    }
}
