#include "GPUFrame.hpp"
#include "Display.hpp"
#include "LegacyAssets.hpp"
#include "LogicalViewport.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
    [[nodiscard]] Uint32 roundedCoordinate(
        double value,
        Uint32 maximum)
    {
        const long long rounded = std::llround(value);

        if (rounded <= 0)
        {
            return 0;
        }

        if (rounded >= static_cast<long long>(maximum))
        {
            return maximum;
        }

        return static_cast<Uint32>(rounded);
    }
}

namespace monopoly::engine::gpuframe
{
    bool present(
        SDL_GPUDevice* device,
        SDL_Window* window)
    {
        if (device == nullptr || window == nullptr)
        {
            return false;
        }

        SDL_GPUCommandBuffer* commandBuffer =
            SDL_AcquireGPUCommandBuffer(device);

        if (commandBuffer == nullptr)
        {
            return false;
        }

        SDL_GPUTexture* swapchainTexture = nullptr;

        Uint32 width = 0;
        Uint32 height = 0;

        if (!SDL_WaitAndAcquireGPUSwapchainTexture(
                commandBuffer,
                window,
                &swapchainTexture,
                &width,
                &height))
        {
            SDL_CancelGPUCommandBuffer(commandBuffer);
            return false;
        }

        if (swapchainTexture != nullptr)
        {
            SDL_GPUColorTargetInfo target{};

            target.texture = swapchainTexture;

            // Aucun faux asset :
            // tant que le véritable BMP_sybkgrnd n'est pas
            // disponible, le framebuffer est simplement noir.
            target.clear_color =
            {
                0.0f,
                0.0f,
                0.0f,
                1.0f
            };

            target.load_op =
                SDL_GPU_LOADOP_CLEAR;

            target.store_op =
                SDL_GPU_STOREOP_STORE;

            target.cycle = true;

            SDL_GPURenderPass* renderPass =
                SDL_BeginGPURenderPass(
                    commandBuffer,
                    &target,
                    1,
                    nullptr
                );

            if (renderPass == nullptr)
            {
                SDL_CancelGPUCommandBuffer(commandBuffer);
                return false;
            }

            SDL_EndGPURenderPass(renderPass);


            // ------------------------------------------------
            // Background du viewport 3D.
            //
            // PC3D/view.cpp original calcule :
            //
            // horizontalScale =
            //     backgroundWidth / viewportWidth
            //
            // verticalScale =
            //     backgroundHeight / viewportHeight
            //
            // puis effectue un stretch blit.
            // ------------------------------------------------

            const legacyassets::Texture2D& background =
                legacyassets::background3D();

            if (background.texture != nullptr)
            {
                logicalviewport::LogicalRect logicalDestination{};

                switch (
                    display::stateReadOnly().viewportInUse)
                {
                    case display::Viewport3D::Main:
                        // viewRects[0]
                        // { 0, 0, 800, 450 }
                        logicalDestination =
                            { 0.0, 0.0, 800.0, 450.0 };
                        break;

                    case display::Viewport3D::Status:
                        // viewRects[1]
                        // { 0, 0, 400, 225 }
                        logicalDestination =
                            { 0.0, 0.0, 400.0, 225.0 };
                        break;

                    case display::Viewport3D::Trade:
                        // viewRects[2]
                        // { 200, 0, 600, 225 }
                        logicalDestination =
                            { 200.0, 0.0, 400.0, 225.0 };
                        break;

                    case display::Viewport3D::Off:
                    default:
                        break;
                }


                if (logicalDestination.width > 0.0 &&
                    logicalDestination.height > 0.0 &&
                    width <= static_cast<Uint32>(
                        std::numeric_limits<int>::max()) &&
                    height <= static_cast<Uint32>(
                        std::numeric_limits<int>::max()))
                {
                    const auto transform =
                        logicalviewport::makeTransform(
                            static_cast<int>(width),
                            static_cast<int>(height)
                        );

                    const auto destination =
                        logicalviewport::logicalToPixelRect(
                            transform,
                            logicalDestination
                        );

                    const Uint32 destinationX =
                        roundedCoordinate(destination.x, width);

                    const Uint32 destinationY =
                        roundedCoordinate(destination.y, height);

                    const Uint32 destinationRight =
                        roundedCoordinate(
                            destination.x + destination.width,
                            width
                        );

                    const Uint32 destinationBottom =
                        roundedCoordinate(
                            destination.y + destination.height,
                            height
                        );

                    const Uint32 destinationWidth =
                        destinationRight - destinationX;

                    const Uint32 destinationHeight =
                        destinationBottom - destinationY;


                    if (destinationWidth == 0 ||
                        destinationHeight == 0)
                    {
                        return SDL_SubmitGPUCommandBuffer(
                            commandBuffer
                        );
                    }

                    SDL_GPUBlitInfo blit{};

                    blit.source.texture =
                        background.texture;

                    blit.source.mip_level = 0;
                    blit.source.layer_or_depth_plane = 0;

                    blit.source.x = 0;
                    blit.source.y = 0;

                    blit.source.w =
                        background.width;

                    blit.source.h =
                        background.height;


                    blit.destination.texture =
                        swapchainTexture;

                    blit.destination.mip_level = 0;
                    blit.destination.layer_or_depth_plane = 0;

                    blit.destination.x =
                        destinationX;

                    blit.destination.y =
                        destinationY;

                    blit.destination.w =
                        destinationWidth;

                    blit.destination.h =
                        destinationHeight;


                    // Le framebuffer a déjà été vidé en noir
                    // par le render pass précédent.
                    blit.load_op =
                        SDL_GPU_LOADOP_LOAD;

                    // Le nearest-neighbour conserve les pixels nets du
                    // bitmap 1999, y compris apres redimensionnement.
                    blit.filter =
                        SDL_GPU_FILTER_NEAREST;

                    blit.flip_mode =
                        SDL_FLIP_NONE;

                    // Le swapchain vient d'etre entierement vide en noir
                    // plus haut dans le meme command buffer. Un cycle ici
                    // basculerait vers un backing texture indefini, puis
                    // LOAD lirait ces donnees indefinies au lieu de garder
                    // les bandes letterbox noires.
                    blit.cycle = false;


                    SDL_BlitGPUTexture(
                        commandBuffer,
                        &blit
                    );
                }
            }
        }

        return SDL_SubmitGPUCommandBuffer(
            commandBuffer
        );
    }
}

