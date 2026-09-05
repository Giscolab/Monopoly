cmake_minimum_required(VERSION 3.25)

project(
    MonopolyModern
    VERSION 0.1.0
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    # MSVC does not enable standard C++ stack unwinding for these Ninja
    # targets automatically in the current VS 18 toolchain.
    add_compile_options(
        $<$<COMPILE_LANGUAGE:CXX>:/EHsc>
    )
endif()

include(CTest)

include(FetchContent)

set(
    MONOPOLY_LEGACY_SOURCE_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/../Source"
)

set(MONOPOLY_LEGACY_MANIFESTS_AVAILABLE TRUE)

foreach(LEGACY_MANIFEST_HEADER IN ITEMS
    dat_3d.h
    dat_bord.h
    DAT_BORDE.h
    dat_brd2.h
    dat_lk01.h
    dat_lk02.h
    dat_lm01.h
    dat_lm02.h
    dat_main.h
    dat_pat.h
)
    if(NOT EXISTS
        "${MONOPOLY_LEGACY_SOURCE_ROOT}/monopoly/Dat_Mon/${LEGACY_MANIFEST_HEADER}"
    )
        set(MONOPOLY_LEGACY_MANIFESTS_AVAILABLE FALSE)
    endif()
endforeach()

set(MONOPOLY_LEGACY_TEXTURE_CORPUS_AVAILABLE TRUE)

foreach(LEGACY_TEXTURE_DIRECTORY IN ITEMS
    Boards
    Cities
    Currency
    Languages
)
    if(NOT IS_DIRECTORY
        "${MONOPOLY_LEGACY_SOURCE_ROOT}/monopoly/${LEGACY_TEXTURE_DIRECTORY}"
    )
        set(MONOPOLY_LEGACY_TEXTURE_CORPUS_AVAILABLE FALSE)
    endif()
endforeach()

if(
    EXISTS "${MONOPOLY_LEGACY_SOURCE_ROOT}/monopoly/BackGround.bmp"
    AND EXISTS
        "${MONOPOLY_LEGACY_SOURCE_ROOT}/monopoly/BackGroundEuro.bmp"
)
    set(MONOPOLY_LEGACY_BACKGROUNDS_AVAILABLE TRUE)
else()
    set(MONOPOLY_LEGACY_BACKGROUNDS_AVAILABLE FALSE)
endif()

# Source/ZLib/ZImplode.c proves that the historical implode/explode names
# wrap ordinary zlib streams (windowBits = 15). Prefer an installed package;
# otherwise build the pinned upstream release, never the private SDL copy.
find_package(ZLIB 1.2.9 QUIET)

if(ZLIB_FOUND)
    set(MONOPOLY_ZLIB_TARGET ZLIB::ZLIB)
else()
    set(ZLIB_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(ZLIB_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(ZLIB_BUILD_STATIC ON CACHE BOOL "" FORCE)
    set(ZLIB_INSTALL OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        zlib
        GIT_REPOSITORY https://github.com/madler/zlib.git
        GIT_TAG v1.3.2
        GIT_SHALLOW TRUE
    )

    FetchContent_MakeAvailable(zlib)
    set(MONOPOLY_ZLIB_TARGET ZLIB::ZLIBSTATIC)
endif()

FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.4.14
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(SDL3)

add_library(
    MonopolyDataCore
    STATIC
    src/DataBanks.cpp
    src/LegacyDataArchive.cpp
    src/LegacyDataArchiveBuilder.cpp
    src/LegacyChunk.cpp
    src/LegacySequence.cpp
    src/SequenceClock.cpp
    src/SequenceChildSchedule.cpp
    src/SequenceRuntime.cpp
    src/SequenceCommands.cpp
    src/SequenceTransforms.cpp
    src/LegacyMeshData.cpp
    src/LanguageResources.cpp
    src/ResourcePaths.cpp
    src/ResourceRuntime.cpp
    src/LegacyManifest.cpp
    src/LegacyBitmap.cpp
    src/TextureCatalog.cpp
)

target_include_directories(
    MonopolyDataCore
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(
    MonopolyDataCore
    PUBLIC
    ${MONOPOLY_ZLIB_TARGET}
)

if(MSVC)
    target_compile_options(
        MonopolyDataCore
        PRIVATE
        /W4
        /permissive-
    )
endif()

add_executable(
    MonopolyManifestTool
    src/ManifestTool.cpp
)

target_link_libraries(
    MonopolyManifestTool
    PRIVATE
    MonopolyDataCore
)

if(MSVC)
    target_compile_options(
        MonopolyManifestTool
        PRIVATE
        /W4
        /permissive-
    )
endif()

if(BUILD_TESTING)

    add_executable(
        MonopolyLegacyManifestTests
        tests/LegacyManifestTests.cpp
    )

    target_link_libraries(
        MonopolyLegacyManifestTests
        PRIVATE
        MonopolyDataCore
    )

    if(MONOPOLY_LEGACY_MANIFESTS_AVAILABLE)
        target_compile_definitions(
            MonopolyLegacyManifestTests
            PRIVATE
            MONOPOLY_LEGACY_SOURCE_DIR="${MONOPOLY_LEGACY_SOURCE_ROOT}"
        )
    endif()

    if(MSVC)
        target_compile_options(
            MonopolyLegacyManifestTests
            PRIVATE
            /W4
            /permissive-
        )
    endif()

    add_test(
        NAME Monopoly.LegacyManifest
        COMMAND MonopolyLegacyManifestTests
    )

    add_executable(
        MonopolyLegacyBitmapTests
        tests/LegacyBitmapTests.cpp
    )

    target_link_libraries(
        MonopolyLegacyBitmapTests
        PRIVATE
        MonopolyDataCore
    )

    if(MONOPOLY_LEGACY_TEXTURE_CORPUS_AVAILABLE)
        target_compile_definitions(
            MonopolyLegacyBitmapTests
            PRIVATE
            MONOPOLY_LEGACY_SOURCE_DIR="${MONOPOLY_LEGACY_SOURCE_ROOT}"
        )
    endif()

    if(MSVC)
        target_compile_options(
            MonopolyLegacyBitmapTests
            PRIVATE
            /W4
            /permissive-
        )
    endif()

    add_test(
        NAME Monopoly.LegacyBitmap
        COMMAND MonopolyLegacyBitmapTests
    )

    add_executable(
        MonopolyTextureCatalogTests
        tests/TextureCatalogTests.cpp
    )

    target_link_libraries(
        MonopolyTextureCatalogTests
        PRIVATE
        MonopolyDataCore
    )

    if(MSVC)
        target_compile_options(
            MonopolyTextureCatalogTests
            PRIVATE
            /W4
            /permissive-
        )
    endif()

    if(MONOPOLY_LEGACY_TEXTURE_CORPUS_AVAILABLE)
        add_test(
            NAME Monopoly.TextureCatalog
            COMMAND MonopolyTextureCatalogTests
                "${MONOPOLY_LEGACY_SOURCE_ROOT}/monopoly"
        )
    else()
        add_test(
            NAME Monopoly.TextureCatalog
            COMMAND MonopolyTextureCatalogTests
        )
    endif()

endif()

add_executable(
    MonopolyModern
    src/main.cpp
)

target_link_libraries(
    MonopolyModern
    PRIVATE
    SDL3::SDL3
    MonopolyDataCore
)

if(MSVC)
    target_compile_options(
        MonopolyModern
        PRIVATE
        /W4
        /permissive-
    )
endif()

# ============================================================
# ArtLib 60 Hz timer -> UIMessages contract
# ============================================================

if(BUILD_TESTING)

    add_executable(
        MonopolyUIMessagesTests
        tests/UIMessagesTests.cpp
        src/UIMessages.cpp
    )

    target_include_directories(
        MonopolyUIMessagesTests
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    if(MSVC)
        target_compile_options(
            MonopolyUIMessagesTests
            PRIVATE
            /W4
            /permissive-
        )
    endif()

    add_test(
        NAME Monopoly.UIMessages
        COMMAND MonopolyUIMessagesTests
    )

    add_executable(
        MonopolyTimerTests
        tests/TimerTests.cpp
        src/Timers.cpp
        src/UIMessages.cpp
    )

    target_include_directories(
        MonopolyTimerTests
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    if(MSVC)
        target_compile_options(
            MonopolyTimerTests
            PRIVATE
            /W4
            /permissive-
        )
    endif()

    add_test(
        NAME Monopoly.Timers
        COMMAND MonopolyTimerTests
    )

endif()

# TARGET_RUNTIME_DLLS is empty on non-DLL platforms and with static SDL.
# Do not invoke copy_if_different with only a destination in those builds.
if(WIN32 AND SDL_SHARED)
    add_custom_command(
        TARGET MonopolyModern
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:MonopolyModern>
            $<TARGET_FILE_DIR:MonopolyModern>
        COMMAND_EXPAND_LISTS
    )
endif()


target_sources(
    MonopolyModern
    PRIVATE
    src/Application.cpp
    src/Game.cpp
)

target_sources(
    MonopolyModern
    PRIVATE
    src/Engine.cpp
)

target_sources(
    MonopolyModern
    PRIVATE
    src/RenderSlots.cpp
)

target_sources(
    MonopolyModern
    PRIVATE
    src/UDUtils.cpp
)

target_sources(
    MonopolyModern
    PRIVATE
    src/MousePointer.cpp
)

target_sources(
    MonopolyModern
    PRIVATE
    src/Timers.cpp
)

target_sources(
    MonopolyModern
    PRIVATE
    src/RuntimeState.cpp
    src/ExtendedInitialization.cpp
)

# ============================================================
# Legacy L_Data identifiers and bank catalog contracts
# ============================================================

if(BUILD_TESTING)

    add_executable(
        MonopolyLegacyDataContractTests
        tests/LegacyDataContractTests.cpp
    )

    target_include_directories(
        MonopolyLegacyDataContractTests
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    target_link_libraries(
        MonopolyLegacyDataContractTests
        PRIVATE
        MonopolyDataCore
    )

    if(MSVC)
        target_compile_options(
            MonopolyLegacyDataContractTests
            PRIVATE
            /W4
            /permissive-
        )
    endif()

    add_test(
        NAME Monopoly.LegacyDataContract
        COMMAND MonopolyLegacyDataContractTests
    )

    add_executable(
        MonopolyLegacyDataArchiveTests
        tests/LegacyDataArchiveTests.cpp
    )

    target_link_libraries(
        MonopolyLegacyDataArchiveTests
        PRIVATE
        MonopolyDataCore
    )

    if(MSVC)
        target_compile_options(
            MonopolyLegacyDataArchiveTests
            PRIVATE
            /W4
            /permissive-
        )
    endif()

    add_test(
        NAME Monopoly.LegacyDataArchive
        COMMAND MonopolyLegacyDataArchiveTests
    )

    add_executable(
        MonopolyLegacyChunkTests
        tests/LegacyChunkTests.cpp
    )

    target_link_libraries(
        MonopolyLegacyChunkTests
        PRIVATE
        MonopolyDataCore
    )

    if(MSVC)
        target_compile_options(
            MonopolyLegacyChunkTests
            PRIVATE
            /W4
            /permissive-
        )
    endif()

    add_test(
        NAME Monopoly.LegacyChunk
        COMMAND MonopolyLegacyChunkTests
    )

    add_executable(
        MonopolyLanguageResourcesTests
        tests/LanguageResourcesTests.cpp
    )

    target_link_libraries(
        MonopolyLanguageResourcesTests
        PRIVATE
        MonopolyDataCore
    )

    if(MSVC)
        target_compile_options(
            MonopolyLanguageResourcesTests
            PRIVATE
            /W4
            /permissive-
        )
    endif()

    add_test(
        NAME Monopoly.LanguageResources
        COMMAND MonopolyLanguageResourcesTests
    )

    foreach(RESOURCE_SUITE IN ITEMS ResourcePaths ResourceRuntime LegacySequence LegacyMeshData SequenceClock SequenceChildSchedule SequenceRuntime SequenceCommands SequenceTransforms)
        add_executable(
            Monopoly${RESOURCE_SUITE}Tests
            tests/${RESOURCE_SUITE}Tests.cpp
        )
        target_link_libraries(
            Monopoly${RESOURCE_SUITE}Tests PRIVATE MonopolyDataCore
        )
        if(MSVC)
            target_compile_options(
                Monopoly${RESOURCE_SUITE}Tests PRIVATE /W4 /permissive-
            )
        endif()
        add_test(
            NAME Monopoly.${RESOURCE_SUITE}
            COMMAND Monopoly${RESOURCE_SUITE}Tests
        )
    endforeach()

    add_executable(
        MonopolyResourceLifecycleTests
        tests/ResourceLifecycleTests.cpp
        src/Game.cpp
        src/ExtendedInitialization.cpp
        src/Timers.cpp
        src/UIMessages.cpp
    )
    target_link_libraries(
        MonopolyResourceLifecycleTests PRIVATE MonopolyDataCore
    )
    if(MSVC)
        target_compile_options(
            MonopolyResourceLifecycleTests PRIVATE /W4 /permissive-
        )
    endif()
    add_test(
        NAME Monopoly.ResourceLifecycle
        COMMAND MonopolyResourceLifecycleTests
    )

    add_executable(
        MonopolyResourceConfigurationTests
        tests/ResourceConfigurationTests.cpp
        src/UDUtils.cpp
    )
    target_link_libraries(
        MonopolyResourceConfigurationTests PRIVATE MonopolyDataCore SDL3::SDL3
    )
    if(MSVC)
        target_compile_options(
            MonopolyResourceConfigurationTests PRIVATE /W4 /permissive-
        )
    endif()
    if(WIN32 AND SDL_SHARED)
        add_custom_command(
            TARGET MonopolyResourceConfigurationTests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:MonopolyResourceConfigurationTests>
                $<TARGET_FILE_DIR:MonopolyResourceConfigurationTests>
            COMMAND_EXPAND_LISTS
        )
    endif()
    add_test(
        NAME Monopoly.ResourceConfiguration
        COMMAND MonopolyResourceConfigurationTests
    )

endif()

target_sources(
    MonopolyModern
    PRIVATE
    src/UIMessages.cpp
    src/UserInterface.cpp
)

if(BUILD_TESTING)

    add_executable(
        MonopolyUserInterfaceRoutingTests
        tests/UserInterfaceRoutingTests.cpp
        src/UserInterface.cpp
        src/RuntimeState.cpp
    )

    target_include_directories(
        MonopolyUserInterfaceRoutingTests
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    if(MSVC)
        target_compile_options(
            MonopolyUserInterfaceRoutingTests
            PRIVATE
            /W4
            /permissive-
        )
    endif()

    add_test(
        NAME Monopoly.UserInterfaceRouting
        COMMAND MonopolyUserInterfaceRoutingTests
    )

endif()

target_sources(
    MonopolyModern
    PRIVATE
    src/Messaging.cpp
    src/RulesEngine.cpp
)

target_sources(
    MonopolyModern
    PRIVATE
    src/TimeStep.cpp
)

target_sources(
    MonopolyModern
    PRIVATE
    src/PhaseStack.cpp
)

target_sources(
    MonopolyModern
    PRIVATE
    src/RuleStartupActions.cpp
)

target_sources(
    MonopolyModern
    PRIVATE
    src/Display.cpp
    src/PlayerSelection.cpp
)

target_sources(
    MonopolyModern
    PRIVATE
    src/GPUFrame.cpp
    src/LogicalViewport.cpp
)

# ============================================================
# Logical 800x600 viewport, resizing and letterbox contract
# ============================================================

if(BUILD_TESTING)

    add_executable(
        MonopolyLogicalViewportTests
        tests/LogicalViewportTests.cpp
        src/LogicalViewport.cpp
    )

    target_include_directories(
        MonopolyLogicalViewportTests
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    if(MSVC)
        target_compile_options(
            MonopolyLogicalViewportTests
            PRIVATE
            /W4
            /permissive-
        )
    endif()

    add_test(
        NAME Monopoly.LogicalViewport
        COMMAND MonopolyLogicalViewportTests
    )

endif()

target_sources(
    MonopolyModern
    PRIVATE
    src/LegacyAssets.cpp
)

if(MONOPOLY_LEGACY_BACKGROUNDS_AVAILABLE)
    add_custom_command(
        TARGET MonopolyModern
        POST_BUILD

        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:MonopolyModern>/assets/legacy"

        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${MONOPOLY_LEGACY_SOURCE_ROOT}/monopoly/BackGround.bmp"
            "$<TARGET_FILE_DIR:MonopolyModern>/assets/legacy/BackGround.bmp"

        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${MONOPOLY_LEGACY_SOURCE_ROOT}/monopoly/BackGroundEuro.bmp"
            "$<TARGET_FILE_DIR:MonopolyModern>/assets/legacy/BackGroundEuro.bmp"
    )
endif()

target_sources(
    MonopolyModern
    PRIVATE
    src/BoardRules.cpp
    src/BoardGeometry.cpp
)

target_sources(
    MonopolyModern
    PRIVATE
    src/RuleRandom.cpp
    src/CardDecks.cpp
    src/RuleGameStart.cpp
)

target_sources(
    MonopolyModern
    PRIVATE
    src/RuleTurnActions.cpp
)

target_sources(
    MonopolyModern
    PRIVATE
    src/RuleEconomy.cpp
)

target_sources(
    MonopolyModern
    PRIVATE
    src/RuleBuildings.cpp
    src/RuleAuction.cpp
)

target_sources(
    MonopolyModern
    PRIVATE
    src/CardDeckRuntime.cpp
    src/RuleCards.cpp
)

target_sources(
    MonopolyModern
    PRIVATE
    src/RuleJail.cpp
    src/RuleStartingOrder.cpp
)

target_sources(
    MonopolyModern
    PRIVATE
    src/RuleTrade.cpp
)

target_sources(
    MonopolyModern
    PRIVATE
    src/RuleLifecycle.cpp
    src/RulePlayers.cpp
)

target_sources(
    MonopolyModern
    PRIVATE
    src/RuleArchive.cpp
    src/RuleResync.cpp
    src/RuleSave.cpp
)

target_sources(
    MonopolyModern
    PRIVATE
    src/RuleSynchronization.cpp
    src/RuleCoreActions.cpp
)

target_sources(
    MonopolyModern
    PRIVATE
    src/RuleOptions.cpp
    src/RuleInitialDeal.cpp
    src/RulePrediction.cpp
)

# ============================================================
# RULE scenario tests
# ============================================================

if(BUILD_TESTING)

    add_executable(
        MonopolyRuleTests
        tests/RuleScenarioTests.cpp
        src/BoardRules.cpp
        src/PhaseStack.cpp
        src/RuleArchive.cpp
        src/RuleOptions.cpp
    )

    target_include_directories(
        MonopolyRuleTests
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    if(MSVC)
        target_compile_options(
            MonopolyRuleTests
            PRIVATE
            /W4
            /permissive-
        )
    endif()

    add_test(
        NAME Monopoly.RuleScenarios
        COMMAND MonopolyRuleTests
    )

endif()

# ============================================================
# Original Userifce local-player ownership model
# ============================================================

target_sources(
    MonopolyModern
    PRIVATE
    src/LocalPlayers.cpp
)

# ============================================================
# UDPSEL player setup state machine
# ============================================================

target_sources(
    MonopolyModern
    PRIVATE
    src/PlayerSetupFlow.cpp
)

if(BUILD_TESTING)

    add_executable(
        MonopolyPlayerSetupTests
        tests/PlayerSetupFlowTests.cpp
        src/PlayerSetupFlow.cpp
    )

    target_include_directories(
        MonopolyPlayerSetupTests
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    if(MSVC)
        target_compile_options(
            MonopolyPlayerSetupTests
            PRIVATE
            /W4
            /permissive-
        )
    endif()

    add_test(
        NAME Monopoly.PlayerSetup
        COMMAND MonopolyPlayerSetupTests
    )

endif()

if(BUILD_TESTING)

    add_executable(
        MonopolyLocalPlayersTests
        tests/LocalPlayersTests.cpp
        src/LocalPlayers.cpp
        src/Messaging.cpp
    )

    target_include_directories(
        MonopolyLocalPlayersTests
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    if(MSVC)
        target_compile_options(
            MonopolyLocalPlayersTests
            PRIVATE
            /W4
            /permissive-
        )
    endif()

    add_test(
        NAME Monopoly.LocalPlayers
        COMMAND MonopolyLocalPlayersTests
    )

endif()

# ============================================================
# DISPLAY_UDIBAR
# Player-select status/player bar
# ============================================================

target_sources(
    MonopolyModern
    PRIVATE
    src/IBar.cpp
    src/IBarLayout.cpp
)

if(BUILD_TESTING)

    add_executable(
        MonopolyIBarTests
        tests/IBarLayoutTests.cpp
        src/IBarLayout.cpp
    )

    target_include_directories(
        MonopolyIBarTests
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    if(MSVC)
        target_compile_options(
            MonopolyIBarTests
            PRIVATE
            /W4
            /permissive-
        )
    endif()

    add_test(
        NAME Monopoly.IBarLayout
        COMMAND MonopolyIBarTests
    )

endif()

if(BUILD_TESTING)

    add_executable(
        MonopolyDisplayContractTests
        tests/DisplayContractTests.cpp
        src/Display.cpp
    )

    target_include_directories(
        MonopolyDisplayContractTests
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    if(MSVC)
        target_compile_options(
            MonopolyDisplayContractTests
            PRIVATE
            /W4
            /permissive-
        )
    endif()

    add_test(
        NAME Monopoly.DisplayContract
        COMMAND MonopolyDisplayContractTests
    )

endif()
