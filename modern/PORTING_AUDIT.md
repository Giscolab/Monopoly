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
| 4 | gameplay/runtime central | `Source/monopoly/Rule.cpp` | 465 |
| 4 | gameplay/runtime central | `Source/monopoly/Userifce.cpp` | 476 |
| 4 | gameplay/runtime central | `Source/monopoly/UDPsel.cpp` | 482 |
| 4 | gameplay/runtime central | `Source/monopoly/UDChat.cpp` | 487 |
| 3 | parcours visible joueur | `Source/monopoly/trade.cpp` | 466 |
| 3 | parcours visible joueur | `Source/monopoly/display.cpp` | 475 |
| 3 | parcours visible joueur | `Source/monopoly/UDBoard.cpp` | 478 |
| 3 | parcours visible joueur | `Source/monopoly/UDIBar.cpp` | 479 |
| 3 | parcours visible joueur | `Source/monopoly/UDOpts.cpp` | 480 |
| 3 | parcours visible joueur | `Source/monopoly/UDStats.cpp` | 484 |
| 3 | parcours visible joueur | `Source/monopoly/UDTrade.cpp` | 485 |
| 3 | parcours visible joueur | `Source/artlib/L_Fonts.*', 'L_Print.*` | 509 |
| 3 | parcours visible joueur | `cameras, viewports, background ('camera.*', 'D3DDevice.*', view code)` | 518 |
| 2 | infrastructure ArtLib active | `Source/monopoly/Lang.cpp` | 471 |
| 2 | infrastructure ArtLib active | `Source/artlib/L_Data.*` | 504 |
| 2 | infrastructure ArtLib active | `Source/artlib/L_Grafix.*', 'L_Rend2D.*', 'L_Sprite.*` | 506 |
| 2 | infrastructure ArtLib active | `Source/artlib/L_Seqncr.*` | 508 |
| 2 | infrastructure ArtLib active | `Source/artlib/L_Sound.*', 'L_Midi.*` | 511 |
| 2 | infrastructure ArtLib active | `Source/artlib/L_Video.*` | 512 |
| 2 | infrastructure ArtLib active | `Commandes 'L_Seqncr` | 540 |

<details><summary>Les 43 entrees PARTIAL</summary>

