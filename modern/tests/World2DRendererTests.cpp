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
        renderer.reset();
        SDL_DestroyGPUDevice(device);device=nullptr;SDL_Quit();return 0;
    }
    catch(const std::exception& e)
    {
        std::cerr << "[FAIL] " << e.what() << " SDL: " << SDL_GetError() << '\n';
        if(device) SDL_DestroyGPUDevice(device);SDL_Quit();return 1;
    }
}
