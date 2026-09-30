#pragma once

#include "LegacyDataArchive.hpp"

#include <memory>
#include <span>
#include <unordered_map>

namespace monopoly::data
{
    struct DataSourceOverride
    {
        DataId id{ EmptyDataId };
        LegacyDataType type{ LegacyDataType::Unknown };
        SharedDataBytes bytes;
    };


    // Immutable logical DATA overlay. Modern resources can replace individual
    // DataIds while every non-overridden request falls back to the retail DAT
    // source. This keeps the game playable during incremental asset migration.
    class LayeredDataSource final : public DataSource
    {
    public:
        [[nodiscard]] static std::expected<
            std::shared_ptr<const LayeredDataSource>,
            DataError>
        create(
            std::shared_ptr<const DataSource> fallback,
            std::span<const DataSourceOverride> overrides);

        [[nodiscard]] std::size_t overrideCount() const noexcept;
        [[nodiscard]] bool overrides(DataId id) const noexcept;

        [[nodiscard]] std::expected<ArchiveItemMetadata, DataError>
        metadata(DataId id) const override;

        [[nodiscard]] std::expected<LegacyDataType, DataError>
        initialDataType(DataId id) const override;

        [[nodiscard]] std::expected<std::uint32_t, DataError>
        initialSize(DataId id) const override;

        [[nodiscard]] std::expected<std::uint32_t, DataError>
        loadedRawSize(DataId id) const override;

        [[nodiscard]] std::expected<SharedDataBytes, DataError>
        load(DataId id) const override;

        [[nodiscard]] std::expected<std::size_t, DataError>
        readRaw(
            DataId id,
            std::span<std::byte> destination,
            std::uint32_t startOffset = 0) const override;

        [[nodiscard]] std::expected<bool, DataError>
        isLoaded(DataId id) const override;

    private:
        struct Item
        {
            LegacyDataType type{ LegacyDataType::Unknown };
            SharedDataBytes bytes;
        };

        explicit LayeredDataSource(std::shared_ptr<const DataSource> fallback);

        [[nodiscard]] const Item* find(DataId id) const noexcept;
        [[nodiscard]] DataError missing(DataId id) const;

        std::shared_ptr<const DataSource> fallback_;
        std::unordered_map<DataId, Item> overrides_;
    };
}
