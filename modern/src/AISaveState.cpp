#include "AISaveState.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <utility>

namespace monopoly::ai::save
{
    namespace
    {
        constexpr std::array<std::uint8_t, 4> Magic{'A', 'I', 'S', 'V'};
        constexpr std::uint32_t Version = 1;

        struct Writer
        {
            std::vector<std::uint8_t> data{};

            void u8(std::uint8_t value)
            {
                data.push_back(value);
            }

            void u32(std::uint32_t value)
            {
                for (int shift = 0; shift < 32; shift += 8)
                    data.push_back(static_cast<std::uint8_t>(value >> shift));
            }

            void u64(std::uint64_t value)
            {
                for (int shift = 0; shift < 64; shift += 8)
                    data.push_back(static_cast<std::uint8_t>(value >> shift));
            }

            void i32(std::int32_t value)
            {
                u32(std::bit_cast<std::uint32_t>(value));
            }

            void i64(std::int64_t value)
            {
                u64(std::bit_cast<std::uint64_t>(value));
            }

            void f64(double value)
            {
                u64(std::bit_cast<std::uint64_t>(value));
            }
        };

        struct Reader
        {
            std::span<const std::uint8_t> data{};
            std::size_t offset{};

            bool u8(std::uint8_t& value) noexcept
            {
                if (offset >= data.size())
                    return false;
                value = data[offset++];
                return true;
            }

            bool u32(std::uint32_t& value) noexcept
            {
                if (data.size() - offset < 4)
                    return false;
                value = 0;
                for (int shift = 0; shift < 32; shift += 8)
                    value |= static_cast<std::uint32_t>(data[offset++]) << shift;
                return true;
            }

            bool u64(std::uint64_t& value) noexcept
            {
                if (data.size() - offset < 8)
                    return false;
                value = 0;
                for (int shift = 0; shift < 64; shift += 8)
                    value |= static_cast<std::uint64_t>(data[offset++]) << shift;
                return true;
            }

            bool i32(std::int32_t& value) noexcept
            {
                std::uint32_t raw{};
                if (!u32(raw))
                    return false;
                value = std::bit_cast<std::int32_t>(raw);
                return true;
            }

            bool i64(std::int64_t& value) noexcept
            {
                std::uint64_t raw{};
                if (!u64(raw))
                    return false;
                value = std::bit_cast<std::int64_t>(raw);
                return true;
            }

            bool f64(double& value) noexcept
            {
                std::uint64_t raw{};
                if (!u64(raw))
                    return false;
                value = std::bit_cast<double>(raw);
                return true;
            }

            [[nodiscard]] bool finished() const noexcept
            {
                return offset == data.size();
            }
        };

        template <std::size_t N>
        void writeDoubles(
            Writer& writer,
            const std::array<double, N>& values)
        {
            for (double value : values)
                writer.f64(value);
        }

        template <std::size_t N>
        bool readDoubles(
            Reader& reader,
            std::array<double, N>& values) noexcept
        {
            for (double& value : values)
            {
                if (!reader.f64(value))
                    return false;
            }
            return true;
        }

        [[nodiscard]] bool validClassification(std::uint8_t value) noexcept
        {
            return value <= 6 || value == 10 || value == 11;
        }

