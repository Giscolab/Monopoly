#include "StudioEnvironmentGPU.hpp"
#include "World3DRenderer.hpp"
#include "SequenceTransforms.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string_view>

#ifndef MONOPOLY_SHADER_DIR
#error MONOPOLY_SHADER_DIR must identify generated World3D shader assets
#endif

namespace
{
    using namespace monopoly;
    int failures{};
    void expect(bool value, std::string_view label)
    { std::cout << (value ? "[PASS] " : "[FAIL] ") << label << '\n'; failures += !value; }
    void put32(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint32_t value)
    { for (unsigned i=0; i<4; ++i) bytes[at+i] = static_cast<std::uint8_t>(value >> (8U*i)); }
    std::vector<std::uint8_t> fixture()
    {
        std::vector<std::uint8_t> bytes(32U+6U*85U*8U);
        const std::array<std::uint8_t,8> magic{'M','S','T','U','D','I','O',0};
        std::copy(magic.begin(),magic.end(),bytes.begin());
        put32(bytes,8,1); put32(bytes,12,8); put32(bytes,16,4);
        put32(bytes,20,6); put32(bytes,24,1); put32(bytes,28,static_cast<std::uint32_t>(bytes.size()-32));
        // Independent prefiltered fixture: sharp face radiance contrasts at
        // mip0, progressively converging to a uniform broad rough reflection.
        constexpr std::uint16_t sharp[]{0x4400,0x2800,0x4000,0x2800,0x4200,0x2800};
        std::size_t at=32;
        for (unsigned mip=0,size=8; mip<4; ++mip,size>>=1U)
            for (unsigned face=0; face<6; ++face)
                for (unsigned pixel=0; pixel<size*size; ++pixel)
                {
                    const std::uint16_t value = mip==0 ? sharp[face] :
                        (mip==1 ? (face%2==0 ? 0x3E00U : 0x3400U) : 0x3A00U);
                    for (unsigned channel=0; channel<4; ++channel)
                    {
                        const auto bits = channel==3 ? 0x3C00U : value;
                        bytes[at++] = static_cast<std::uint8_t>(bits);
                        bytes[at++] = static_cast<std::uint8_t>(bits>>8U);
                    }
                }
        return bytes;
    }
    void cpuTests()
    {
        auto bytes=fixture();
        auto parsed=engine::parseStudioEnvironment(bytes);
        expect(parsed && parsed->width==8 && parsed->mipCount==4,
            "bounded full-chain linear HDR studio asset accepted");
        if (parsed) expect(parsed->rgba16f[1]==0x44,
            "HDR radiance above one remains encoded without gamma/clamping");
        const auto rejected=[&](auto mutate,std::string_view label) {
            auto bad=bytes; mutate(bad); expect(!engine::parseStudioEnvironment(bad),label); };
        rejected([](auto& b){b[0]='X';},"incorrect magic rejected");
        rejected([](auto& b){put32(b,8,2);},"unknown version rejected");
        rejected([](auto& b){put32(b,12,0);},"zero width rejected");
        rejected([](auto& b){put32(b,12,3);},"non-power-of-two width rejected");
        rejected([](auto& b){put32(b,12,256);},"oversized cube rejected");
        rejected([](auto& b){put32(b,16,9);},"excessive mip count rejected");
        rejected([](auto& b){put32(b,16,3);},"incomplete mip chain rejected");
        rejected([](auto& b){put32(b,20,5);},"missing cube face rejected");
        rejected([](auto& b){put32(b,24,2);},"unknown pixel format rejected");
        rejected([](auto& b){put32(b,28,0);},"incorrect payload length rejected");
        rejected([](auto& b){b.pop_back();},"truncated pixel data rejected");
        rejected([](auto& b){b.push_back(0);},"trailing byte rejected");
        rejected([](auto& b){b[32]=1;b[33]=0x7C;},"NaN radiance rejected");
        rejected([](auto& b){b[32]=0;b[33]=0x7C;},"infinite radiance rejected");
        rejected([](auto& b){b[33]=0xBC;},"negative radiance rejected");
        rejected([](auto& b){b[39]=0;},"non-one alpha rejected");
        expect(!engine::parseStudioEnvironment(std::vector<std::uint8_t>(2U*1024U*1024U+1U)),
            "file size over two MiB rejected");
        expect(!engine::parseStudioEnvironment(std::array<std::uint8_t,7>{}),
            "truncated header rejected");
    }

