#pragma once

#include "ResourceRuntime.hpp"

namespace monopoly::startup
{
    bool mainExtendedInitialization();
    bool mainExtendedInitialization(
        const data::ResourcePaths& paths,
        data::ResourceContext context = {});
    void mainExtendedShutdown();

    [[nodiscard]] std::shared_ptr<const data::ResourceSnapshot>
    resources() noexcept;
    // Apres DISPLAY, souris et render slots : leurs destructeurs peuvent
    // encore demander des ressources, comme dans GameShutdown original.
    void releaseResources() noexcept;
}
