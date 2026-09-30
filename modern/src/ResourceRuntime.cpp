#include "ResourceRuntime.hpp"

#include <utility>

namespace monopoly::data
{
    std::vector<DataError> inspectResourceInstallation(
        const ResourcePaths& paths, ResourceContext context,
        ArchiveOpenOptions options)
    {
        std::vector<DataError> issues;
        if (context.board != BoardEdition::Usa &&
            context.board != BoardEdition::Europe)
        {
            issues.push_back({DataErrorCode::InvalidBoardEdition, {},
                std::nullopt, "board edition must be USA or Europe"});
            return issues;
        }
        const auto* language = findLanguageBankTriplet(context.language);
        if (!language)
        {
            issues.push_back({DataErrorCode::InvalidLanguage, {},
                std::nullopt, "language ID must be in the source-defined range 1..10"});
            return issues;
        }

        const auto inspect = [&](const BankDefinition& bank)
        {
            const auto path = paths.resolve(bank.legacyPath);
            if (!path)
            {
                auto error = path.error();
                error.detail = std::string(bank.legacyPath) + ": " + error.detail;
                issues.push_back(std::move(error));
                return;
            }
            const auto archive = LegacyDataArchive::open(
                *path, legacyGroupValue(bank.group), options);
            if (!archive)
            {
                auto error = archive.error();
                error.detail = std::string(bank.legacyPath) + ": " + error.detail;
                issues.push_back(std::move(error));
            }
        };
        for (const auto& bank : coreBanks(context.board)) inspect(bank);
        for (const auto* bank :
            {&language->text, &language->graphics, &language->dialog})
            inspect(*bank);

        if (issues.empty())
        {
            ResourceRuntime candidate;
            const auto initialized = candidate.initialize(paths, context, options);
            if (!initialized) issues.push_back(initialized.error());
        }
        return issues;
    }

    ResourceSnapshot::ResourceSnapshot(
        ResourcePaths paths, ResourceContext context)
        : paths_(std::move(paths)),
          context_(context),
          banks_(std::make_shared<DataBankRegistry>()),
          data_(banks_)
    {
    }


    const DataSource& ResourceSnapshot::data() const noexcept
    {
        return *data_;
    }


    const DataBankRegistry& ResourceSnapshot::banks() const noexcept
    {
        return *banks_;
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
        ResourcePaths paths,
        ResourceContext context,
        ArchiveOpenOptions options,
        std::span<const DataSourceOverride> overrides)
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
            auto archive = staged->banks_->mount(
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
        auto selected = staged->language_.select(*staged->banks_, context.language);
        if (!selected)
        {
            return std::unexpected(selected.error());
        }

        // The gameplay DATA surface may now be layered with modern loose or
        // generated payloads while the untouched IDs continue through retail
        // DAT. LanguageService still owns legacy archive snapshots, so keep
        // groups 5/9/10 on that single backend until its catalog is migrated.
        for (const auto& replacement : overrides)
        {
            const auto group = dataGroup(replacement.id);
            if (group == legacyGroupValue(LegacyGroupId::LanguageGraphics) ||
                group == legacyGroupValue(LegacyGroupId::LanguageText) ||
                group == legacyGroupValue(LegacyGroupId::LanguageDialog))
            {
                return std::unexpected(DataError{
                    DataErrorCode::InvalidGroup,
                    {},
                    dataTag(replacement.id),
                    "language DATA overrides require the language catalog backend migration"
                });
            }
        }

        if (!overrides.empty())
        {
            auto layered = LayeredDataSource::create(staged->data_, overrides);
            if (!layered)
            {
                return std::unexpected(layered.error());
            }
            staged->data_ = std::move(*layered);
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
