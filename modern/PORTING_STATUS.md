# Monopoly 1999 -> modern porting status

Cette matrice est la carte unique du port moderne. La source originale reste
l'autorite semantique; les statuts ci-dessous decrivent uniquement le code
effectivement present sous `modern/`.

## Legende

- `PORTED_COMPLETE` : contrat original identifie, integre et verifie sans
  omission connue dans le perimetre indique.
- `PORTED_PARTIAL` : une partie reelle du comportement existe, mais le module
  conserve des omissions connues.
- `NOT_STARTED` : aucun equivalent fonctionnel significatif n'existe encore.
- `REPLACED_PORTABLE` : le service legacy a un equivalent portable couvrant
  le contrat consomme par Monopoly.
- `BLOCKED_MISSING_DATA` : le code ou les identifiants existent, mais les
  payloads necessaires ne sont pas disponibles et ne doivent pas etre inventes.
- `LEGACY_UNUSED` : le callgraph a prouve que Monopoly ne consomme pas ce
  composant. Ne pas utiliser ce statut sans preuve.

## Jeu Monopoly

| Original | Equivalent moderne | Statut | Symboles originaux importants | Dependances | Travail restant / blocage |
|---|---|---|---|---|---|
| `Source/monopoly/Main.cpp` | `Application`, `Engine`, `Game`, `RenderSlots`, `Timers`, `GPUFrame` | `PORTED_PARTIAL` | `GameInitialise`, `InitRenderSlots`, `ProcessUIMessage`, `GameShutdown`, `GameUpdateCycle` | SDL3, SDL_GPU, DISPLAY, UI messages | Les slots ne pilotent pas encore un vrai renderer 2D/3D; completer les chemins d'erreur et les tests de lifecycle avec fakes GPU. |
| `Source/monopoly/GameInc.cpp/.h` | `RuleTypes`, `DataBanks`, includes modernes explicites | `PORTED_PARTIAL` | constantes `DAT_*`, `MAIN_GAME_TIMER`, configuration de build | Tous les sous-systemes | Les contrats de groupes sont portes; continuer a remplacer le precompiled-header implicite par des interfaces explicites. |
| `Source/monopoly/Mdef.cpp` | `RuleTypes`, `RuntimeState`, `BoardRules` | `PORTED_PARTIAL` | constantes de jeu, langues, plateaux, joueurs | RULE, LANG, DISPLAY | Comparaison systematique des constantes encore a terminer. |
| `Source/monopoly/Mess.cpp` | `Messaging` | `PORTED_PARTIAL` | `MESS_InitializeSystem`, `MESS_SendAction`, `MESS_ReceiveActionMessage`, modes serveur/reseau | RULE, UI, futur transport | File locale fonctionnelle; DirectPlay/Winsock volontairement absents; saturation, voice-chat et transport futur restent a porter. |
| `Source/monopoly/Rule.cpp` | `RulesEngine`, `Rule*`, `BoardRules`, `PhaseStack`, `CardDeck*` | `PORTED_PARTIAL` | creation, phases, tours, economie, cartes, prison, enchere, trade, save/resync | Messaging, RNG, donnees plateau | Couverture importante mais inventaire symbole-par-symbole et scenarios complets encore requis; ne pas reecrire RULE. |
| `Source/monopoly/trade.cpp` | `RuleTrade` | `PORTED_PARTIAL` | editions, acceptations, immunites, loyers futurs | RULE, Messaging, UI trade | Logique RULE largement presente; ecran `UDTrade` et validation scenario bout-en-bout absents. |
| `Source/monopoly/Ai.cpp` | Aucun | `NOT_STARTED` | boucle de decision, achat, enchere, prison, construction | RULE, `Ai_load`, donnees AI | Porter l'architecture de decision; aucun fallback aleatoire ne doit la remplacer. |
| `Source/monopoly/Ai_load.cpp` | Aucun | `NOT_STARTED` | chargement `Boot.ai`, `Normal.ai`, parametres | fichiers `*.AI`, parser | Identifier le format et les fichiers reels avant implementation. |
| `Source/monopoly/Ai_trade.cpp` | Aucun | `NOT_STARTED` | evaluation et negociation de trades | AI, RuleTrade | Porter apres le noyau AI et ajouter des tests deterministes. |
| `Source/monopoly/Ai_util.cpp` | Aucun | `NOT_STARTED` | evaluations joueurs/proprietes/cash | AI, BoardRules | Extraire les calculs purs puis tester contre les constantes originales. |
| `Source/monopoly/Lang.cpp` | contrats `DataBanks` et `LegacyTextIds` uniquement | `BLOCKED_MISSING_DATA` | `LANG_InitializeSystem`, `StartupExternalLanguage`, `GetLanguageString`, formatage nombres/monnaie | DAT langue, index table, UTF-16LE, fonts | Les `dat_lnNN.dat` ne sont pas presents. Porter lookup/fallback/formatage quand une reconstruction ou fixture fidele est disponible; ne pas inventer les chaines. |
| `Source/monopoly/TexInfo.cpp` | Aucun catalogue complet | `NOT_STARTED` | tableaux de textures villes/plateaux, positions | BMP bruts, resolver d'assets | Porter les tableaux immuables et chemins reels, avec tests de tailles/coordonnees et gestion de casse portable. |
| `Source/monopoly/Tickler.cpp` | `TimeStep`, `Messaging`, `UserInterface` | `PORTED_PARTIAL` | `AdvanceTimeStep`, action unique par cycle, `ACTION_TICK` | Timers, RULE, AI | Routage local/broadcast et reset moderne presents; verrou de game queue, AI et voice-chat restent absents. |
| `Source/monopoly/L_voice.cpp` | Aucun | `NOT_STARTED` | voix, capture/lecture, timing | audio portable, MESS | Identifier les contrats consommes; ne pas porter les codecs/wrappers Win32 litteralement. |
| `Source/monopoly/display.cpp` | `Display`, `GPUFrame`, `RenderSlots`, `LogicalViewport` | `PORTED_PARTIAL` | `DISPLAY_initialize`, `DISPLAY_tickActions`, `DISPLAY_showAll2`, `DISPLAY_destroy` | modules UD, renderer, assets | Ordre relatif, cycle desired/current et repere logique letterbox conserves; renderer 2D et nombreux modules manquent. |
| `Source/monopoly/Userifce.cpp` | `UserInterface`, `LocalPlayers`, `TimeStep`, `ExtendedInitialization` | `PORTED_PARTIAL` | `MainExtendedInitialization`, `ProcessLibraryMessage`, `ProcessMessageToPlayer`, `ProcessPlayersUI` | RULE, DISPLAY, LANG, CHAT | Frontiere locale retablie; `NOTIFY_GAME_STARTING` et pause sont routes. Le miroir UI reste incomplet pour cash, plateau, trade, enchere, prison, etc. |
| `Source/monopoly/UDAuct.cpp` | `RuleAuction` couvre seulement la regle | `NOT_STARTED` | init/destroy/tick/show/process message de l'ecran enchere | DISPLAY, IBar, RULE auction | Creer le module UI sans dupliquer l'etat authoritative de `RuleAuction`. |
| `Source/monopoly/UDBoard.cpp` | etat de backdrop dans `Display`, geometie dans `BoardGeometry` | `PORTED_PARTIAL` | `UDBOARD_SetBackdrop`, `DISPLAY_UDBOARD_Show` | assets plateau, renderer, PC3D | Valeurs des huit ecrans, etat initial invalide, commit differe et mapping Main/Portfolio/Trade portes; composition et hotspots absents. |
| `Source/monopoly/UDIBar.cpp` | `IBar`, `IBarLayout` | `PORTED_PARTIAL` | initialize/destroy/tick/show/process, filtrage joueurs | LocalPlayers, DISPLAY, assets/fonts | Visibilite setup/Main/Portfolio/Trade et layout purs presents; aucun dessin reel, icones, textes, cash ou animation complete. |
| `Source/monopoly/UDOpts.cpp` | Aucun | `NOT_STARTED` | options UI | persistence options, DISPLAY | Porter apres le resolver de ressources et le renderer UI. |
| `Source/monopoly/UDPieces.cpp` | geometrie de plateau partielle seulement | `NOT_STARTED` | pieces, positions, animations, cameras | PC3D moderne, BoardGeometry, RULE | Necessite mesh/camera/scene SDL_GPU. |
| `Source/monopoly/UDPsel.cpp` | `PlayerSelection`, `PlayerSetupFlow`, `LocalPlayers`, `IBar` | `PORTED_PARTIAL` | phases, noms, tokens, humains/IA, add/remove/start | UserInterface, Messaging, DISPLAY | Commit de phase limite a `DISPLAY_UDPSEL_Show`, refresh one-shot, reset zero-joueur et initialisation de la premiere notification non nulle portes; SelectCity/regles, projections mortes et rendu restent a traiter. |
| `Source/monopoly/UDSound.cpp` | Aucun | `NOT_STARTED` | options son, volume, musique/SFX | service audio portable, persistence | Choisir l'equivalent portable apres inventaire des assets et callers. |
| `Source/monopoly/UDStats.cpp` | Aucun | `NOT_STARTED` | statistiques joueur/partie | miroir UI, fonts, assets | Porter state/layout/show/process apres consolidation du miroir UI. |
| `Source/monopoly/UDTrade.cpp` | `RuleTrade` couvre seulement la regle | `NOT_STARTED` | editeur, acceptance, presentation trade | miroir UI, RuleTrade, DISPLAY | L'UI de trade reste entierement a porter. |
| `Source/monopoly/UDCGE.cpp` | Aucun | `NOT_STARTED` | editeur/outil runtime appele par le jeu | DISPLAY, data | Determiner le callgraph runtime exact avant de classer ou porter. |
| `Source/monopoly/UDChat.cpp` | Aucun; `Messaging` accepte les futurs messages | `NOT_STARTED` | `CHAT_InitializeSystem`, saisie, `ACTION_TEXT_CHAT` | fonts, DAT_MAIN, LANG, transport | Remplacer le controle d'edition Win32; ressources DAT actuellement manquantes. |
| `Source/monopoly/UDPlrCfg.cpp` | Aucun | `NOT_STARTED` | configuration joueur | PlayerSelection, options, assets | Comparer les ecrans et transitions avant implementation. |
| `Source/monopoly/UDPlrSum.cpp` | Aucun | `NOT_STARTED` | resume joueur | miroir UI, DISPLAY | Porter apres les notifications de jeu dans le miroir UI. |
| `Source/monopoly/UDRules.cpp` | `RuleOptions` couvre la regle, pas l'ecran | `NOT_STARTED` | choix/reglage des regles | RuleOptions, PlayerSetupFlow, DISPLAY | Implementer une projection controlee de `RuleOptions`. |
| `Source/monopoly/UDTitle.cpp` | Aucun | `NOT_STARTED` | title/opening flow | video, assets, DISPLAY | Identifier les ecrans et l'ordre; Bink/AVI seront remplaces par un service portable. |
| `Source/monopoly/UDPenny.cpp` | Aucun | `NOT_STARTED` | Penny Bags/assistant | animations, audio, assets | Porter seulement les comportements effectivement appeles. |
| `Source/monopoly/UDUtils.cpp` | `UDUtils` | `PORTED_PARTIAL` | chemins, INI, helpers UI | filesystem, assets | Le resolver de chemins est inerte; fusionner avec DataBanks/LegacyAssets sans chemins machine. |
| `Source/monopoly/Unility.cpp` | Aucun mapping complet confirme | `NOT_STARTED` | utilitaires de jeu | callers a inventorier | Etablir definitions et callers avant tout port. |
| `Source/monopoly/Debugart.cpp` | Aucun | `NOT_STARTED` | outils/debug runtime | callgraph | Ne classer `LEGACY_UNUSED` qu'apres preuve par callgraph. |

