# Audit automatique du portage {#porting_audit}

> Genere mecaniquement depuis `PORTING_STATUS.md`, `modern/src`, `modern/tests` et `modern/CMakeLists.txt`. Ce rapport detecte les derives structurelles; il ne certifie **pas** la parite semantique avec le jeu de 1999.

## Synthese

| Metrique | Valeur courante | Signification |
|---|---:|---|
| Audit fonctionnel | 75% | Snapshot manuel : 14 septembre |
| Familles engagees | 40/40 (100%) | Familles legacy actives avec un equivalent moderne engage |
| Indice automatique | 67% | Complet/remplace=100, partiel=50, non demarre=0 |
| Entrees actives closes | 24/67 (35.8%) | `PORTED_COMPLETE` + `REPLACED_PORTABLE` |
| Entrees actives partielles | 43/67 | Travail connu restant |
| Non demarrees | 0 | Entrees actives sans equivalent moderne significatif |
| Preuve CTest documentee | 127/127 (100%) | Derniere preuve courante de PORTING_STATUS; ce n est pas un score de fidelite |

## Controles automatiques

- PASS - Les chiffres structurels ecrits dans PORTING_STATUS correspondent a la matrice
- PASS - Tous les `modern/src/*.cpp` sont enregistres dans CMake
- PASS - Tous les `modern/tests/*.cpp` sont enregistres dans CMake
- PASS - Aucune entree active de la matrice n est `NOT_STARTED`

## File de revue des `PORTED_PARTIAL`

Le classement ci-dessous est mecanique. Il place le runtime/gameplay et les chemins visibles avant les travaux de fidelite plus bas niveau; c est une aide au triage, pas un verdict de completion.

| Poids | Zone | Ligne legacy | Ligne matrice |
|---:|---|---|---:|
| 4 | gameplay/runtime central | `Source/monopoly/Rule.cpp` | 375 |
| 4 | gameplay/runtime central | `Source/monopoly/Userifce.cpp` | 386 |
| 4 | gameplay/runtime central | `Source/monopoly/UDPsel.cpp` | 392 |
| 4 | gameplay/runtime central | `Source/monopoly/UDChat.cpp` | 397 |
| 3 | parcours visible joueur | `Source/monopoly/trade.cpp` | 376 |
| 3 | parcours visible joueur | `Source/monopoly/display.cpp` | 385 |
| 3 | parcours visible joueur | `Source/monopoly/UDBoard.cpp` | 388 |
| 3 | parcours visible joueur | `Source/monopoly/UDIBar.cpp` | 389 |
| 3 | parcours visible joueur | `Source/monopoly/UDOpts.cpp` | 390 |
| 3 | parcours visible joueur | `Source/monopoly/UDStats.cpp` | 394 |
| 3 | parcours visible joueur | `Source/monopoly/UDTrade.cpp` | 395 |
| 3 | parcours visible joueur | `Source/artlib/L_Fonts.*', 'L_Print.*` | 419 |
| 3 | parcours visible joueur | `cameras, viewports, background ('camera.*', 'D3DDevice.*', view code)` | 428 |
| 2 | infrastructure ArtLib active | `Source/monopoly/Lang.cpp` | 381 |
| 2 | infrastructure ArtLib active | `Source/artlib/L_Data.*` | 414 |
| 2 | infrastructure ArtLib active | `Source/artlib/L_Grafix.*', 'L_Rend2D.*', 'L_Sprite.*` | 416 |
| 2 | infrastructure ArtLib active | `Source/artlib/L_Seqncr.*` | 418 |
| 2 | infrastructure ArtLib active | `Source/artlib/L_Sound.*', 'L_Midi.*` | 421 |
| 2 | infrastructure ArtLib active | `Source/artlib/L_Video.*` | 422 |
| 2 | infrastructure ArtLib active | `Commandes 'L_Seqncr` | 450 |

<details><summary>Les 43 entrees PARTIAL</summary>

