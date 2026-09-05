#include "ResourceRuntime.hpp"

#include <utility>

namespace monopoly::data
{
    ResourceSnapshot::ResourceSnapshot(
        ResourcePaths paths, ResourceContext context)
        : paths_(std::move(paths)), context_(context)
    {
    }


    const DataBankRegistry& ResourceSnapshot::banks() const noexcept
    {
        return banks_;
    }


    std::shared_ptr<const LanguageSnapshot>
    ResourceSnapshot::language() const noexcept
    {
        return language_.snapshot();
    }


    ResourceContext ResourceSnapshot::context() const noexcept
    {
        return context_;
    }


    const ResourcePaths& ResourceSnapshot::paths() const noexcept
    {
        return paths_;
    }


    std::expected<void, DataError> ResourceRuntime::initialize(
        ResourcePaths paths, ResourceContext context, ArchiveOpenOptions options)
    {
        if (context.board != BoardEdition::Usa &&
            context.board != BoardEdition::Europe)
        {
            return std::unexpected(DataError{
                DataErrorCode::InvalidBoardEdition, {}, std::nullopt,
                "board edition must be USA or Europe" });
        }
        const auto* language = findLanguageBankTriplet(context.language);
        if (language == nullptr)
        {
            return std::unexpected(DataError{
                DataErrorCode::InvalidLanguage, {}, std::nullopt,
                "language ID must be in the source-defined range 1..10" });
        }

        auto staged = std::shared_ptr<ResourceSnapshot>(
            new ResourceSnapshot(std::move(paths), context));
        const auto mount = [&](const BankDefinition& definition)
            -> std::expected<void, DataError>
        {
            auto path = staged->paths_.resolve(definition.legacyPath);
            if (!path)
            {
                auto error = path.error();
                error.detail = std::string(definition.legacyPath) +
                    ": " + error.detail;
                return std::unexpected(std::move(error));
            }
            auto archive = staged->banks_.mount(
                *path, legacyGroupValue(definition.group), options);
            if (!archive)
            {
                return std::unexpected(archive.error());
            }
            return {};
        };

        for (const auto& definition : coreBanks(context.board))
        {
            auto result = mount(definition);
            if (!result)
            {
                return std::unexpected(result.error());
            }
        }
        for (const auto* definition :
            { &language->text, &language->graphics, &language->dialog })
        {
            auto result = mount(*definition);
            if (!result)
            {
                return std::unexpected(result.error());
            }
        }
        auto selected = staged->language_.select(staged->banks_, context.language);
        if (!selected)
        {
            return std::unexpected(selected.error());
        }

        std::scoped_lock lock(mutex_);
        active_ = std::move(staged);
        return {};
    }


    std::shared_ptr<const ResourceSnapshot>
    ResourceRuntime::snapshot() const noexcept
    {
        std::scoped_lock lock(mutex_);
        return active_;
    }


    void ResourceRuntime::shutdown() noexcept
    {
        std::scoped_lock lock(mutex_);
        active_.reset();
    }
}