## Services ArtLib consommes

| Original | Equivalent moderne | Statut | Contrat consomme | Dependances | Travail restant / blocage |
|---|---|---|---|---|---|
| `Source/artlib/L_Main.*` | `Application`, `Engine`, `Game` | `REPLACED_PORTABLE` | boucle evenementielle, init/shutdown | SDL3 | Fenetre redimensionnable/HiDPI, presentation, erreurs de startup et formats GPU Vulkan/D3D12/Metal sont raccordes; test de lifecycle GPU et smoke test interactif restent requis. |
| `Source/artlib/L_Timers.*` | `Timers` + `UIMessages` | `REPLACED_PORTABLE` | horloge 60 Hz, 4 timers, speed/restart, evenement index+tick | steady_clock, file UI | Contrat actuellement raccorde teste de facon deterministe; les appels historiques `LE_TIMER_Delay` identifies dans `Main`, `UDUtils` et les diagnostics DISPLAY appartiennent a des chemins remplaces, inactifs ou encore differes. |
| `Source/artlib/L_UIMsg.*` | `UIMessages` | `PORTED_PARTIAL` | FIFO 100, evenements timer/input, delestage | Application, Timers | Ajouter davantage de tests de pression/coalescence et les types requis par les futurs modules. |
| `Source/artlib/L_Data.*` | `DataBanks`, futurs chargeur/resolver | `BLOCKED_MISSING_DATA` | DataId 16:16, groupes, index DAT, chargement paresseux | PKWARE explode, filesystem, formats bitmap/audio | Aucun DAT retail present; contrats d'identifiants portes, payload/loader non implemente. |
| `Source/artlib/L_Chunk.*` | Aucun | `BLOCKED_MISSING_DATA` | chunks 24-bit size + 8-bit id pour le sequenceur | donnees CNK, L_Seqncr | Aucun CNK reel present; decoder explicitement les octets, jamais les bitfields ABI. |
| `Source/artlib/L_Grafix.*`, `L_Rend2D.*`, `L_Sprite.*` | `Display`, futur renderer UI SDL_GPU | `PORTED_PARTIAL` | composition 2D, clipping, priorites, surfaces | SDL_GPU, assets, transformation 800x600 | Seul le cycle d'etat existe; rendu des objets/sprites/fonts a construire. |
| `Source/artlib/L_Rend3D.*` | `GPUFrame` et futur renderer 3D | `PORTED_PARTIAL` | viewport et fond 3D | SDL_GPU, PC3D moderne | Fond BMP seulement; scene, camera, mesh, materiaux et animations absents. |
| `Source/artlib/L_Seqncr.*` | Aucun | `NOT_STARTED` | sequences UI/audio/video et render priorities | L_Chunk, assets, timers | Cartographier les sous-contrats reellement appeles puis creer un sequenceur portable. |
| `Source/artlib/L_Fonts.*`, `L_Print.*` | Aucun | `NOT_STARTED` | Arial 10, mesure/rendu de texte | font rasterizer portable, LANG | Choisir un backend portable et conserver metriques/layout observables. |
| `Source/artlib/L_Keybrd.*`, `L_Mouse.*` | traduction SDL dans `Application`, `MousePointer` partiel | `REPLACED_PORTABLE` | input clavier/souris | SDL3, `LogicalViewport` | Souris reconvertie vers 800x600 et bandes noires rejetees; rendu du pointeur et certains types d'evenements restent partiels. |
| `Source/artlib/L_Sound.*`, `L_Midi.*` | Aucun | `NOT_STARTED` | WAV, musique, mixage, voice | audio portable, data | Inventorier les appels Monopoly et remplacer DirectSound/MIDI legacy. |
| `Source/artlib/L_Video.*` | Aucun | `NOT_STARTED` | opening movies | decoder/service video portable | Identifier fichiers et timing avant choix technique; ne pas porter VFW/Bink litteralement. |