        bool writeProfile(Writer& writer, const profile::Profile& value)
        {
            if (value.maxTrades > std::numeric_limits<std::uint32_t>::max())
                return false;

            writeDoubles(writer, value.playerAttitude);
            writer.f64(value.attitudeLostForRejectedTrade);
            writer.f64(value.attitudeChangeTradeObserving);
            writer.f64(value.attitudeTickChange);
            writer.f64(value.neutralAttitude);
            writer.i64(value.timeForgetTrade);
            writer.u32(static_cast<std::uint32_t>(value.maxTrades));
            writer.i32(value.tradeCounterLimit);
            writer.f64(value.tradeCounterProbability);
            writer.i64(value.minCashOnHand);
            writer.u8(static_cast<std::uint8_t>(value.cashStrategy));
            writer.u8(value.housingPurchaseStrategy);
            writer.f64(value.buyingStageCashMultiplier);
            writer.f64(value.noMonopolyStageCashMultiplier);
            writer.f64(value.cashLiquidAssetsDependence);
            writer.f64(value.monopolyNotOwnedStageCashMultiplier);
            writer.f64(value.monopolyOwnedStageCashMultiplier);
            writer.f64(value.chancesFactor);
            writer.f64(value.chancesThreshold);
            writer.f64(value.cashFactor);
            writeDoubles(writer, value.worthFactors.property);
            writeDoubles(writer, value.worthFactors.cashCow);
            writer.f64(value.tradeImportanceFactor);
            writer.f64(value.minEvaluationThreshold);
            writer.f64(value.minGiveMonopolyEvaluation);

            const auto& importance = value.propertyImportance;
            writer.f64(importance.monopolyReceivedImportance);
            writer.f64(importance.givingMonopolyImportance);
            writer.f64(importance.negativePropertyImportanceChangeMultiplier);
            writer.f64(importance.propertyAllowTradeImportance);
            writer.f64(importance.propertyAllowMoreTradeImportance);
            writer.f64(importance.propertyOneUnownedImportance);
            writer.f64(importance.propertyTwoUnownedImportance);
            writeDoubles(writer, importance.railroadImportance);
            writeDoubles(writer, importance.utilityImportance);
            writeDoubles(writer, importance.monopolyVetoImportance);
            writer.f64(value.balticReceivedImportance);
            writer.f64(value.proposeTradeProbability);
            writer.f64(value.monopolyTradeProbability);
            writer.f64(value.proposeMonopolyGiveAwayProbability);
            writer.i32(value.maxHousesPerSquareForGiveAway);
            writer.i32(value.minActionWait);
            writer.i32(value.maxActionWait);
            writer.i32(value.waitForTradeConsider);
            writer.u64(value.patience);
            writer.i32(value.numberTimesAllowPropertyTrade);
            writer.i32(value.turnsToForgetPropertyTrade);
            writer.f64(value.minEvaluationIfFedUp);
            writer.f64(value.monopolySuicideFactor);
            writer.f64(value.lowestPropertyImportanceForCounter);
            writer.f64(value.minimumNonmonopolyTradeAttitude);

            for (const auto& trade : value.whatToTrade)
            {
                writer.u8(static_cast<std::uint8_t>(trade.giveMonopoly));
                writer.u8(trade.giveGroupTrades);
                writer.u8(trade.giveCashCows);
                writer.u8(trade.giveJunk);
                writer.f64(trade.cashMultiplier);
            }

            return true;
        }