| Section | Ligne legacy | Ligne matrice |
|---|---|---:|
| Jeu Monopoly | `Source/monopoly/GameInc.cpp/.h` | 462 |
| Jeu Monopoly | `Source/monopoly/Mess.cpp` | 464 |
| Jeu Monopoly | `Source/monopoly/Rule.cpp` | 465 |
| Jeu Monopoly | `Source/monopoly/trade.cpp` | 466 |
| Jeu Monopoly | `Source/monopoly/Ai.cpp` | 467 |
| Jeu Monopoly | `Source/monopoly/Ai_trade.cpp` | 469 |
| Jeu Monopoly | `Source/monopoly/Ai_util.cpp` | 470 |
| Jeu Monopoly | `Source/monopoly/Lang.cpp` | 471 |
| Jeu Monopoly | `Source/monopoly/display.cpp` | 475 |
| Jeu Monopoly | `Source/monopoly/Userifce.cpp` | 476 |
| Jeu Monopoly | `Source/monopoly/UDBoard.cpp` | 478 |
| Jeu Monopoly | `Source/monopoly/UDIBar.cpp` | 479 |
| Jeu Monopoly | `Source/monopoly/UDOpts.cpp` | 480 |
| Jeu Monopoly | `Source/monopoly/UDPieces.cpp` | 481 |
| Jeu Monopoly | `Source/monopoly/UDPsel.cpp` | 482 |
| Jeu Monopoly | `Source/monopoly/UDSound.cpp` | 483 |
| Jeu Monopoly | `Source/monopoly/UDStats.cpp` | 484 |
| Jeu Monopoly | `Source/monopoly/UDTrade.cpp` | 485 |
| Jeu Monopoly | `Source/monopoly/UDChat.cpp` | 487 |
| Jeu Monopoly | `Source/monopoly/UDPenny.cpp` | 492 |
| Jeu Monopoly | `Source/monopoly/UDUtils.cpp` | 493 |
| Services ArtLib consommes | `Source/artlib/L_UIMsg.*` | 503 |
| Services ArtLib consommes | `Source/artlib/L_Data.*` | 504 |
| Services ArtLib consommes | `Source/artlib/L_Grafix.*', 'L_Rend2D.*', 'L_Sprite.*` | 506 |
| Services ArtLib consommes | `Source/artlib/L_Rend3D.*` | 507 |
| Services ArtLib consommes | `Source/artlib/L_Seqncr.*` | 508 |
| Services ArtLib consommes | `Source/artlib/L_Fonts.*', 'L_Print.*` | 509 |
| Services ArtLib consommes | `Source/artlib/L_Sound.*', 'L_Midi.*` | 511 |
| Services ArtLib consommes | `Source/artlib/L_Video.*` | 512 |
| PC3D consomme | `cameras, viewports, background ('camera.*', 'D3DDevice.*', view code)` | 518 |
| PC3D consomme | `meshes/scenes/materials ('mesh*', 'NewMesh*', 'Scene.h', 'l_material.h')` | 519 |
| PC3D consomme | `decodeur HMD / postload MESHX ('HMDData.h', 'NewMesh.cpp', 'hmdload.*')` | 520 |
| Donnees et verification | `Lifecycle, lookup, metadata et ownership` | 530 |
| Donnees et verification | `CRC global DAT` | 532 |
| Donnees et verification | `Parseurs semantiques CNK / sequence` | 536 |
| Donnees et verification | `Arbre runtime et execution` | 539 |
| Donnees et verification | `Commandes 'L_Seqncr` | 540 |
| Donnees et verification | `Transformations / tweekers` | 541 |
| Donnees et verification | `MESHX runtime` | 542 |
| Donnees et verification | `Render data de sequence` | 543 |
| Donnees et verification | `Raccordement sequence -> render slots` | 544 |
| Donnees et verification | `LANG core` | 545 |
| Donnees et verification | `Loader BMP runtime` | 549 |

</details>

## Signaux informatifs

- Marqueurs source/tests (`TODO`, `FIXME`, `XXX`, `TBD`) : **0**.
- Fichiers source modernes `.cpp` dont le nom n est pas cite litteralement dans PORTING_STATUS : **199**. Ce signal reste informatif car un helper peut legitimement etre couvert par une ligne de famille.
- Premiers noms non cites : `AICounterTradeRuntime.cpp`, `AIDecisionUtility.cpp`, `AIMessageIngress.cpp`, `AIProfile.cpp`, `AIProfileRuntime.cpp`, `AISaveState.cpp`, `AITradeIngress.cpp`, `AITradeSendRuntime.cpp`, `AITradeUtility.cpp`, `AIUtility.cpp`, `Application.cpp`, `AuctionPennyBagsPlayback.cpp`, `AuctionPlayback.cpp`, `AuctionUI.cpp`, `AudioRuntime.cpp`, `BitmapRuntime.cpp`, `BoardBackdropPlayback.cpp`, `BoardCameraController.cpp`, `BoardGeometry.cpp`, `BoardLightingController.cpp`, `BoardOwnershipHighlight.cpp`, `BoardRules.cpp`, `BoardTextureRuntime.cpp`, `CardDeckRuntime.cpp`, `CardDecks.cpp`

## Limites d interpretation

- CMake et CTest ne prouvent que la coherence build/tests.
- `PORTING_PARTIAL` remains partial until its documented omissions are closed or explicitly excluded by caller/content evidence.
- Les DAT/HMD retail, la parite visuelle, l audio/reseau physique et les parties completes demandent une qualification separee.
- Le SVG genere separe volontairement l audit fonctionnel manuel des pourcentages mecaniques.