## PC3D consomme

| Original | Equivalent moderne | Statut | Contrat consomme | Dependances | Travail restant / blocage |
|---|---|---|---|---|---|
| cameras, viewports, background (`camera.*`, `D3DDevice.*`, view code) | `GPUFrame`, `Display::Viewport3D`, `LogicalViewport` | `PORTED_PARTIAL` | rectangles Main/Status/Trade, fond et clear | SDL_GPU | Rectangles mis a l'echelle dans le viewport letterbox; cameras, scene et projection 3D reelles restent a porter. |
| meshes/scenes/materials (`mesh*`, `NewMesh*`, `Scene.h`, `l_material.h`) | Aucun | `NOT_STARTED` | plateau et pieces effectivement references | HMD/MESHX, textures, SDL_GPU | Faire l'inventaire des services appeles; ne pas porter PC3D entier. |
| chargeur HMD/MESHX (`hmdload.*`) | Aucun | `BLOCKED_MISSING_DATA` | modeles 3D DAT | DAT_3D, decompression | Headers/tags disponibles, payloads retail absents. |
| vieux DirectDraw/Direct3D drivers (`DDraw*`, `D3DDevice*`) | SDL3 / SDL_GPU | `REPLACED_PORTABLE` | creation device, swapchain, soumission | SDL3 | Les implementations legacy ne seront pas portees; completer seulement les comportements de rendu consommes. |