    engine::SequenceWorld3DSlot curvedMetal(float roughness)
    {
        auto geometry=std::make_shared<data::MeshRenderData>();
        constexpr unsigned side=17;
        for (unsigned row=0;row<side;++row) for (unsigned column=0;column<side;++column)
        {
            const float x=(static_cast<float>(column)-8)*.25F;
            const float y=(static_cast<float>(row)-8)*.25F;
            const float z=std::sqrt(std::max(0.F,4.F-x*x-y*y));
            data::MeshVertex vertex;
            vertex.position={x,y,7.F-z}; vertex.normal={x*.5F,y*.5F,-z*.5F};
            geometry->vertices.push_back(vertex);
        }
        const auto inside=[&](unsigned index) {
            const auto& p=geometry->vertices[index].position;
            return p[0]*p[0]+p[1]*p[1]<3.85F; };
        for (unsigned row=0;row+1<side;++row) for (unsigned col=0;col+1<side;++col)
        {
            const auto a=row*side+col,b=a+1,c=a+side,d=c+1;
            if (inside(a)&&inside(b)&&inside(c)&&inside(d))
                geometry->indices.insert(geometry->indices.end(),{a,c,b,b,c,d});
        }
        data::MeshRenderBatch batch;
        batch.indexCount=static_cast<std::uint32_t>(geometry->indices.size());
        batch.material.model=data::MeshMaterialModel::MetallicRoughness;
        batch.material.diffuse={.8F,.8F,.8F,1};
        batch.material.metallic=1; batch.material.roughness=roughness;
        batch.material.doubleSided=true;
        geometry->batches.push_back(batch);
        geometry->bounds={{-2,-2,5},{2,2,7}};
        auto asset=std::make_shared<data::MeshRuntimeAsset>();
        asset->dataId=data::packDataId(0xFFFB, static_cast<std::uint16_t>(roughness*1000));
        asset->origin=data::MeshAssetOrigin::ModernGltf; asset->renderData=geometry;
        sequence::SequenceMeshRenderItem item;
        item.node=1;item.contentsDataId=asset->dataId;item.asset=asset;
        item.worldTransform=sequence::identity3D();
        engine::SequenceWorld3DSlot slot;
        (void)slot.sync({item});
        engine::World3DCamera camera;
        camera.location={0,0,0};
        camera.forward={0,0,1};camera.up={0,1,0};camera.fieldOfView=1.2F;
        camera.nearPlane=1;camera.farPlane=100;
        (void)slot.configureView({0,0,64,64},camera);
        return slot;
    }
    using Pixels=std::array<std::uint8_t,64U*64U*4U>;
    bool draw(engine::World3DRenderer& renderer, SDL_GPUDevice* device,
        SDL_GPUTexture* target,const engine::SequenceWorld3DSlot& slot,Pixels& pixels)
    {
        auto* command=SDL_AcquireGPUCommandBuffer(device);
        if (!command) return false;
        SDL_GPUColorTargetInfo color{};
        color.texture=target;color.clear_color={0,0,0,1};
        color.load_op=SDL_GPU_LOADOP_CLEAR;color.store_op=SDL_GPU_STOREOP_STORE;
        auto* pass=SDL_BeginGPURenderPass(command,&color,1,nullptr);
        if (!pass) { SDL_CancelGPUCommandBuffer(command);return false; }
        SDL_EndGPURenderPass(pass);
        const SDL_GPUViewport viewport{0,0,64,64,0,1};
        if (!renderer.render(command,target,64,64,viewport,slot))
        { SDL_CancelGPUCommandBuffer(command);return false; }
        SDL_GPUTransferBufferCreateInfo info{};
        info.usage=SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;info.size=static_cast<Uint32>(pixels.size());
        auto* transfer=SDL_CreateGPUTransferBuffer(device,&info);
        auto* copy=transfer ? SDL_BeginGPUCopyPass(command) : nullptr;
        if (!copy)
        {
            SDL_CancelGPUCommandBuffer(command);
            if (transfer) SDL_ReleaseGPUTransferBuffer(device,transfer);
            return false;
        }
        SDL_GPUTextureRegion from{};from.texture=target;from.w=from.h=64;from.d=1;
        SDL_GPUTextureTransferInfo to{};to.transfer_buffer=transfer;to.pixels_per_row=to.rows_per_layer=64;
        SDL_DownloadFromGPUTexture(copy,&from,&to);SDL_EndGPUCopyPass(copy);
        auto* fence=SDL_SubmitGPUCommandBufferAndAcquireFence(command);
        bool success=fence && SDL_WaitForGPUFences(device,true,&fence,1);
        if (success)
        {
            auto* mapped=SDL_MapGPUTransferBuffer(device,transfer,false);
            success=mapped!=nullptr;
            if (mapped) { SDL_memcpy(pixels.data(),mapped,pixels.size());SDL_UnmapGPUTransferBuffer(device,transfer); }
        }
        if (fence) SDL_ReleaseGPUFence(device,fence);
        SDL_ReleaseGPUTransferBuffer(device,transfer);
        return success;
    }
    int contrast(const Pixels& pixels)
    {
        int low=255,high=0;
        // Interior only, so the clear background and silhouette are excluded.
        for (unsigned row=25;row<39;++row) for (unsigned col=25;col<39;++col)
        { const int value=pixels[(row*64U+col)*4U];low=std::min(low,value);high=std::max(high,value); }
        return high-low;
    }
    void gpuTests()
    {
        if (!SDL_Init(SDL_INIT_VIDEO)) { std::cout << "[SKIP] SDL video unavailable\n";return; }
        auto* device=SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_DXIL|SDL_GPU_SHADERFORMAT_SPIRV|SDL_GPU_SHADERFORMAT_MSL,
            true,nullptr);
        if (!device) { std::cout << "[SKIP] GPU unavailable: " << SDL_GetError() << '\n';SDL_Quit();return; }
        const auto root=std::filesystem::temp_directory_path()/
            ("monopoly-studio-test-"+std::to_string(SDL_GetTicksNS()));
        std::filesystem::create_directories(root);
        const auto valid=root/"valid.studio",bad=root/"bad.studio";
        const auto bytes=fixture();
        { std::ofstream file(valid,std::ios::binary);file.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size())); }
        { std::ofstream file(bad,std::ios::binary);file << "bad"; }
        constexpr auto format=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        auto off=engine::World3DRenderer::load(device,MONOPOLY_SHADER_DIR,format);
        auto missing=engine::World3DRenderer::load(device,MONOPOLY_SHADER_DIR,format,root/"missing.studio");
        auto malformed=engine::World3DRenderer::load(device,MONOPOLY_SHADER_DIR,format,bad);
        auto enabled=engine::World3DRenderer::load(device,MONOPOLY_SHADER_DIR,format,valid);
        expect(off && !off->studioEnvironmentEnabled(),"default renderer environment disabled");
        expect(missing && !missing->studioEnvironmentEnabled(),"missing optional environment preserves renderer");
        expect(malformed && !malformed->studioEnvironmentEnabled(),"malformed optional environment preserves renderer");
        expect(enabled && enabled->studioEnvironmentEnabled(),"valid HDR cube enables optional studio lighting");
        SDL_GPUTextureCreateInfo targetInfo{};
        targetInfo.type=SDL_GPU_TEXTURETYPE_2D;targetInfo.format=format;
        targetInfo.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
        targetInfo.width=targetInfo.height=64;targetInfo.layer_count_or_depth=1;targetInfo.num_levels=1;
        targetInfo.sample_count=SDL_GPU_SAMPLECOUNT_1;
        auto* target=SDL_CreateGPUTexture(device,&targetInfo);
        expect(target!=nullptr,"studio test color target allocated");
        if (off&&missing&&malformed&&enabled&&target)
        {
            engine::World3DLighting light;light.ambient={.15F,.15F,.15F};
            off->setLighting(light);missing->setLighting(light);malformed->setLighting(light);enabled->setLighting(light);
            Pixels old{},absent{},invalid{},sharp{},rough{};
            const auto smoothSlot=curvedMetal(.08F),roughSlot=curvedMetal(1.F);
            const bool read=draw(*off,device,target,smoothSlot,old)&&
                draw(*missing,device,target,smoothSlot,absent)&&draw(*malformed,device,target,smoothSlot,invalid)&&
                draw(*enabled,device,target,smoothSlot,sharp)&&draw(*enabled,device,target,roughSlot,rough);
            expect(read,"real GPU studio renders and fence readback succeed");
            expect(read && old[(32U*64U+32U)*4U] > 60U,
                "curved metal fixture is visible in the configured camera frustum");
            expect(read&&old==absent&&old==invalid,"missing/malformed environment retains disabled pixels exactly");
            expect(read&&sharp!=old&&contrast(sharp)>40,"HDR reflections create contrast on curved metallic geometry");
            expect(read&&contrast(rough)<contrast(sharp),"roughness selects broader prefiltered reflection mips");
        }
        if (off) off->reset();if (missing) missing->reset();
        if (malformed) malformed->reset();if (enabled) enabled->reset();
        if (target) SDL_ReleaseGPUTexture(device,target);
        SDL_DestroyGPUDevice(device);SDL_Quit();
        std::filesystem::remove(valid);std::filesystem::remove(bad);std::filesystem::remove(root);
    }
}
int main()
{
    std::cout << std::unitbuf;
    cpuTests();gpuTests();
    std::cout << "Studio environment failures: " << failures << '\n';
    return failures ? 1 : 0;
}
