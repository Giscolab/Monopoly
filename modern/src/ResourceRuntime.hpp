#pragma once

#include "LanguageResources.hpp"
#include "ResourcePaths.hpp"

#include <memory>
#include <mutex>

namespace monopoly::data
{
    struct ResourceContext
    {
        BoardEdition board{ BoardEdition::Usa };
        LanguageId language{ LanguageId::EnglishUs };
    };


    // Le registre et LANG partagent les memes archives. Les consommateurs
    // conservent ce snapshot pour garder les banques utilisables, meme apres
    // un remplacement ou l'arret du service. Aucun clear/unmount premature.
    class ResourceSnapshot final
    {
    public:
        [[nodiscard]] const DataBankRegistry& banks() const noexcept;
        [[nodiscard]] std::shared_ptr<const LanguageSnapshot>
        language() const noexcept;
        [[nodiscard]] ResourceContext context() const noexcept;
        [[nodiscard]] const ResourcePaths& paths() const noexcept;

    private:
        friend class ResourceRuntime;
        ResourceSnapshot(ResourcePaths paths, ResourceContext context);

        ResourcePaths paths_;
        ResourceContext context_;
        DataBankRegistry banks_;
        LanguageService language_;
    };


    class ResourceRuntime final
    {
    public:
        // MainExtendedInitialization : cinq banques core, puis LANG 9/5/10.
        // Tout echec conserve exactement le snapshot actif precedent.
        [[nodiscard]] std::expected<void, DataError> initialize(
            ResourcePaths paths,
            ResourceContext context = {},
            ArchiveOpenOptions options = {});

        [[nodiscard]] std::shared_ptr<const ResourceSnapshot>
        snapshot() const noexcept;
        void shutdown() noexcept;

    private:
        mutable std::mutex mutex_;
        std::shared_ptr<const ResourceSnapshot> active_;
    };
}
