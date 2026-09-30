#include "LayeredDataSource.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>

namespace monopoly::data
{
    namespace
    {
        [[nodiscard]] DataError itemError(
            DataErrorCode code,
            DataId id,
            std::string detail)
        {
            return {
                code,
                {},
                dataTag(id),
                std::move(detail)
            };
        }


        [[nodiscard]] std::expected<std::uint32_t, DataError> checkedSize(
            DataId id,
            const SharedDataBytes& bytes)
        {
            if (!bytes)
            {
                return std::unexpected(itemError(
                    DataErrorCode::InvalidItemRange,
                    id,
                    "DATA override payload is null"));
            }

            if (bytes->size() >
                static_cast<std::size_t>(
                    std::numeric_limits<std::uint32_t>::max()))
            {
                return std::unexpected(itemError(
                    DataErrorCode::InvalidItemRange,
                    id,
                    "DATA override payload exceeds the 32-bit runtime size"));
            }

            return static_cast<std::uint32_t>(bytes->size());
        }
    }


    LayeredDataSource::LayeredDataSource(
        std::shared_ptr<const DataSource> fallback)
        : fallback_(std::move(fallback))
    {
    }


    std::expected<
        std::shared_ptr<const LayeredDataSource>,
        DataError>
    LayeredDataSource::create(
        std::shared_ptr<const DataSource> fallback,
        std::span<const DataSourceOverride> overrides)
    {
        auto result = std::shared_ptr<LayeredDataSource>(
            new LayeredDataSource(std::move(fallback)));

        for (const auto& replacement : overrides)
        {
            if (isEmptyDataId(replacement.id))
            {
                return std::unexpected(itemError(
                    DataErrorCode::EmptyItem,
                    replacement.id,
                    "DATA override cannot replace EmptyDataId"));
            }

            if (replacement.type == LegacyDataType::Unknown)
            {
                return std::unexpected(itemError(
                    DataErrorCode::InvalidItemType,
                    replacement.id,
                    "DATA override requires an explicit runtime type"));
            }

            const auto size = checkedSize(
                replacement.id,
                replacement.bytes);
            if (!size)
            {
                return std::unexpected(size.error());
            }

            const auto [_, inserted] = result->overrides_.emplace(
                replacement.id,
                Item{replacement.type, replacement.bytes});
            if (!inserted)
            {
                return std::unexpected(itemError(
                    DataErrorCode::DuplicateDataId,
                    replacement.id,
                    "DATA override contains the same logical DataId twice"));
            }
        }

        return std::shared_ptr<const LayeredDataSource>(
            std::move(result));
    }


    std::size_t LayeredDataSource::overrideCount() const noexcept
    {
        return overrides_.size();
    }


    bool LayeredDataSource::overrides(DataId id) const noexcept
    {
        return overrides_.contains(id);
    }


    const LayeredDataSource::Item* LayeredDataSource::find(
        DataId id) const noexcept
    {
        const auto item = overrides_.find(id);
        return item == overrides_.end() ? nullptr : &item->second;
    }


    DataError LayeredDataSource::missing(DataId id) const
    {
        return itemError(
            DataErrorCode::ResourceNotFound,
            id,
            "logical DATA item is absent from the layered source");
    }


    std::expected<ArchiveItemMetadata, DataError>
    LayeredDataSource::metadata(DataId id) const
    {
        if (const auto* item = find(id))
        {
            const auto size = checkedSize(id, item->bytes);
            if (!size) return std::unexpected(size.error());
            return ArchiveItemMetadata{
                dataTag(id),
                item->type,
                0,
                *size,
                *size
            };
        }

        if (fallback_) return fallback_->metadata(id);
        return std::unexpected(missing(id));
    }


    std::expected<LegacyDataType, DataError>
    LayeredDataSource::initialDataType(DataId id) const
    {
        if (const auto* item = find(id)) return item->type;
        if (fallback_) return fallback_->initialDataType(id);
        return std::unexpected(missing(id));
    }


    std::expected<std::uint32_t, DataError>
    LayeredDataSource::initialSize(DataId id) const
    {
        if (const auto* item = find(id))
            return checkedSize(id, item->bytes);
        if (fallback_) return fallback_->initialSize(id);
        return std::unexpected(missing(id));
    }


    std::expected<std::uint32_t, DataError>
    LayeredDataSource::loadedRawSize(DataId id) const
    {
        if (const auto* item = find(id))
            return checkedSize(id, item->bytes);
        if (fallback_) return fallback_->loadedRawSize(id);
        return std::unexpected(missing(id));
    }


    std::expected<SharedDataBytes, DataError>
    LayeredDataSource::load(DataId id) const
    {
        if (const auto* item = find(id)) return item->bytes;
        if (fallback_) return fallback_->load(id);
        return std::unexpected(missing(id));
    }


    std::expected<std::size_t, DataError>
    LayeredDataSource::readRaw(
        DataId id,
        std::span<std::byte> destination,
        std::uint32_t startOffset) const
    {
        if (const auto* item = find(id))
        {
            const auto size = item->bytes->size();
            const auto start = static_cast<std::size_t>(startOffset);
            if (destination.empty() || start >= size)
                return std::size_t{0};

            const auto amount = std::min(
                destination.size(),
                size - start);
            std::memcpy(
                destination.data(),
                item->bytes->data() + start,
                amount);
            return amount;
        }

        if (fallback_)
            return fallback_->readRaw(
                id,
                destination,
                startOffset);
        return std::unexpected(missing(id));
    }


    std::expected<bool, DataError>
    LayeredDataSource::isLoaded(DataId id) const
    {
        if (find(id)) return true;
        if (fallback_) return fallback_->isLoaded(id);
        return std::unexpected(missing(id));
    }
}
