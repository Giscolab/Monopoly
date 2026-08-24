#include "LocalPlayers.hpp"

#include "LegacyTextIds.hpp"
#include "Messaging.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace monopoly::ui::localplayers
{
    namespace
    {
        using rules::AllPlayers;
        using rules::BankPlayer;
        using rules::GameState;
        using rules::MaxPlayerNameLength;
        using rules::MaxPlayers;
        using rules::NobodyPlayer;
        using rules::PlayerNumber;
        using rules::SpectatorPlayer;


        constexpr std::uint8_t OffBoardSquare = 41;


        struct LocalEntry
        {
            std::wstring name{};

            PlayerNumber slot =
                NobodyPlayer;
        };


        std::array<
            LocalEntry,
            MaxLocalPlayers
        > localPlayers{};


        std::size_t numberOfLocalPlayers = 0;


        std::array<bool, MaxPlayers>
            localSlots{};


        std::array<bool, MaxPlayers>
            localHumanSlots{};


        std::array<bool, MaxPlayers>
            localAISlots{};


        PlayerNumber selectedUIPlayer =
            NobodyPlayer;


        std::wstring messageText(
            const actions::Message& message)
        {
            if (message.stringA.data() == nullptr)
            {
                return {};
            }

            return std::wstring(
                message.stringA.data()
            );
        }


        void clearSlot(
            PlayerNumber slot)
        {
            if (slot >= MaxPlayers)
            {
                return;
            }

            localSlots[slot] = false;
            localHumanSlots[slot] = false;
            localAISlots[slot] = false;


            if (selectedUIPlayer == slot)
            {
                selectedUIPlayer =
                    NobodyPlayer;
            }
        }


        void removeEntryAt(
            std::size_t index)
        {
            if (
                index >=
                numberOfLocalPlayers)
            {
                return;
            }


            clearSlot(
                localPlayers[index].slot
            );


            if (numberOfLocalPlayers > 0)
            {
                --numberOfLocalPlayers;


                // RemoveLocalPlayerByName() original bouche le trou avec
                // le dernier joueur local; il ne decale pas toute la liste.
                if (index != numberOfLocalPlayers)
                {
                    localPlayers[index] =
                        std::move(
                            localPlayers[numberOfLocalPlayers]
                        );
                }

                localPlayers[
                    numberOfLocalPlayers
                ] = {};
            }
        }


        void removeLocalPlayerByName(
            std::wstring_view name)
        {
            for (
                std::size_t i = 0;
                i < numberOfLocalPlayers;
                ++i)
            {
                if (
                    localPlayers[i].name ==
                    name)
                {
                    removeEntryAt(i);
                    return;
                }
            }
        }


        std::size_t findLocalName(
            std::wstring_view name)
        {
            for (
                std::size_t i = 0;
                i < numberOfLocalPlayers;
                ++i)
            {
                if (
                    localPlayers[i].name ==
                    name)
                {
                    return i;
                }
            }

            return MaxLocalPlayers;
        }


        PlayerNumber determineTakeOverPlayer(
            const GameState& uiState,
            std::uint8_t token,
            bool takeOverAI)
        {
            // AddLocalPlayer() original.
            //
            // Priorité :
            //  1. AI non-bankrupt avec même token
            //  2. AI non-bankrupt
            //  3. AI bankrupt
            //  4. RULE_NOBODY_PLAYER = nouveau slot.

            PlayerNumber bestPlayer =
                NobodyPlayer;


            if (!takeOverAI)
            {
                return bestPlayer;
            }


            for (
                PlayerNumber player = 0;
                player <
                    uiState.numberOfPlayers;
                ++player)
            {
                const auto& info =
                    uiState.players[player];


                if (info.aiPlayerLevel == 0)
                {
                    continue;
                }


                if (
                    info.currentSquare ==
                    OffBoardSquare)
                {
                    if (
                        bestPlayer ==
                            NobodyPlayer ||
                        (
                            uiState.players[
                                bestPlayer
                            ].currentSquare ==
                                OffBoardSquare &&
                            uiState.players[
                                bestPlayer
                            ].token != token
                        ))
                    {
                        bestPlayer =
                            player;
                    }
                }
                else
                {
                    bestPlayer =
                        player;
                }


                if (
                    bestPlayer !=
                        NobodyPlayer &&
                    uiState.players[
                        bestPlayer
                    ].currentSquare !=
                        OffBoardSquare &&
                    uiState.players[
                        bestPlayer
                    ].token == token)
                {
                    break;
                }
            }


            return bestPlayer;
        }


        void processNamingError(
            const actions::Message& message)
        {
            if (
                message.numberA !=
                    legacy_text::ErrorNameInUse &&
                message.numberA !=
                    legacy_text::ErrorNoMorePlayers &&
                message.numberA !=
                    legacy_text::ErrorNotYourPlayer)
            {
                return;
            }


            const std::wstring name =
                messageText(message);


            for (
                std::size_t i = 0;
                i < numberOfLocalPlayers;
                ++i)
            {
                if (
                    localPlayers[i].name ==
                        name &&
                    localPlayers[i].slot ==
                        NobodyPlayer)
                {
                    removeEntryAt(i);
                    return;
                }
            }
        }


        void processNamePlayer(
            GameState& uiState,
            const actions::Message& message)
        {
            const PlayerNumber newSlot =
                static_cast<PlayerNumber>(
                    message.numberA
                );


            if (newSlot >= MaxPlayers)
            {
                return;
            }


            auto& player =
                uiState.players[newSlot];


            const std::uint8_t oldAILevel =
                player.aiPlayerLevel;

            const std::uint8_t oldColour =
                player.colour;

            const std::uint8_t oldToken =
                player.token;


            const std::wstring newName =
                messageText(message);


            // ------------------------------------------------
            // UICurrentGameState est mis à jour AVANT de
            // déterminer si le joueur est local.
            // ------------------------------------------------

            player.token =
                static_cast<std::uint8_t>(
                    message.numberB
                );


            player.colour =
                static_cast<std::uint8_t>(
                    message.numberC
                );


            player.aiPlayerLevel =
                static_cast<std::uint8_t>(
                    message.numberD
                );


            player.name =
                newName.substr(
                    0,
                    MaxPlayerNameLength
                );


            // ------------------------------------------------
            // Est-ce l'un de nos noms locaux ?
            // ------------------------------------------------

            const std::size_t localIndex =
                findLocalName(
                    player.name
                );


            if (
                localIndex <
                numberOfLocalPlayers)
            {
                const PlayerNumber oldSlot =
                    localPlayers[
                        localIndex
                    ].slot;


                // Notification identique déjà appliquée :
                // le code 1999 l'ignore.
                if (
                    localSlots[newSlot] &&
                    oldSlot == newSlot &&
                    oldAILevel ==
                        player.aiPlayerLevel &&
                    oldToken ==
                        player.token &&
                    oldColour ==
                        player.colour)
                {
                    return;
                }


                // Aucun autre joueur local ne doit rester
                // associé au nouveau slot ou à l'ancien slot.

                for (
                    std::size_t i = 0;
                    i < numberOfLocalPlayers;
                    ++i)
                {
                    if (
                        localPlayers[i].slot ==
                            newSlot ||
                        localPlayers[i].slot ==
                            oldSlot)
                    {
                        localPlayers[i].slot =
                            NobodyPlayer;
                    }
                }


                if (
                    oldSlot !=
                    NobodyPlayer)
                {
                    clearSlot(
                        oldSlot
                    );
                }


                // Assigner le joueur à son nouveau slot.
                localSlots[newSlot] =
                    true;


                localPlayers[
                    localIndex
                ].slot =
                    newSlot;


                if (
                    player.aiPlayerLevel != 0)
                {
                    localAISlots[newSlot] =
                        true;

                    localHumanSlots[newSlot] =
                        false;


                    // AI_Load_AI() appartenait à l'ancien
                    // sous-système AI DLL.
                    //
                    // Le slot local AI est néanmoins conservé
                    // exactement ici ; le moteur RULE possède
                    // déjà aiPlayerLevel et les blobs AI.
                }
                else
                {
                    localHumanSlots[newSlot] =
                        true;

                    localAISlots[newSlot] =
                        false;
                }


                return;
            }


            // ------------------------------------------------
            // Ce joueur appartient à quelqu'un d'autre.
            //
            // Conserver nos noms locaux mais les rendre
            // "slotless" si un reorder leur a pris ce slot.
            // ------------------------------------------------

            for (
                std::size_t i = 0;
                i < numberOfLocalPlayers;
                ++i)
            {
                if (
                    localPlayers[i].slot ==
                    newSlot)
                {
                    localPlayers[i].slot =
                        NobodyPlayer;
                }
            }


            clearSlot(newSlot);
        }


        void processDeletedPlayer(
            const actions::Message& message)
        {
            removeLocalPlayerByName(
                messageText(message)
            );
        }


        void processAddLocalPlayer(
            const actions::Message& message)
        {
            const PlayerNumber newSlot =
                static_cast<PlayerNumber>(
                    message.numberA
                );


            if (newSlot >= MaxPlayers)
            {
                return;
            }


            const std::wstring name =
                messageText(message);


            // Supprimer une ancienne occurrence du même nom.
            removeLocalPlayerByName(
                name
            );


            // Supprimer aussi tout joueur local qui occupait
            // déjà le slot destination.

            for (
                std::size_t i = 0;
                i < numberOfLocalPlayers;)
            {
                if (
                    localPlayers[i].slot ==
                    newSlot)
                {
                    removeEntryAt(i);
                    continue;
                }

                ++i;
            }


            // Userifce.cpp :
            //
            // uniquement le serveur ajoute ce joueur à la
            // liste locale. NOTIFY_NAME_PLAYER attribuera
            // ensuite son véritable slot.

            if (
                messaging::serverMode() &&
                numberOfLocalPlayers <
                    MaxLocalPlayers)
            {
                localPlayers[
                    numberOfLocalPlayers
                ].name =
                    name.substr(
                        0,
                        MaxPlayerNameLength
                    );


                localPlayers[
                    numberOfLocalPlayers
                ].slot =
                    NobodyPlayer;


                ++numberOfLocalPlayers;
            }
        }
    }


    void reset()
    {
        for (auto& player :
             localPlayers)
        {
            player = {};

            player.slot =
                NobodyPlayer;
        }


        numberOfLocalPlayers = 0;


        localSlots.fill(false);
        localHumanSlots.fill(false);
        localAISlots.fill(false);


        selectedUIPlayer =
            NobodyPlayer;
    }


    std::size_t count()
    {
        return
            numberOfLocalPlayers;
    }


    std::size_t humanCount()
    {
        std::size_t result = 0;


        for (PlayerNumber player = 0;
             player < MaxPlayers;
             ++player)
        {
            if (localHumanSlots[player])
            {
                ++result;
            }
        }


        return result;
    }

    PlayerNumber slotAt(
        std::size_t localIndex)
    {
        if (
            localIndex >=
            numberOfLocalPlayers)
        {
            return NobodyPlayer;
        }


        return
            localPlayers[
                localIndex
            ].slot;
    }


    bool slotIsLocalPlayer(
        PlayerNumber slot)
    {
        return
            slot < MaxPlayers &&
            localSlots[slot];
    }


    bool slotIsLocalHumanPlayer(
        PlayerNumber slot)
    {
        return
            slot < MaxPlayers &&
            localHumanSlots[slot];
    }


    bool slotIsLocalAIPlayer(
        PlayerNumber slot)
    {
        return
            slot < MaxPlayers &&
            localAISlots[slot];
    }


    bool isLocalRecipient(
        PlayerNumber recipient)
    {
        // Userifce.cpp original, AdvanceTimeStep(): seules les
        // notifications broadcast ou destinees a un slot local
        // traversent ProcessPlayersUI().
        return
            recipient == AllPlayers ||
            (recipient < MaxPlayers &&
             slotIsLocalPlayer(recipient));
    }


    PlayerNumber currentUIPlayer()
    {
        return
            selectedUIPlayer;
    }


    void setCurrentUIPlayerFromPlayerNumber(
        PlayerNumber player)
    {
        // SetCurrentUIPlayerFromPlayerNumber() original.

        selectedUIPlayer =
            NobodyPlayer;


        if (
            player < MaxPlayers &&
            localHumanSlots[player])
        {
            selectedUIPlayer =
                player;
        }
    }


    PlayerNumber anyLocalPlayer(
        const GameState& uiState)
    {
        // AnyLocalPlayer() original.
        //
        // Inclut les IA locales.

        for (
            PlayerNumber player = 0;
            player <
                uiState.numberOfPlayers;
            ++player)
        {
            if (localSlots[player])
            {
                return player;
            }
        }


        return NobodyPlayer;
    }


    bool requestAddLocalPlayer(
        const GameState& uiState,
        std::wstring_view name,
        std::uint8_t token,
        std::uint8_t colour,
        std::uint8_t aiLevel,
        bool takeOverAI)
    {
        // ====================================================
        // AddLocalPlayer() original.
        // ====================================================

        if (
            numberOfLocalPlayers >=
            MaxLocalPlayers)
        {
            return false;
        }


        if (
            name.empty() ||
            name.size() >
                MaxPlayerNameLength)
        {
            return false;
        }


        const std::wstring ownedName(
            name
        );


        // ----------------------------------------------------
        // Mémoriser le nom seulement s'il n'est pas déjà
        // dans notre liste locale.
        // ----------------------------------------------------

        if (
            findLocalName(ownedName) ==
            MaxLocalPlayers)
        {
            localPlayers[
                numberOfLocalPlayers
            ].name =
                ownedName;


            localPlayers[
                numberOfLocalPlayers
            ].slot =
                NobodyPlayer;


            ++numberOfLocalPlayers;
        }


        // ----------------------------------------------------
        // Déterminer le joueur AI éventuellement remplacé.
        // ----------------------------------------------------

        const PlayerNumber bestPlayer =
            determineTakeOverPlayer(
                uiState,
                token,
                takeOverAI
            );


        // ----------------------------------------------------
        // IMPORTANT :
        //
        // fromPlayer = RULE_SPECTATOR_PLAYER (9)
        //
        // et NON RULE_NOBODY_PLAYER.
        // ----------------------------------------------------

        messaging::sendAction(
            actions::Type::NamePlayer,
            SpectatorPlayer,
            BankPlayer,

            bestPlayer,
            aiLevel,
            token,
            colour,

            ownedName
        );


        return true;
    }


    bool requestRemoveLocalPlayer(
        const GameState& uiState,
        PlayerNumber player)
    {
        // RemoveLocalPlayer() original.

        if (
            player >=
                uiState.numberOfPlayers ||
            numberOfLocalPlayers == 0)
        {
            return false;
        }


        const auto& info =
            uiState.players[player];


        // L'original retire immédiatement le nom de la liste
        // locale avant que RULE confirme la suppression.
        removeLocalPlayerByName(
            info.name
        );


        // Naming with a null string effects a delete.
        messaging::sendAction(
            actions::Type::NamePlayer,
            player,
            BankPlayer,

            player,
            info.aiPlayerLevel,
            info.token,
            info.colour,

            L""
        );


        return true;
    }

    void processRuleMessage(
        GameState& uiState,
        const actions::Message& message)
    {
        switch (message.action)
        {
            case actions::Type::NotifyErrorMessage:
                processNamingError(
                    message
                );
                return;


            case actions::Type::NotifyNamePlayer:
                processNamePlayer(
                    uiState,
                    message
                );
                return;


            case actions::Type::NotifyPlayerDeleted:
                processDeletedPlayer(
                    message
                );
                return;


            case actions::Type::NotifyAddLocalPlayer:
                processAddLocalPlayer(
                    message
                );
                return;


            default:
                return;
        }
    }
}

