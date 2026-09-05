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
    std::expected<World3DRenderStats, World3DRendererError> recordWorld3D(
        SDL_GPUCommandBuffer* command, SDL_GPUTexture* target,
        Uint32 width, Uint32 height, World3DRenderer& renderer,
        const SequenceWorld3DSlot& world)
    {
        if (!world.view()) return World3DRenderStats{};
        if (width > static_cast<Uint32>(std::numeric_limits<int>::max()) ||
            height > static_cast<Uint32>(std::numeric_limits<int>::max()))
            return std::unexpected(World3DRendererError{
                World3DRendererErrorCode::InvalidTargetSize,
                "World3D target exceeds logical viewport range", {}, {}});
        const auto rect = world.view()->viewport;
        const auto transform = logicalviewport::makeTransform(
            static_cast<int>(width), static_cast<int>(height));
        const auto pixels = logicalviewport::logicalToPixelRect(transform,
            {static_cast<double>(rect.left), static_cast<double>(rect.top),
             static_cast<double>(rect.right - rect.left),
             static_cast<double>(rect.bottom - rect.top)});
        const SDL_GPUViewport viewport{static_cast<float>(pixels.x),
            static_cast<float>(pixels.y), static_cast<float>(pixels.width),
            static_cast<float>(pixels.height), 0, 1};
        return renderer.render(command, target, width, height, viewport, world);
    }
    bool present(
        SDL_GPUDevice* device,
        SDL_Window* window,
        World3DRenderer* renderer,
        const SequenceWorld3DSlot* world)
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
                const auto logicalView = display::worldViewport(
                    display::stateReadOnly().viewportInUse);
                const logicalviewport::LogicalRect logicalDestination{
                    static_cast<double>(logicalView.left),
                    static_cast<double>(logicalView.top),
                    static_cast<double>(logicalView.right - logicalView.left),
                    static_cast<double>(logicalView.bottom - logicalView.top)};


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

            // Slot 1 follows the clear and the historical 3D background.
            // LOAD in World3DRenderer preserves both and the letterbox bars.
            if (renderer && world && world->view())
            {
                const auto drawn = recordWorld3D(commandBuffer, swapchainTexture,
                    width, height, *renderer, *world);
                if (!drawn)
                {
                    // SDL forbids cancelling a command after swapchain acquire.
                    (void)SDL_SubmitGPUCommandBuffer(commandBuffer);
                    return SDL_SetError("World3D: %s", drawn.error().detail.c_str());
                }
            }
        }

        return SDL_SubmitGPUCommandBuffer(
            commandBuffer
        );
    }
}

