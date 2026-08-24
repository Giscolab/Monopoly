#include "LegacyAssets.hpp"

#include <cstring>
#include <filesystem>
#include <iostream>

namespace monopoly::legacyassets
{
    namespace
    {
        SDL_GPUDevice* gpuDevice = nullptr;

        Texture2D background;


        bool loadBMPTexture(
            const std::filesystem::path& path,
            Texture2D& destination)
        {
            SDL_Surface* source =
                SDL_LoadBMP(path.string().c_str());

            if (source == nullptr)
            {
                std::cerr
                    << "Unable to load legacy BMP: "
                    << path.string()
                    << " : "
                    << SDL_GetError()
                    << '\n';

                return false;
            }

            SDL_Surface* rgba =
                SDL_ConvertSurface(
                    source,
                    SDL_PIXELFORMAT_RGBA32
                );

            SDL_DestroySurface(source);

            if (rgba == nullptr)
            {
                std::cerr
                    << "Unable to convert legacy BMP: "
                    << SDL_GetError()
                    << '\n';

                return false;
            }

            const Uint32 width =
                static_cast<Uint32>(rgba->w);

            const Uint32 height =
                static_cast<Uint32>(rgba->h);

            const Uint32 bytesPerPixel = 4;

            const Uint32 uploadSize =
                width *
                height *
                bytesPerPixel;


            SDL_GPUTextureCreateInfo textureInfo{};

            textureInfo.type =
                SDL_GPU_TEXTURETYPE_2D;

            textureInfo.format =
                SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;

            textureInfo.usage =
                SDL_GPU_TEXTUREUSAGE_SAMPLER;

            textureInfo.width = width;
            textureInfo.height = height;

            textureInfo.layer_count_or_depth = 1;
            textureInfo.num_levels = 1;

            textureInfo.sample_count =
                SDL_GPU_SAMPLECOUNT_1;


            SDL_GPUTexture* texture =
                SDL_CreateGPUTexture(
                    gpuDevice,
                    &textureInfo
                );

            if (texture == nullptr)
            {
                std::cerr
                    << "SDL_CreateGPUTexture failed: "
                    << SDL_GetError()
                    << '\n';

                SDL_DestroySurface(rgba);
                return false;
            }


            SDL_GPUTransferBufferCreateInfo transferInfo{};

            transferInfo.usage =
                SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;

            transferInfo.size =
                uploadSize;


            SDL_GPUTransferBuffer* transfer =
                SDL_CreateGPUTransferBuffer(
                    gpuDevice,
                    &transferInfo
                );

            if (transfer == nullptr)
            {
                SDL_ReleaseGPUTexture(
                    gpuDevice,
                    texture
                );

                SDL_DestroySurface(rgba);

                return false;
            }


            void* mapped =
                SDL_MapGPUTransferBuffer(
                    gpuDevice,
                    transfer,
                    false
                );

            if (mapped == nullptr)
            {
                SDL_ReleaseGPUTransferBuffer(
                    gpuDevice,
                    transfer
                );

                SDL_ReleaseGPUTexture(
                    gpuDevice,
                    texture
                );

                SDL_DestroySurface(rgba);

                return false;
            }


            auto* destinationPixels =
                static_cast<unsigned char*>(mapped);

            const auto* sourcePixels =
                static_cast<const unsigned char*>(
                    rgba->pixels
                );

            const std::size_t packedRowSize =
                static_cast<std::size_t>(width) *
                bytesPerPixel;

            for (Uint32 y = 0; y < height; ++y)
            {
                std::memcpy(
                    destinationPixels +
                        static_cast<std::size_t>(y) *
                        packedRowSize,

                    sourcePixels +
                        static_cast<std::size_t>(y) *
                        rgba->pitch,

                    packedRowSize
                );
            }


            SDL_UnmapGPUTransferBuffer(
                gpuDevice,
                transfer
            );

            SDL_DestroySurface(rgba);


            SDL_GPUCommandBuffer* commandBuffer =
                SDL_AcquireGPUCommandBuffer(
                    gpuDevice
                );

            if (commandBuffer == nullptr)
            {
                SDL_ReleaseGPUTransferBuffer(
                    gpuDevice,
                    transfer
                );

                SDL_ReleaseGPUTexture(
                    gpuDevice,
                    texture
                );

                return false;
            }


            SDL_GPUCopyPass* copyPass =
                SDL_BeginGPUCopyPass(
                    commandBuffer
                );

            if (copyPass == nullptr)
            {
                SDL_CancelGPUCommandBuffer(
                    commandBuffer
                );

                SDL_ReleaseGPUTransferBuffer(
                    gpuDevice,
                    transfer
                );

                SDL_ReleaseGPUTexture(
                    gpuDevice,
                    texture
                );

                return false;
            }


            SDL_GPUTextureTransferInfo sourceInfo{};

            sourceInfo.transfer_buffer =
                transfer;

            sourceInfo.offset = 0;

            sourceInfo.pixels_per_row =
                width;

            sourceInfo.rows_per_layer =
                height;


            SDL_GPUTextureRegion destinationRegion{};

            destinationRegion.texture =
                texture;

            destinationRegion.mip_level = 0;
            destinationRegion.layer = 0;

            destinationRegion.x = 0;
            destinationRegion.y = 0;
            destinationRegion.z = 0;

            destinationRegion.w = width;
            destinationRegion.h = height;
            destinationRegion.d = 1;


            SDL_UploadToGPUTexture(
                copyPass,
                &sourceInfo,
                &destinationRegion,
                false
            );

            SDL_EndGPUCopyPass(copyPass);


            if (!SDL_SubmitGPUCommandBuffer(
                    commandBuffer))
            {
                SDL_ReleaseGPUTransferBuffer(
                    gpuDevice,
                    transfer
                );

                SDL_ReleaseGPUTexture(
                    gpuDevice,
                    texture
                );

                return false;
            }


            SDL_ReleaseGPUTransferBuffer(
                gpuDevice,
                transfer
            );


            destination.texture =
                texture;

            destination.width =
                width;

            destination.height =
                height;


            SDL_SetGPUTextureName(
                gpuDevice,
                texture,
                "Legacy Monopoly 3D Background"
            );

            return true;
        }
    }


    bool initialize(SDL_GPUDevice* device)
    {
        gpuDevice = device;

        if (gpuDevice == nullptr)
        {
            return false;
        }

        const char* basePath =
            SDL_GetBasePath();

        if (basePath == nullptr)
        {
            return false;
        }

        const std::filesystem::path path =
            std::filesystem::path(basePath) /
            "assets" /
            "legacy" /
            "BackGround.bmp";


        // display.cpp original :
        //
        // #if USA_VERSION
        //     wBmpID = IDB_BACKGROUND;
        // #endif
        //
        // LE_REND3D_SetBackgroundSurface(1, ...)

        return loadBMPTexture(
            path,
            background
        );
    }


    void shutdown()
    {
        if (gpuDevice != nullptr &&
            background.texture != nullptr)
        {
            SDL_ReleaseGPUTexture(
                gpuDevice,
                background.texture
            );
        }

        background = {};
        gpuDevice = nullptr;
    }


    const Texture2D& background3D()
    {
        return background;
    }
}