## Donnees et verification

- Les headers generes `Dat_Mon/*.h` et les BMP bruts sont disponibles.
- Les banques compilees `dat_main.dat`, `dat_pat.dat`, `dat_bord.dat`,
  `dat_brd2.dat`, `dat_3d.dat`, `dat_lnNN.dat`, `dat_lmNN.dat` et
  `dat_lkNN.dat` ne sont pas presentes dans le depot inspecte.
- Une absence de DAT ne justifie jamais l'invention d'un chunk, d'une chaine,
  d'un offset ou d'une texture.
- Validation requise pour chaque lot : configure CMake reussi, build reussi,
  puis CTest. Un ancien executable de test ne constitue pas une validation.

### Derniere validation locale

Le 2026-08-24, avec MSVC 19.50 et le generateur Visual Studio de CMake :

- configuration Debug existante revalidee avec la source SDL 3.4.14 locale ;
- executable `MonopolyModern` compile et lie ;
- toutes les cibles de tests compilees ;
- CTest : 10/10 suites passees apres le lot courant.

Cette validation prouve la compilation et les contrats automatises. Elle ne
prouve pas encore un parcours interactif complet ni le rendu sur un GPU reel.

## Prochaines priorites

1. Ajouter un smoke test runtime/injectable de la colonne vertebrale
   timer -> UIMessages -> TimeStep -> RULE/UI -> Display -> GPU.
2. Completer le parcours Player Select : SelectCity, regles standard/custom,
   `NotifyProposedConfiguration`, puis transition de jeu verifiee.
3. Consolider le resolver de ressources et porter le catalogue `TexInfo` a
   partir des donnees brutes verifiables.
4. Completer le miroir UI notification par notification, puis rendre IBar et
   Player Select sans dupliquer l'etat RULE.