| Section | Ligne legacy | Ligne matrice |
|---|---|---:|
| Jeu Monopoly | `Source/monopoly/GameInc.cpp/.h` | 372 |
| Jeu Monopoly | `Source/monopoly/Mess.cpp` | 374 |
| Jeu Monopoly | `Source/monopoly/Rule.cpp` | 375 |
| Jeu Monopoly | `Source/monopoly/trade.cpp` | 376 |
| Jeu Monopoly | `Source/monopoly/Ai.cpp` | 377 |
| Jeu Monopoly | `Source/monopoly/Ai_trade.cpp` | 379 |
| Jeu Monopoly | `Source/monopoly/Ai_util.cpp` | 380 |
| Jeu Monopoly | `Source/monopoly/Lang.cpp` | 381 |
| Jeu Monopoly | `Source/monopoly/display.cpp` | 385 |
| Jeu Monopoly | `Source/monopoly/Userifce.cpp` | 386 |
| Jeu Monopoly | `Source/monopoly/UDBoard.cpp` | 388 |
| Jeu Monopoly | `Source/monopoly/UDIBar.cpp` | 389 |
| Jeu Monopoly | `Source/monopoly/UDOpts.cpp` | 390 |
| Jeu Monopoly | `Source/monopoly/UDPieces.cpp` | 391 |
| Jeu Monopoly | `Source/monopoly/UDPsel.cpp` | 392 |
| Jeu Monopoly | `Source/monopoly/UDSound.cpp` | 393 |
| Jeu Monopoly | `Source/monopoly/UDStats.cpp` | 394 |
| Jeu Monopoly | `Source/monopoly/UDTrade.cpp` | 395 |
| Jeu Monopoly | `Source/monopoly/UDChat.cpp` | 397 |
| Jeu Monopoly | `Source/monopoly/UDPenny.cpp` | 402 |
| Jeu Monopoly | `Source/monopoly/UDUtils.cpp` | 403 |
| Services ArtLib consommes | `Source/artlib/L_UIMsg.*` | 413 |
| Services ArtLib consommes | `Source/artlib/L_Data.*` | 414 |
| Services ArtLib consommes | `Source/artlib/L_Grafix.*', 'L_Rend2D.*', 'L_Sprite.*` | 416 |
| Services ArtLib consommes | `Source/artlib/L_Rend3D.*` | 417 |
| Services ArtLib consommes | `Source/artlib/L_Seqncr.*` | 418 |
| Services ArtLib consommes | `Source/artlib/L_Fonts.*', 'L_Print.*` | 419 |
| Services ArtLib consommes | `Source/artlib/L_Sound.*', 'L_Midi.*` | 421 |
| Services ArtLib consommes | `Source/artlib/L_Video.*` | 422 |
| PC3D consomme | `cameras, viewports, background ('camera.*', 'D3DDevice.*', view code)` | 428 |
| PC3D consomme | `meshes/scenes/materials ('mesh*', 'NewMesh*', 'Scene.h', 'l_material.h')` | 429 |
| PC3D consomme | `decodeur HMD / postload MESHX ('HMDData.h', 'NewMesh.cpp', 'hmdload.*')` | 430 |
| Donnees et verification | `Lifecycle, lookup, metadata et ownership` | 440 |
| Donnees et verification | `CRC global DAT` | 442 |
| Donnees et verification | `Parseurs semantiques CNK / sequence` | 446 |
| Donnees et verification | `Arbre runtime et execution` | 449 |
| Donnees et verification | `Commandes 'L_Seqncr` | 450 |
| Donnees et verification | `Transformations / tweekers` | 451 |
| Donnees et verification | `MESHX runtime` | 452 |
| Donnees et verification | `Render data de sequence` | 453 |
| Donnees et verification | `Raccordement sequence -> render slots` | 454 |
| Donnees et verification | `LANG core` | 455 |
| Donnees et verification | `Loader BMP runtime` | 459 |

</details>

## Signaux informatifs

- Marqueurs source/tests (`TODO`, `FIXME`, `XXX`, `TBD`) : **0**.
- Fichiers source modernes `.cpp` dont le nom n est pas cite litteralement dans PORTING_STATUS : **196**. Ce signal reste informatif car un helper peut legitimement etre couvert par une ligne de famille.
- Premiers noms non cites : `AICounterTradeRuntime.cpp`, `AIDecisionUtility.cpp`, `AIMessageIngress.cpp`, `AIProfile.cpp`, `AIProfileRuntime.cpp`, `AISaveState.cpp`, `AITradeIngress.cpp`, `AITradeSendRuntime.cpp`, `AITradeUtility.cpp`, `AIUtility.cpp`, `Application.cpp`, `AuctionPennyBagsPlayback.cpp`, `AuctionPlayback.cpp`, `AuctionUI.cpp`, `AudioRuntime.cpp`, `BitmapRuntime.cpp`, `BoardBackdropPlayback.cpp`, `BoardCameraController.cpp`, `BoardGeometry.cpp`, `BoardLightingController.cpp`, `BoardOwnershipHighlight.cpp`, `BoardRules.cpp`, `BoardTextureRuntime.cpp`, `CardDeckRuntime.cpp`, `CardDecks.cpp`

## Limites d interpretation

- CMake et CTest ne prouvent que la coherence build/tests.
- `PORTING_PARTIAL` remains partial until its documented omissions are closed or explicitly excluded by caller/content evidence.
- Les DAT/HMD retail, la parite visuelle, l audio/reseau physique et les parties completes demandent une qualification separee.
- Le SVG genere separe volontairement l audit fonctionnel manuel des pourcentages mecaniques.
