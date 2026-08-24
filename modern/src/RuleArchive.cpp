#include "RuleArchive.hpp"
#include "RuleOptions.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace monopoly::rules::archive
{
    namespace
    {
        constexpr std::array<std::uint8_t, 8>
            SaveMagic{
                'M', 'O', 'N', 'O',
                '9', '9', 'S', 'V'
            };

        constexpr std::array<std::uint8_t, 8>
            OptionsMagic{
                'M', 'O', 'N', 'O',
                '9', '9', 'O', 'P'
            };

        constexpr std::uint32_t FormatVersion = 2;


        class Writer
        {
        public:
            void u8(std::uint8_t value)
            {
                data_.push_back(value);
            }


            void u32(std::uint32_t value)
            {
                for (int shift = 0;
                     shift < 32;
                     shift += 8)
                {
                    u8(
                        static_cast<std::uint8_t>(
                            value >> shift
                        )
                    );
                }
            }


            void u64(std::uint64_t value)
            {
                for (int shift = 0;
                     shift < 64;
                     shift += 8)
                {
                    u8(
                        static_cast<std::uint8_t>(
                            value >> shift
                        )
                    );
                }
            }


            void i64(std::int64_t value)
            {
                u64(
                    static_cast<std::uint64_t>(
                        value
                    )
                );
            }


            void boolean(bool value)
            {
                u8(value ? 1 : 0);
            }


            void raw(
                std::span<const std::uint8_t> data)
            {
                data_.insert(
                    data_.end(),
                    data.begin(),
                    data.end()
                );
            }


            void blob(
                const std::vector<std::uint8_t>& data)
            {
                u32(
                    static_cast<std::uint32_t>(
                        data.size()
                    )
                );

                raw(data);
            }


            void text(
                const std::wstring& value)
            {
                u32(
                    static_cast<std::uint32_t>(
                        value.size()
                    )
                );

                // On enregistre les unités wchar_t sous forme
                // d'entiers 32 bits.
                //
                // Cela évite de dépendre de la taille du wchar_t
                // ou d'une page de codes Windows.

                for (wchar_t ch : value)
                {
                    u32(
                        static_cast<std::uint32_t>(
                            ch
                        )
                    );
                }
            }


            const std::vector<std::uint8_t>& data() const
            {
                return data_;
            }


            std::vector<std::uint8_t> take()
            {
                return std::move(data_);
            }

        private:
            std::vector<std::uint8_t> data_;
        };


        class Reader
        {
        public:
            explicit Reader(
                std::span<const std::uint8_t> data)
                : data_(data)
            {
            }


            bool u8(std::uint8_t& value)
            {
                if (position_ >= data_.size())
                {
                    return false;
                }

                value =
                    data_[position_++];

                return true;
            }


            bool u32(std::uint32_t& value)
            {
                value = 0;

                for (int shift = 0;
                     shift < 32;
                     shift += 8)
                {
                    std::uint8_t byte = 0;

                    if (!u8(byte))
                    {
                        return false;
                    }

                    value |=
                        static_cast<std::uint32_t>(
                            byte
                        ) << shift;
                }

                return true;
            }


            bool u64(std::uint64_t& value)
            {
                value = 0;

                for (int shift = 0;
                     shift < 64;
                     shift += 8)
                {
                    std::uint8_t byte = 0;

                    if (!u8(byte))
                    {
                        return false;
                    }

                    value |=
                        static_cast<std::uint64_t>(
                            byte
                        ) << shift;
                }

                return true;
            }


            bool i64(std::int64_t& value)
            {
                std::uint64_t rawValue = 0;

                if (!u64(rawValue))
                {
                    return false;
                }

                value =
                    static_cast<std::int64_t>(
                        rawValue
                    );

                return true;
            }


            bool boolean(bool& value)
            {
                std::uint8_t rawValue = 0;

                if (!u8(rawValue) ||
                    rawValue > 1)
                {
                    return false;
                }

                value =
                    rawValue != 0;

                return true;
            }


            bool raw(
                std::size_t size,
                std::vector<std::uint8_t>& result)
            {
                if (
                    size >
                    data_.size() - position_)
                {
                    return false;
                }

                result.assign(
                    data_.begin() +
                        static_cast<std::ptrdiff_t>(
                            position_
                        ),

                    data_.begin() +
                        static_cast<std::ptrdiff_t>(
                            position_ + size
                        )
                );

                position_ += size;

                return true;
            }


            bool blob(
                std::vector<std::uint8_t>& result)
            {
                std::uint32_t size = 0;

                if (!u32(size))
                {
                    return false;
                }

                return raw(
                    size,
                    result
                );
            }


            bool text(
                std::wstring& result)
            {
                std::uint32_t size = 0;

                if (!u32(size) ||
                    size > 4096)
                {
                    return false;
                }

                result.clear();
                result.reserve(size);

                for (std::uint32_t i = 0;
                     i < size;
                     ++i)
                {
                    std::uint32_t ch = 0;

                    if (!u32(ch))
                    {
                        return false;
                    }

                    result.push_back(
                        static_cast<wchar_t>(
                            ch
                        )
                    );
                }

                return true;
            }


            bool finished() const
            {
                return
                    position_ ==
                    data_.size();
            }

        private:
            std::span<const std::uint8_t>
                data_;

            std::size_t position_ = 0;
        };


        template<typename T>
        void writeNumber(
            Writer& writer,
            T value)
        {
            writer.i64(
                static_cast<std::int64_t>(
                    value
                )
            );
        }


        template<typename T>
        bool readNumber(
            Reader& reader,
            T& value)
        {
            std::int64_t rawValue = 0;

            if (!reader.i64(rawValue))
            {
                return false;
            }

            value =
                static_cast<T>(
                    rawValue
                );

            return true;
        }


        std::uint64_t fnv1a(
            std::span<const std::uint8_t> data)
        {
            std::uint64_t hash =
                14695981039346656037ull;

            for (const std::uint8_t byte :
                 data)
            {
                hash ^= byte;

                hash *=
                    1099511628211ull;
            }

            return hash;
        }


        void writeOptionsPayload(
            Writer& writer,
            const GameOptions& options)
        {
#define WRITE_OPTION(field) \
            writeNumber(writer, options.field)

#define WRITE_BOOL_OPTION(field) \
            writer.boolean(options.field)

            WRITE_OPTION(housesPerHotel);
            WRITE_OPTION(maximumHouses);
            WRITE_OPTION(maximumHotels);

            WRITE_OPTION(interestRate);
            WRITE_OPTION(initialCash);
            WRITE_OPTION(passingGoAmount);
            WRITE_OPTION(luxuryTaxAmount);
            WRITE_OPTION(taxRate);
            WRITE_OPTION(flatTaxFee);

            WRITE_OPTION(freeParkingSeed);

            WRITE_BOOL_OPTION(freeParkingPot);
            WRITE_BOOL_OPTION(doubleSalaryOnGo);
            WRITE_BOOL_OPTION(hideCash);
            WRITE_BOOL_OPTION(evenBuildRule);

            WRITE_BOOL_OPTION(
                rollDiceToDecideStartingOrder
            );

            WRITE_BOOL_OPTION(cheatingAllowed);

            WRITE_BOOL_OPTION(
                aiTakesTimeToThink
            );

            WRITE_BOOL_OPTION(
                futureRentTradingAllowed
            );

            WRITE_BOOL_OPTION(
                immunitiesTradingAllowed
            );

            WRITE_BOOL_OPTION(
                allowPlayersToTakeOverAIs
            );

            WRITE_BOOL_OPTION(
                dealFreePropertiesAtStartup
            );

            WRITE_OPTION(
                dealNPropertiesAtStartup
            );

            WRITE_OPTION(
                stopAtNthBankruptcy
            );

            WRITE_OPTION(
                maximumTurnsInJail
            );

            WRITE_OPTION(
                getOutOfJailFee
            );

            WRITE_BOOL_OPTION(
                mortgagedCountsInGroupRent
            );

            WRITE_OPTION(
                houseShortageLevel
            );

            WRITE_OPTION(
                hotelShortageLevel
            );

            WRITE_OPTION(
                auctionGoingTimeDelay
            );

            WRITE_OPTION(
                inactivityWarningTime
            );

            WRITE_OPTION(
                gameOverTimeLimit
            );

            writeNumber(
                writer,
                options.voiceChat.recordingHz
            );

            writeNumber(
                writer,
                options.voiceChat.recordingBits
            );

            writer.text(
                options.voiceChat.compressorName
            );

#undef WRITE_OPTION
#undef WRITE_BOOL_OPTION
        }


        bool readOptionsPayload(
            Reader& reader,
            GameOptions& options)
        {
#define READ_OPTION(field) \
            if (!readNumber(reader, options.field)) return false

#define READ_BOOL_OPTION(field) \
            if (!reader.boolean(options.field)) return false

            READ_OPTION(housesPerHotel);
            READ_OPTION(maximumHouses);
            READ_OPTION(maximumHotels);

            READ_OPTION(interestRate);
            READ_OPTION(initialCash);
            READ_OPTION(passingGoAmount);
            READ_OPTION(luxuryTaxAmount);
            READ_OPTION(taxRate);
            READ_OPTION(flatTaxFee);

            READ_OPTION(freeParkingSeed);

            READ_BOOL_OPTION(freeParkingPot);
            READ_BOOL_OPTION(doubleSalaryOnGo);
            READ_BOOL_OPTION(hideCash);
            READ_BOOL_OPTION(evenBuildRule);

            READ_BOOL_OPTION(
                rollDiceToDecideStartingOrder
            );

            READ_BOOL_OPTION(cheatingAllowed);

            READ_BOOL_OPTION(
                aiTakesTimeToThink
            );

            READ_BOOL_OPTION(
                futureRentTradingAllowed
            );

            READ_BOOL_OPTION(
                immunitiesTradingAllowed
            );

            READ_BOOL_OPTION(
                allowPlayersToTakeOverAIs
            );

            READ_BOOL_OPTION(
                dealFreePropertiesAtStartup
            );

            READ_OPTION(
                dealNPropertiesAtStartup
            );

            READ_OPTION(
                stopAtNthBankruptcy
            );

            READ_OPTION(
                maximumTurnsInJail
            );

            READ_OPTION(
                getOutOfJailFee
            );

            READ_BOOL_OPTION(
                mortgagedCountsInGroupRent
            );

            READ_OPTION(
                houseShortageLevel
            );

            READ_OPTION(
                hotelShortageLevel
            );

            READ_OPTION(
                auctionGoingTimeDelay
            );

            READ_OPTION(
                inactivityWarningTime
            );

            READ_OPTION(
                gameOverTimeLimit
            );

            if (
                !readNumber(
                    reader,
                    options.voiceChat.recordingHz
                ) ||
                !readNumber(
                    reader,
                    options.voiceChat.recordingBits
                ) ||
                !reader.text(
                    options.voiceChat.compressorName
                ))
            {
                return false;
            }

#undef READ_OPTION
#undef READ_BOOL_OPTION

            return true;
        }


        void writeStatePayload(
            Writer& writer,
            const GameState& state)
        {
            writeOptionsPayload(
                writer,
                state.options
            );


            // ------------------------------------------------
            // Identité de la partie.
            // ------------------------------------------------

            writeNumber(
                writer,
                state.numberOfPlayers
            );

            writeNumber(
                writer,
                state.currentPlayer
            );


            // ------------------------------------------------
            // Joueurs.
            // ------------------------------------------------

            for (const PlayerState& player :
                 state.players)
            {
                writer.text(player.name);

                writeNumber(
                    writer,
                    player.token
                );

                writeNumber(
                    writer,
                    player.colour
                );

                writeNumber(
                    writer,
                    player.aiPlayerLevel
                );

                writeNumber(
                    writer,
                    player.cash
                );

                writeNumber(
                    writer,
                    player.timeOfLastActivity
                );

                writeNumber(
                    writer,
                    player.currentSquare
                );

                writer.boolean(
                    player.firstMoveMade
                );

                writeNumber(
                    writer,
                    player.turnsInJail
                );

                writeNumber(
                    writer,
                    player.inactivityCount
                );

                writer.boolean(
                    player.acceptedConfiguration
                );

                writeNumber(
                    writer,
                    player.diceRollHistory
                );

                for (const auto amount :
                     player.cashGivenInTrade)
                {
                    writeNumber(
                        writer,
                        amount
                    );
                }

                writer.boolean(
                    player.tradeAccepted
                );
            }


            // ------------------------------------------------
            // Plateau.
            // ------------------------------------------------

            for (const SquareState& square :
                 state.squares)
            {
                writeNumber(
                    writer,
                    square.owner
                );

                writeNumber(
                    writer,
                    square.offeredInTradeTo
                );

                writeNumber(
                    writer,
                    square.houses
                );

                writer.boolean(
                    square.mortgaged
                );

                writeNumber(
                    writer,
                    square.gameEarnings
                );
            }


            // ------------------------------------------------
            // Decks.
            // ------------------------------------------------

            for (const CardDeck& deck :
                 state.cards)
            {
                writeNumber(
                    writer,
                    deck.cardCount
                );

                for (const auto card :
                     deck.cardPile)
                {
                    writeNumber(
                        writer,
                        card
                    );
                }

                writeNumber(
                    writer,
                    deck.jailOwner
                );

                writeNumber(
                    writer,
                    deck.jailOfferedInTradeTo
                );
            }


            // ------------------------------------------------
            // Dés / carte pending.
            // ------------------------------------------------

            for (const auto die :
                 state.dice)
            {
                writeNumber(writer, die);
            }

            for (const auto die :
                 state.nextDice)
            {
                writeNumber(writer, die);
            }

            for (const auto die :
                 state.utilityDice)
            {
                writeNumber(writer, die);
            }

            writeNumber(
                writer,
                state.numberOfDoublesRolled
            );

            writer.boolean(
                state.justRolledOutOfJail
            );

            writeNumber(
                writer,
                state.pendingCard
            );


            // ------------------------------------------------
            // Horloge / jackpot.
            // ------------------------------------------------

            writeNumber(
                writer,
                state.gameDurationInSeconds
            );

            writeNumber(
                writer,
                state.freeParkingJackpotAmount
            );

            writeNumber(
                writer,
                state.configurationProposer
            );


            // ------------------------------------------------
            // Auction shared state.
            // ------------------------------------------------

            writeNumber(
                writer,
                state.auction.tickCount
            );

            writeNumber(
                writer,
                state.auction.goingCount
            );

            writeNumber(
                writer,
                state.auction.highestBidder
            );

            writeNumber(
                writer,
                state.auction.highestBid
            );

            writeNumber(
                writer,
                state.auction
                    .propertyBeingAuctioned
            );


            // ------------------------------------------------
            // Trade + futures / immunités.
            // ------------------------------------------------

            writer.boolean(
                state.tradeInProgress
            );

            for (const CountHitRecord& hit :
                 state.countHits)
            {
                writeNumber(
                    writer,
                    hit.properties
                );

                writeNumber(
                    writer,
                    hit.fromPlayer
                );

                writeNumber(
                    writer,
                    hit.toPlayer
                );

                writeNumber(
                    writer,
                    hit.hitType
                );

                writer.boolean(
                    hit.tradedItem
                );

                writeNumber(
                    writer,
                    hit.hitCount
                );
            }


            // ------------------------------------------------
            // Phase stack.
            // ------------------------------------------------

            writeNumber(
                writer,
                state.numberOfPendingPhases
            );

            for (const PendingPhase& phase :
                 state.phaseStack)
            {
                writeNumber(
                    writer,
                    phase.phase
                );

                writeNumber(
                    writer,
                    phase.fromPlayer
                );

                writeNumber(
                    writer,
                    phase.toPlayer
                );

                writeNumber(
                    writer,
                    phase.amount
                );
            }
        }


        bool readStatePayload(
            Reader& reader,
            GameState& state)
        {
            if (
                !readOptionsPayload(
                    reader,
                    state.options
                ))
            {
                return false;
            }


            if (
                !readNumber(
                    reader,
                    state.numberOfPlayers
                ) ||
                state.numberOfPlayers >
                    MaxPlayers)
            {
                return false;
            }


            if (
                !readNumber(
                    reader,
                    state.currentPlayer
                ))
            {
                return false;
            }


            for (PlayerState& player :
                 state.players)
            {
                if (!reader.text(player.name))
                {
                    return false;
                }

                if (
                    !readNumber(reader, player.token) ||
                    !readNumber(reader, player.colour) ||
                    !readNumber(
                        reader,
                        player.aiPlayerLevel
                    ) ||
                    !readNumber(reader, player.cash) ||
                    !readNumber(
                        reader,
                        player.timeOfLastActivity
                    ) ||
                    !readNumber(
                        reader,
                        player.currentSquare
                    ) ||
                    !reader.boolean(
                        player.firstMoveMade
                    ) ||
                    !readNumber(
                        reader,
                        player.turnsInJail
                    ) ||
                    !readNumber(
                        reader,
                        player.inactivityCount
                    ) ||
                    !reader.boolean(
                        player.acceptedConfiguration
                    ) ||
                    !readNumber(
                        reader,
                        player.diceRollHistory
                    ))
                {
                    return false;
                }


                for (auto& amount :
                     player.cashGivenInTrade)
                {
                    if (
                        !readNumber(
                            reader,
                            amount
                        ))
                    {
                        return false;
                    }
                }


                if (
                    !reader.boolean(
                        player.tradeAccepted
                    ))
                {
                    return false;
                }
            }


            for (SquareState& square :
                 state.squares)
            {
                if (
                    !readNumber(
                        reader,
                        square.owner
                    ) ||
                    !readNumber(
                        reader,
                        square.offeredInTradeTo
                    ) ||
                    !readNumber(
                        reader,
                        square.houses
                    ) ||
                    !reader.boolean(
                        square.mortgaged
                    ) ||
                    !readNumber(
                        reader,
                        square.gameEarnings
                    ))
                {
                    return false;
                }
            }


            for (CardDeck& deck :
                 state.cards)
            {
                if (
                    !readNumber(
                        reader,
                        deck.cardCount
                    ) ||
                    deck.cardCount >
                        MaxCardsInDeck)
                {
                    return false;
                }


                for (auto& card :
                     deck.cardPile)
                {
                    if (
                        !readNumber(
                            reader,
                            card
                        ))
                    {
                        return false;
                    }
                }


                if (
                    !readNumber(
                        reader,
                        deck.jailOwner
                    ) ||
                    !readNumber(
                        reader,
                        deck.jailOfferedInTradeTo
                    ))
                {
                    return false;
                }
            }


            for (auto& die : state.dice)
            {
                if (!readNumber(reader, die))
                {
                    return false;
                }
            }

            for (auto& die : state.nextDice)
            {
                if (!readNumber(reader, die))
                {
                    return false;
                }
            }

            for (auto& die :
                 state.utilityDice)
            {
                if (!readNumber(reader, die))
                {
                    return false;
                }
            }


            if (
                !readNumber(
                    reader,
                    state.numberOfDoublesRolled
                ) ||
                !reader.boolean(
                    state.justRolledOutOfJail
                ) ||
                !readNumber(
                    reader,
                    state.pendingCard
                ) ||
                !readNumber(
                    reader,
                    state.gameDurationInSeconds
                ) ||
                !readNumber(
                    reader,
                    state.freeParkingJackpotAmount
                ) ||
                !readNumber(
                    reader,
                    state.configurationProposer
                ))
            {
                return false;
            }


            if (
                !readNumber(
                    reader,
                    state.auction.tickCount
                ) ||
                !readNumber(
                    reader,
                    state.auction.goingCount
                ) ||
                !readNumber(
                    reader,
                    state.auction.highestBidder
                ) ||
                !readNumber(
                    reader,
                    state.auction.highestBid
                ) ||
                !readNumber(
                    reader,
                    state.auction
                        .propertyBeingAuctioned
                ))
            {
                return false;
            }


            if (
                !reader.boolean(
                    state.tradeInProgress
                ))
            {
                return false;
            }


            for (CountHitRecord& hit :
                 state.countHits)
            {
                if (
                    !readNumber(
                        reader,
                        hit.properties
                    ) ||
                    !readNumber(
                        reader,
                        hit.fromPlayer
                    ) ||
                    !readNumber(
                        reader,
                        hit.toPlayer
                    ) ||
                    !readNumber(
                        reader,
                        hit.hitType
                    ) ||
                    !reader.boolean(
                        hit.tradedItem
                    ) ||
                    !readNumber(
                        reader,
                        hit.hitCount
                    ))
                {
                    return false;
                }
            }


            if (
                !readNumber(
                    reader,
                    state.numberOfPendingPhases
                ) ||
                state.numberOfPendingPhases >
                    state.phaseStack.size())
            {
                return false;
            }


            for (PendingPhase& phase :
                 state.phaseStack)
            {
                if (
                    !readNumber(
                        reader,
                        phase.phase
                    ) ||
                    !readNumber(
                        reader,
                        phase.fromPlayer
                    ) ||
                    !readNumber(
                        reader,
                        phase.toPlayer
                    ) ||
                    !readNumber(
                        reader,
                        phase.amount
                    ))
                {
                    return false;
                }
            }


            // ------------------------------------------------
            // Validation minimale.
            // ------------------------------------------------

            for (PlayerNumber player = 0;
                 player < state.numberOfPlayers;
                 ++player)
            {
                if (
                    state.players[player]
                        .currentSquare > 41)
                {
                    return false;
                }
            }


            return true;
        }


        bool makeEnvelope(
            const std::array<std::uint8_t, 8>& magic,
            std::span<const std::uint8_t> payload,
            std::vector<std::uint8_t>& result)
        {
            if (
                payload.size() >
                std::numeric_limits<
                    std::uint32_t
                >::max())
            {
                return false;
            }


            Writer writer;

            writer.raw(magic);

            writer.u32(
                FormatVersion
            );

            writer.u32(
                static_cast<std::uint32_t>(
                    payload.size()
                )
            );

            writer.u64(
                fnv1a(payload)
            );

            writer.raw(payload);


            result =
                writer.take();

            return true;
        }


        bool openEnvelope(
            std::span<const std::uint8_t> data,
            const std::array<std::uint8_t, 8>& magic,
            std::vector<std::uint8_t>& payload)
        {
            constexpr std::size_t HeaderSize =
                8 + 4 + 4 + 8;


            if (data.size() < HeaderSize)
            {
                return false;
            }


            for (std::size_t i = 0;
                 i < magic.size();
                 ++i)
            {
                if (data[i] != magic[i])
                {
                    return false;
                }
            }


            Reader reader(
                data.subspan(8)
            );


            std::uint32_t version = 0;
            std::uint32_t size = 0;
            std::uint64_t checksum = 0;


            if (
                !reader.u32(version) ||
                !reader.u32(size) ||
                !reader.u64(checksum) ||
                version != FormatVersion)
            {
                return false;
            }


            if (
                size !=
                data.size() - HeaderSize)
            {
                return false;
            }


            payload.assign(
                data.begin() +
                    static_cast<std::ptrdiff_t>(
                        HeaderSize
                    ),
                data.end()
            );


            return
                fnv1a(payload) ==
                checksum;
        }
    }


    bool encodeSave(
        const GameState& state,
        const AIStateArray& aiStates,
        std::vector<std::uint8_t>& result)
    {
        Writer payload;

        writeStatePayload(
            payload,
            state
        );


        for (const auto& aiState :
             aiStates)
        {
            payload.blob(aiState);
        }


        return makeEnvelope(
            SaveMagic,
            payload.data(),
            result
        );
    }


    bool decodeSave(
        std::span<const std::uint8_t> data,
        GameState& state,
        AIStateArray* aiStates)
    {
        std::vector<std::uint8_t> payload;

        if (
            !openEnvelope(
                data,
                SaveMagic,
                payload
            ))
        {
            return false;
        }


        Reader reader(payload);

        GameState decodedState{};


        if (
            !readStatePayload(
                reader,
                decodedState
            ))
        {
            return false;
        }


        options::validate(
            decodedState.options
        );


        AIStateArray decodedAIStates{};


        for (auto& aiState :
             decodedAIStates)
        {
            if (!reader.blob(aiState))
            {
                return false;
            }
        }


        if (!reader.finished())
        {
            return false;
        }


        state =
            std::move(decodedState);


        if (aiStates != nullptr)
        {
            *aiStates =
                std::move(decodedAIStates);
        }


        return true;
    }


    bool encodeOptions(
        const GameOptions& options,
        std::vector<std::uint8_t>& result)
    {
        Writer payload;

        writeOptionsPayload(
            payload,
            options
        );


        return makeEnvelope(
            OptionsMagic,
            payload.data(),
            result
        );
    }

    bool decodeOptions(
        std::span<const std::uint8_t> data,
        GameOptions& result)
    {
        std::vector<std::uint8_t>
            payload;


        if (
            !openEnvelope(
                data,
                OptionsMagic,
                payload
            ))
        {
            return false;
        }


        Reader reader(payload);

        GameOptions decoded{};


        if (
            !readOptionsPayload(
                reader,
                decoded
            ) ||
            !reader.finished())
        {
            return false;
        }


        options::validate(
            decoded
        );


        result =
            std::move(decoded);


        return true;
    }
}