        bool readProfile(Reader& reader, profile::Profile& value) noexcept
        {
            std::uint32_t maxTrades{};
            std::int32_t intValue{};
            std::uint8_t enumValue{};

            if (!readDoubles(reader, value.playerAttitude) ||
                !reader.f64(value.attitudeLostForRejectedTrade) ||
                !reader.f64(value.attitudeChangeTradeObserving) ||
                !reader.f64(value.attitudeTickChange) ||
                !reader.f64(value.neutralAttitude) ||
                !reader.i64(value.timeForgetTrade) ||
                !reader.u32(maxTrades) ||
                !reader.i32(intValue))
                return false;

            value.maxTrades = maxTrades;
            value.tradeCounterLimit = intValue;

            if (!reader.f64(value.tradeCounterProbability) ||
                !reader.i64(value.minCashOnHand) ||
                !reader.u8(enumValue) ||
                enumValue >= static_cast<std::uint8_t>(decision::CashStrategy::Count))
                return false;
            value.cashStrategy = static_cast<decision::CashStrategy>(enumValue);

            if (!reader.u8(value.housingPurchaseStrategy) ||
                !reader.f64(value.buyingStageCashMultiplier) ||
                !reader.f64(value.noMonopolyStageCashMultiplier) ||
                !reader.f64(value.cashLiquidAssetsDependence) ||
                !reader.f64(value.monopolyNotOwnedStageCashMultiplier) ||
                !reader.f64(value.monopolyOwnedStageCashMultiplier) ||
                !reader.f64(value.chancesFactor) ||
                !reader.f64(value.chancesThreshold) ||
                !reader.f64(value.cashFactor) ||
                !readDoubles(reader, value.worthFactors.property) ||
                !readDoubles(reader, value.worthFactors.cashCow) ||
                !reader.f64(value.tradeImportanceFactor) ||
                !reader.f64(value.minEvaluationThreshold) ||
                !reader.f64(value.minGiveMonopolyEvaluation))
                return false;

            auto& importance = value.propertyImportance;
            if (!reader.f64(importance.monopolyReceivedImportance) ||
                !reader.f64(importance.givingMonopolyImportance) ||
                !reader.f64(importance.negativePropertyImportanceChangeMultiplier) ||
                !reader.f64(importance.propertyAllowTradeImportance) ||
                !reader.f64(importance.propertyAllowMoreTradeImportance) ||
                !reader.f64(importance.propertyOneUnownedImportance) ||
                !reader.f64(importance.propertyTwoUnownedImportance) ||
                !readDoubles(reader, importance.railroadImportance) ||
                !readDoubles(reader, importance.utilityImportance) ||
                !readDoubles(reader, importance.monopolyVetoImportance) ||
                !reader.f64(value.balticReceivedImportance) ||
                !reader.f64(value.proposeTradeProbability) ||
                !reader.f64(value.monopolyTradeProbability) ||
                !reader.f64(value.proposeMonopolyGiveAwayProbability))
                return false;

            if (!reader.i32(intValue))
                return false;
            value.maxHousesPerSquareForGiveAway = intValue;
            if (!reader.i32(intValue))
                return false;
            value.minActionWait = intValue;
            if (!reader.i32(intValue))
                return false;
            value.maxActionWait = intValue;

            if (!reader.i32(intValue))
                return false;
            value.waitForTradeConsider = intValue;
            if (!reader.u64(value.patience))
                return false;
            if (!reader.i32(intValue))
                return false;
            value.numberTimesAllowPropertyTrade = intValue;
            if (!reader.i32(intValue))
                return false;
            value.turnsToForgetPropertyTrade = intValue;

            if (!reader.f64(value.minEvaluationIfFedUp) ||
                !reader.f64(value.monopolySuicideFactor) ||
                !reader.f64(value.lowestPropertyImportanceForCounter) ||
                !reader.f64(value.minimumNonmonopolyTradeAttitude))
                return false;

            for (auto& trade : value.whatToTrade)
            {
                if (!reader.u8(enumValue) || !validClassification(enumValue))
                    return false;
                trade.giveMonopoly =
                    static_cast<trade::PropertyClassification>(enumValue);
                if (!reader.u8(trade.giveGroupTrades) ||
                    !reader.u8(trade.giveCashCows) ||
                    !reader.u8(trade.giveJunk) ||
                    !reader.f64(trade.cashMultiplier))
                    return false;
            }

            return true;
        }
    }

    bool encode(
        const State& state,
        std::vector<std::uint8_t>& result) noexcept
    {
        try
        {
            Writer payload{};
            payload.u32(Version);
            if (!writeProfile(payload, state.profile))
                return false;
            for (std::int64_t timer : state.timeLastTrade)
                payload.i64(timer);

            if (payload.data.size() >
                std::numeric_limits<std::uint32_t>::max())
                return false;

            Writer chunk{};
            for (std::uint8_t byte : Magic)
                chunk.u8(byte);
            chunk.u32(static_cast<std::uint32_t>(payload.data.size()));
            chunk.data.insert(
                chunk.data.end(), payload.data.begin(), payload.data.end());
            result = std::move(chunk.data);
            return true;
        }
        catch (...)
        {
            result.clear();
            return false;
        }
    }

    bool decode(
        std::span<const std::uint8_t> data,
        State& result) noexcept
    {
        if (data.size() < 12 ||
            !std::equal(Magic.begin(), Magic.end(), data.begin()))
            return false;

        Reader header{data.subspan(4, 4)};
        std::uint32_t payloadSize{};
        if (!header.u32(payloadSize) ||
            payloadSize != data.size() - 8)
            return false;

        Reader reader{data.subspan(8)};
        std::uint32_t version{};
        if (!reader.u32(version) || version != Version)
            return false;

        State decoded{};
        if (!readProfile(reader, decoded.profile))
            return false;
        for (std::int64_t& timer : decoded.timeLastTrade)
        {
            if (!reader.i64(timer))
                return false;
        }
        if (!reader.finished())
            return false;

        result = std::move(decoded);
        return true;
    }
}
