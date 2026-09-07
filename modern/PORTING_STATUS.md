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
- `MISSING_TOOLING` : un outil historique necessaire a une reproduction
  bit-a-bit n'est pas present dans le depot inspecte.
- `LEGACY_UNUSED` : le callgraph a prouve que Monopoly ne consomme pas ce
  composant. Ne pas utiliser ce statut sans preuve.

## Jeu Monopoly

| Original | Equivalent moderne | Statut | Symboles originaux importants | Dependances | Travail restant / blocage |
|---|---|---|---|---|---|
| `Source/monopoly/Main.cpp` | `Application`, `Engine`, `Game`, `RenderSlots`, `Timers`, `GPUFrame` | `PORTED_PARTIAL` | `GameInitialise`, `InitRenderSlots`, `ProcessUIMessage`, `GameShutdown`, `GameUpdateCycle` | SDL3, SDL_GPU, DISPLAY, UI messages | Lifecycle DATA/LANG et timers raccorde. World3D 1 et Overlay2D alimentent maintenant des renderers SDL_GPU reels jusqu au swapchain/offscreen, avec readbacks D3D12; le renderer UI generique et le parcours interactif retail restent incomplets. |
| `Source/monopoly/GameInc.cpp/.h` | `RuleTypes`, `DataBanks`, `LegacyDataArchive`, includes modernes explicites | `PORTED_PARTIAL` | constantes `DAT_*`, `MAIN_GAME_TIMER`, configuration de build | Tous les sous-systemes | Groupes, chemins de banques et triplets de langue sont portes; continuer a remplacer le precompiled-header implicite par des interfaces explicites. |
| `Source/monopoly/Mdef.cpp` | `RuleTypes`, `RuntimeState`, `BoardRules` | `PORTED_PARTIAL` | constantes de jeu, langues, plateaux, joueurs | RULE, LANG, DISPLAY | Comparaison systematique des constantes encore a terminer. |
| `Source/monopoly/Mess.cpp` | `Messaging` | `PORTED_PARTIAL` | `MESS_InitializeSystem`, `MESS_SendAction`, `MESS_ReceiveActionMessage`, modes serveur/reseau | RULE, UI, futur transport | File locale fonctionnelle; DirectPlay/Winsock volontairement absents; saturation, voice-chat et transport futur restent a porter. |
| `Source/monopoly/Rule.cpp` | `RulesEngine`, `Rule*`, `BoardRules`, `PhaseStack`, `CardDeck*` | `PORTED_PARTIAL` | creation, phases, tours, economie, cartes, prison, enchere, trade, save/resync | Messaging, RNG, donnees plateau | Couverture importante mais inventaire symbole-par-symbole et scenarios complets encore requis; ne pas reecrire RULE. |
| `Source/monopoly/trade.cpp` | `RuleTrade` | `PORTED_PARTIAL` | editions, acceptations, immunites, loyers futurs | RULE, Messaging, UI trade | Logique RULE largement presente; ecran `UDTrade` et validation scenario bout-en-bout absents. |
| `Source/monopoly/Ai.cpp` | Aucun | `NOT_STARTED` | boucle de decision, achat, enchere, prison, construction | RULE, `Ai_load`, donnees AI | Porter l'architecture de decision; aucun fallback aleatoire ne doit la remplacer. |
| `Source/monopoly/Ai_load.cpp` | Aucun | `NOT_STARTED` | chargement `Boot.ai`, `Normal.ai`, parametres | fichiers `*.AI`, parser | Identifier le format et les fichiers reels avant implementation. |
| `Source/monopoly/Ai_trade.cpp` | Aucun | `NOT_STARTED` | evaluation et negociation de trades | AI, RuleTrade | Porter apres le noyau AI et ajouter des tests deterministes. |
| `Source/monopoly/Ai_util.cpp` | Aucun | `NOT_STARTED` | evaluations joueurs/proprietes/cash | AI, BoardRules | Extraire les calculs purs puis tester contre les constantes originales. |
| `Source/monopoly/Lang.cpp` | `LanguageCatalog`, `LanguageService`, `ResourceRuntime`, `DataBanks`, `LegacyTextIds` | `PORTED_PARTIAL` | `LANG_InitializeSystem`, `StartupExternalLanguage`, `GetLanguageString`, fallback, clean | lecteur DAT, index packed, UTF-16LE | Contrats existants conserves; startup publie les trois banques LANG partagees avec le registre DATA avant MESS. Les snapshots anciens conservent des chargements non caches apres remplacement/arret. Restent la migration des callers UI, fonts/mesures/impression et le formatage locale; textes retail bloques separement. |
| `Source/monopoly/TexInfo.cpp` | `TextureCatalog` | `PORTED_COMPLETE` | tableaux USA/Europe, recettes 8/16/14/22, overlays, 39 vues 2D, tags HMD | BMP bruts, futur resolver/mesh | Catalogue CPU, ordre, coordonnees, noms et anomalie `CityHigh128 -> *_256.BMP` sont portes et testes. Les meshes/resolutions hors contrat produisent une erreur typee avant toute indexation. L'application GPU/PC3D des substitutions appartient au futur chargeur de mesh. |
| `Source/monopoly/Tickler.cpp` | `TimeStep`, `Messaging`, `UserInterface`, `GameQueueGate` | `PORTED_PARTIAL` | `AdvanceTimeStep`, action unique par cycle, `ACTION_TICK`, `gameQueueLock/gameQueueUnLock` | Timers, RULE, DISPLAY, AI | Routage local/broadcast, reset et compteur de verrou animation sont portes; le gate bloque la consommation RULE avec le failsafe historique de 15 s / 900 ticks. AI et voice-chat restent absents. |
| `Source/monopoly/L_voice.cpp` | Aucun | `NOT_STARTED` | voix, capture/lecture, timing | audio portable, MESS | Identifier les contrats consommes; ne pas porter les codecs/wrappers Win32 litteralement. |
| `Source/monopoly/display.cpp` | `Display`, `GPUFrame`, `RenderSlots`, `LogicalViewport`, `World2DRenderer` | `PORTED_PARTIAL` | `DISPLAY_initialize`, `DISPLAY_tickActions`, `DISPLAY_showAll2`, `DISPLAY_destroy` | modules UD, renderer, assets | Ordre relatif, cycle desired/current et repere logique letterbox conserves. Le slot Overlay2D rend maintenant les feuilles bitmap du sequenceur via SDL_GPU dans le canvas logique 800x600; sprites/UI generiques, fonts et nombreux modules UD restent a porter. |
| `Source/monopoly/Userifce.cpp` | `UserInterface`, `LocalPlayers`, `RuntimeState`, `TimeStep`, `ExtendedInitialization`, `ResourceRuntime`, `IBarRuleState` | `PORTED_PARTIAL` | `MainExtendedInitialization`, `ProcessLibraryMessage`, `ProcessMessageToPlayer`, `ProcessPlayersUI`, `GameInProgress`, projection des etats UDIBar | RULE, DISPLAY, LANG, CHAT | Cinq banques core puis LANG avant MESS raccordes; absence/corruption bloque le startup avec erreur typee. Frontiere locale, `NOTIFY_GAME_STARTING`, pause et `GameInProgress` sont routes. `IBarRuleState` conserve les valeurs numeriques `IBAR_STATES` et projette les notifications de tour, paiement, achat/enchere, prison, cartes, unmortgage gratuit, taxe, placement/decomposition et GameOver, avec reset exact apres les actions acceptees actuellement couvertes. Le miroir UI reste incomplet pour cash, plateau, trade, enchere detaillee et autres etats interactifs. |
| `Source/monopoly/UDAuct.cpp` | `RuleAuction` couvre seulement la regle | `NOT_STARTED` | init/destroy/tick/show/process message de l'ecran enchere | DISPLAY, IBar, RULE auction | Creer le module UI sans dupliquer l'etat authoritative de `RuleAuction`. |
| `Source/monopoly/UDBoard.cpp` | `Display`, `BoardCameraController`, `BoardGeometry`, `BoardBackdropPlayback`, `BoardOwnershipHighlight`, `BoardLightingController`, `World3DRenderer` | `PORTED_PARTIAL` | `UDBOARD_SetBackdrop`, `DISPLAY_UDBOARD_Show`, backdrop 2D runtime, ownership/mortgage bars, presets/camera waiting et interpolation `TickActions` | assets plateau, renderer, PC3D | Valeurs des huit ecrans, etat initial invalide, commit differe et mapping Main/Portfolio/Trade portes. `game3DOn` est maintenant distinct de `board3DOn`, rendant le chemin Main 2D reellement atteignable. `BoardBackdropPlayback` reproduit le cache legacy : quatre surfaces DataNative 800x450 pour Main, une surface partagee 400x225 pour Portfolio/Trade, `BMP_mybs*`/`BMP_mybss*` par camera, priorite 10, `StartCXYSlot`, offsets viewport et remplacement Stop/Start/Move transactionnel. Pour USA, les deux familles DataBMP utilisent maintenant l index retail `camera + 39*city` pour les villes 0..10; le cache Main identifie explicitement `(city,camera)` et une ville invalide est rejetee avant allocation ou publication. Les 39 `CameraAngles2D`, le slot waiting unique, l interpolation accel/decel sur 75 ticks, force-interrupt hors 3D et revalidation apres override sont portes/testes. `UDBOARD_ProcessMessage` couvre le controle manuel souris du viewport 3D : deltas logiques C/D, bouton gauche, bouton droit/Ctrl pour l orbite verticale, centre (243,10,243), mouvement initial lineaire 75 ticks, zoom 100..1200 et lock libere strictement apres 1200 ticks avant revalidation du preset. `TickActions` couvre aussi le DemoMode historique : seuil strict >90 s, cycle sequentiel des 39 cameras, durees 51..118 ticks, sortie sur activite et retour a la camera originale. Le floating idle est porte comme un Bezier interruptible de 100 ticks, arme une fois apres un mouvement standard, supprime sur TopDownSquare et module son amplitude selon TokenAnimStack. Les ownership/mortgage bars sont planifiees depuis `GameState` puis jouees transactionnellement : Main 2D utilise les DataUAP `DAT_BOARD/DAT_BOARD2` camera/propriete/couleur avec origine `StartCXYSlot`, Main 3D/Portfolio/Trade utilisent les HMD ownership/mortgage, priorite `12+square` et transformations exactes des quatre cotes. Pour `USA_VERSION`, `UDBOARD_CompileBackdrop` retourne avant les overlays ville/systeme; le chemin custom `city<0` depend encore des DataID externes `g_aid2DBoards` et reste differe. La selection 3D USA suit maintenant exactement `city == 0 ? HMD_boardmed : HMD_board_citymed`, y compris le futur sentinel custom non nul. Le bloc `LIGHTING` est maintenant porte sans simulation : `BoardLightingController` reproduit ambient 0.53/0.84, les deux directionnelles, spotlight active seulement avec board 3D, position source Y+200, range/attenuation/falloff/theta/phi, focus accelerate-in-out sur 210 ticks, suivi du token anime au ratio 0.3 via `TokenPoseTracker`, idle strict >25 s avec pas de 3 unites/tick et convergence couleur joueur de 0.003/tick vers 75 % des six RGB retail. `World3DRenderer` transporte world position/normale, applique ces lumieres dans HLSL/MSL et utilise des DXIL regeneres; des readbacks D3D12 verrouillent directionnelle, spotlight dans le cone et rejet hors cone. Le contrat `viewportBackgroundFillOn`/`LE_REND3D_ClearBeforeRender` est aussi raccorde : le flag suit `game3DOn` lors de `DISPLAY_UDBOARD_Show` et `GPUFrame` ne reblitte `BackGround.bmp` dans le viewport que lorsque ce fill est actif, preservant le fond 2D en mode plateau 2D. Les backdrops statiques `BMP_sybkgrnd`, `BMP_rnbacknd` et `BMP_auctiona` ne sont pas presents dans les ressources modernes et restent donc explicitement differes; `g_bOptionsButtonsOn` est un contrat UDOpts/menus hors IBar sans consommateur moderne actuel, il n est pas materialise comme etat mort. `UDBOARD_SelectAppropriateView` est maintenant porte pour ses deux callers retail actifs : `VIEW_ROLLDICE` reproduit la formule 15-tuiles et le fallback `square>39 -> 10`, tandis que `VIEW_JAILCHOICE` force `VIEW2D17_CORNER_JAIL`; `UserInterface` applique ces choix sur `NOTIFY_PLEASE_ROLL_DICE` et `NOTIFY_JAIL_EXIT_CHOICE`. `UDBOARD_PreLoadBackdrop` n est pas porte car son seul caller de `UDPieces.cpp` est compile sous `#if 0`. Les allocations de surfaces/lumieres et leur destruction ont desormais des owners modernes (`BoardBackdropPlayback`, renderer/controller lighting), donc aucun handle DirectDraw/Direct3D legacy n est recree pour le lifecycle. Les manques UDBoard auto-contenus actifs sont ainsi fermes; restent les dependances externes deja identifiees (`g_aid2DBoards`, DAT statiques, UDOpts et effets camera specifiques portes avec UDPieces). |
| `Source/monopoly/UDIBar.cpp` | `IBar`, `IBarLayout`, `IBarBackdropPlayback`, `IBarPropertyPlayback`, `IBarCardPlayback`, `IBarJailCardPlayback`, `IBarBankPlayback`, `IBarCameraButtonPlayback`, `IBarCurrentPlayerPlayback`, `IBarRuleState` | `PORTED_PARTIAL` | initialize/destroy/tick/show/process, filtrage joueurs, backdrop, boutons globaux et d'etat, pion courant, titres/deeds, cartes Chance/Community Chest | LocalPlayers, DISPLAY, UserInterface, SequencePlayback, Overlay2D, assets/fonts | Overlay2D couvre backdrop joueur/banque, icone Banque, pion courant, boutons globaux, decisions tour/prison/taxe/achat, Bankrupt, BSSM Build/Sell/Mortgage/Unmort, AucHouse/AucHotel, PlaceHouse/PlaceHotel et TradeAcc/TradeCnt/TradeRej. Les acknowledgements RULE alimentent maintenant le visuel transitoire `Pressed`, consomme une seule fois par le playback. Les 28 titres DAT_MAIN et leur mouseover deed sont raccordes : delai strict `>36`, priorite 1003, `StartXY(540,130)`, variantes normale/hypothequee DAT_LANG2. Le popup Buy/Auction reutilise le deed normal USA, conserve `PropertyBuyAuctionDesired` comme etat separe, priorite 1002 et placements source `StartXY(20,110)`, `(560,110)` ou `(594,110)` selon Main gauche/droite ou Trade/Portfolio; le gate par identite conserve aussi l absence de repositionnement quand la vue change sans changer de deed. Les cartes USA suivent `Off -> DeckOut -> CardIn -> FaceIn -> Idle -> Out`, priorite 1005, DropDropFrames, `StayAtEnd`/`Stop`, 39 offsets camera DAT_MAIN et les quatre familles DAT_LANG2 par deck; Main reste a y=0 et Portfolio/Trade a y=136. `NotifyPickedUpCard` mappe Chance/Community en index 0..31 et `CardSeen`/`NotifyPutAwayCard` declenchent la sortie. Les score boxes bitmap sont raccordees via `IBarScoreStripPlayback` : tokens `TAB_inpsa`, barres couleur large/petite `TAB_inpsl0`/`TAB_inpss0`, barreaux `TAB_inpbj`, priorites historiques, hover +1 et positions `UDIBAR_Calc_ScoreX`; le bank hover pilote maintenant `TAB_bank` entre y=560/561. Le snapshot cash/nom conserve aussi `LastPlayerScoresPrinted`, le gate de 20 ticks et la direction Up/Down sans inventer de rendu texte. Les deux cartes prison joueur `TAB_indsgoojc01/02` sont maintenant des feuilles DAT_LANG2 transactionnelles aux priorites 256/257 et positions (740,506)/(750,525), visibles sur Main/Trade selon `cards[deck].jailOwner`; BankPlayer reste explicitement differe car le legacy y compose maison/hotel + compteur sur surfaces runtime/fonts. La selection score bar est maintenant fidele a `IBarStateTrackOn` : clic joueur RULE restaure le tracking, humain local/Banque entre en `OtherPlayer`, distant/IA en `OtherPlayerRemote`, avec joueur inspecte persistant a travers les changements RULE et retour Done exact depuis les sous-etats BSSM/DeedActive. Le bouton Camera est maintenant aussi un controle reel, pas seulement un playback : hit global independant des masques RULE, joueur IBar inspecte, cycle source Top -> 3 tiles -> 15 tiles -> Top, trois variantes Top, fallback prison, avance droite/Ctrl modulo 39, feedback Pressed et handoff one-shot d un preset quand le lock souris manuel est actif. Les atlas boutons CNK_iyaaf/CNK_iycaf, priorites 999/1000/1001/1002, IBarIsStable, IBAR_JustChanged, layout/hit masks et sous-etats locaux restent testes. Restent surtout le rendu texte/noms/cash, les surfaces dynamiques/fonts et les SFX cash. |
| `Source/monopoly/UDOpts.cpp` | Aucun | `NOT_STARTED` | options UI | persistence options, DISPLAY | Porter apres le resolver de ressources et le renderer UI. |
| `Source/monopoly/UDPieces.cpp` | `PiecePlacement`, `PieceRuntime`, `PieceCamera`, `PieceMovePlan`, `PieceMovePlayback`, `PieceMoveIngress`, `PieceInterpolation`, `PieceJailPlan`, `PieceJailPlayback`, `PieceIdleTransition`, `PieceIdlePlayback`, `PieceIdleDisplay`, `PieceBuildingDisplay`, `DiceIngress`, `DiceDisplay`, `BoardGeometry` | `PORTED_PARTIAL` | orientation tokens/repos, maisons/hotels, pose runtime, cameras, TokenAnimStack, game-queue movement ingress, GoToJail 1..14, transitions idle centre<->repos, idles persistants, batiments et cycle des | `SequenceRuntime`, `SequencePlayback`, GameQueueGate, BoardCameraController, PC3D moderne, BoardGeometry, RULE | Placement/orientation, pose runtime, cameras 3/5/15, `PlanMoveAnim`, faillite/victoire, interpolation Bezier, paddywagon GoToJail 1..14, transitions centre/repos et `Player3DTokenShown` sont portes/testes. Maisons/hotels utilisent HMD 4/5 et priorites historiques. Le lancer de des 3D/idle 3D utilise les tables 6x6, priorite 100, seuils stricts +36/+91/+121, queue lock et override camera. Le cycle 2D DAT_MAIN 0x0096..0x009C est raccorde au moteur : `CurrentDiceID`, `CurrentBobDice`, Stop/Start, notification consommee apres le de gauche, bobbing -35/-11, DropDropFrames a droite et LoopToBeginning. Un readback D3D12 prouve les pixels reels aux deux positions. Restent surtout shadows, effets audio/Pennybags et quelques aspects lifecycle/options. |
| `Source/monopoly/UDPsel.cpp` | `PlayerSelection`, `PlayerSetupFlow`, `LocalPlayers`, `IBar` | `PORTED_PARTIAL` | phases, noms, tokens, humains/IA, add/remove/start, SelectCity | UserInterface, Messaging, DISPLAY | Commit de phase limite a `DISPLAY_UDPSEL_Show`, refresh one-shot, reset zero-joueur et initialisation de la premiere notification non nulle portes. `SelectCity` USA porte les quatre hotspots Classic/Left/Right/Next, le wrap 0..10, Classic -> ville 0, Next -> ville selectionnee et publie la ville autoritative jusque `Display::State.city` avant `StandardOrCustomRules`. L ecran de regles, les projections restantes et le rendu retail restent a traiter. |
| `Source/monopoly/UDSound.cpp` | Aucun | `NOT_STARTED` | options son, volume, musique/SFX | service audio portable, persistence | Choisir l'equivalent portable apres inventaire des assets et callers. |
| `Source/monopoly/UDStats.cpp` | Aucun | `NOT_STARTED` | statistiques joueur/partie | miroir UI, fonts, assets | Porter state/layout/show/process apres consolidation du miroir UI. |
| `Source/monopoly/UDTrade.cpp` | `RuleTrade`, `LocalPlayers::tradeSourcePlayer` | `PORTED_PARTIAL` | `UDTrade_GetPlayerToTradeFrom`, editeur, acceptance, presentation trade | miroir UI, RuleTrade, LocalPlayers, DISPLAY | Le helper consomme par UDIBar est porte exactement : depart joueur IBar/banque, parcours arriere, humain local et exclusion `SQ_OFF_BOARD`, avec `RULE_MAX_PLAYERS` si aucun candidat. L'editeur Trade, ses surfaces, listes, acceptance et presentation restent a porter. |
| `Source/monopoly/UDCGE.cpp` | Aucun | `NOT_STARTED` | editeur/outil runtime appele par le jeu | DISPLAY, data | Determiner le callgraph runtime exact avant de classer ou porter. |
| `Source/monopoly/UDChat.cpp` | Aucun; `Messaging` accepte les futurs messages | `NOT_STARTED` | `CHAT_InitializeSystem`, saisie, `ACTION_TEXT_CHAT` | fonts, DAT_MAIN, LANG, transport | Remplacer le controle d'edition Win32; ressources DAT actuellement manquantes. |
| `Source/monopoly/UDPlrCfg.cpp` | Aucun | `NOT_STARTED` | configuration joueur | PlayerSelection, options, assets | Comparer les ecrans et transitions avant implementation. |
| `Source/monopoly/UDPlrSum.cpp` | Aucun | `NOT_STARTED` | resume joueur | miroir UI, DISPLAY | Porter apres les notifications de jeu dans le miroir UI. |
| `Source/monopoly/UDRules.cpp` | `RuleOptions` couvre la regle, pas l'ecran | `NOT_STARTED` | choix/reglage des regles | RuleOptions, PlayerSetupFlow, DISPLAY | Implementer une projection controlee de `RuleOptions`. |
| `Source/monopoly/UDTitle.cpp` | Aucun | `NOT_STARTED` | title/opening flow | video, assets, DISPLAY | Identifier les ecrans et l'ordre; Bink/AVI seront remplaces par un service portable. |
| `Source/monopoly/UDPenny.cpp` | Aucun | `NOT_STARTED` | Penny Bags/assistant | animations, audio, assets | Porter seulement les comportements effectivement appeles. |
| `Source/monopoly/UDUtils.cpp` | `UDUtils`, `ResourcePaths`, `ResourceContext`, `TextureCatalog` | `PORTED_PARTIAL` | chemins, INI, choix HMD, substitutions de textures | filesystem, DATA, assets | Resolver portable raccorde au startup DATA selon edition/langue, racines explicites et casse ASCII des chemins legacy. Adaptation des recettes ville/langue/plateau/devise aux chemins BMP et au futur mesh encore requise. INI/CD et recherche par basename remplaces. |
| `Source/monopoly/Unility.cpp` | Aucun mapping complet confirme | `NOT_STARTED` | utilitaires de jeu | callers a inventorier | Etablir definitions et callers avant tout port. |
| `Source/monopoly/Debugart.cpp` | Aucun | `NOT_STARTED` | outils/debug runtime | callgraph | Ne classer `LEGACY_UNUSED` qu'apres preuve par callgraph. |

## Services ArtLib consommes

| Original | Equivalent moderne | Statut | Contrat consomme | Dependances | Travail restant / blocage |
|---|---|---|---|---|---|
| `Source/artlib/L_Main.*` | `Application`, `Engine`, `Game` | `REPLACED_PORTABLE` | boucle evenementielle, init/shutdown | SDL3 | Fenetre redimensionnable/HiDPI, presentation, erreurs de startup, creation/device/swapchain et lifecycle GPU sont raccordes et testes sur D3D12. Vulkan/Metal restent architecturaux mais non compiles ici; un smoke test interactif retail reste requis. |
| `Source/artlib/L_Timers.*` | `Timers` + `UIMessages` | `REPLACED_PORTABLE` | horloge 60 Hz, 4 timers, speed/restart, evenement index+tick | steady_clock, file UI | Contrat actuellement raccorde teste de facon deterministe; les appels historiques `LE_TIMER_Delay` identifies dans `Main`, `UDUtils` et les diagnostics DISPLAY appartiennent a des chemins remplaces, inactifs ou encore differes. |
| `Source/artlib/L_UIMsg.*` | `UIMessages` | `PORTED_PARTIAL` | FIFO 100, evenements timer/input, delestage | Application, Timers | Ajouter davantage de tests de pression/coalescence et les types requis par les futurs modules. |
| `Source/artlib/L_Data.*` | `DataBanks`, `LegacyDataArchive`, `DataBankRegistry`, `LegacyDataArchiveBuilder` | `PORTED_PARTIAL` | DataId 16:16, groupes, header/index DAT, zlib, chargement paresseux, refs | zlib, filesystem | Lecture LE explicite, validation, metadata, cache, leases partagees, mount/unmount, index packed et writer de fixtures sont testes. Restent les sources runtime/user-created/external-file, le budget/LRU automatique et une validation sur banque retail. |
| `Source/artlib/L_Chunk.*` | `LegacyChunkReader`, `openLegacyChunkReader` | `PORTED_COMPLETE` | lecteur consomme : header 24-bit + ID 8-bit, descend/ascend/seek/map/read, limite 8 niveaux | `LegacyDataArchive`, futur `L_Seqncr` | Contrat read-only et ownership `ReadFromDataID` portes sans bitfield ABI et testes sur fixtures, y compris le franchissement historique des siblings ID 0/128 lors d'une recherche precise. La validation d'un CNK retail reste bloquee separement; l'editeur/writer non consomme n'est pas dans ce perimetre. |
| `Source/artlib/L_Grafix.*`, `L_Rend2D.*`, `L_Sprite.*` | `Display`, `SequenceBitmapRenderData`, `SequenceWorld2DSlot`, `World2DRenderer` | `PORTED_PARTIAL` | composition 2D, clipping, priorites, surfaces | SDL_GPU, assets, transformation 800x600 | Les feuilles bitmap de sequence ont un chemin actif jusqu'au quad SDL_GPU : ordre depth-first/priorite, cache RGBA8, alpha source, viewport/scissor letterbox et readback D3D12 sont testes. Les sprites UI generiques, fonts, surfaces/blits hors sequence et clipping fin restent a construire. |
| `Source/artlib/L_Rend3D.*` | `GPUFrame`, `SequenceWorld3DSlot`, `World3DGPUScene`, `World3DProjection`, `World3DRenderer` | `PORTED_PARTIAL` | slot World3D 1, viewport, camera/projection, bounds/culling, draw indexed et meshes animes | SDL_GPU, `SequenceRenderData`, PC3D moderne | Le chemin sequence -> slot 1 -> scene GPU -> renderer -> GPUFrame est actif. Bounds, projection ecran, culling, textures HMD et vertex buffers MIMe par node sont testes sur D3D12 reel. Camera 3D/FOV/SetCamera sont raccordes. La visibility 3D historique est maintenant auditee : `SequenceMoved()` retourne toujours TRUE pour un mesh (commentaire source inclus), donc le culling moderne reste strictement renderer-only et ne pilote pas `scrollingWorld`. Restent certains contrats de scene et primitives HMD non consommees. |
| `Source/artlib/L_Seqncr.*` | `LegacySequence`, `SequenceClock`, `SequenceChildSchedule`, `SequenceProgram`, `SequenceRuntime`, `SequenceCommandQueue`, `SequenceTransforms`, `SequenceRenderData`, `SequenceBitmapRenderData` | `PORTED_PARTIAL` | records, arbre runtime, lifecycle, commandes actives, transformations/tweekers, mesh choice, feuilles 2D/3D | `LegacyChunkReader`, DATA, `MeshRuntime`, `SequenceWorld3DSlot`, `SequenceWorld2DSlot` | Grouping/indirect/tweeker, feuilles mesh 3D, feuilles bitmap 2D et records camera 3D sont executes; Start/Stop/SetEndingAction, MoveTheWorks/MoveXY/MoveRySTxz et SetCamera sont raccordes. `GetInfo` expose le sous-ensemble effectivement lu par Monopoly (clock/endTime/matrice monde 3D) avec la recherche `FindNextSequence`, et `GetChildMeshWorldMatrix` parcourt uniquement le sous-arbre du premier root selectionne. ForceRedraw est porte avec son cycle transitoire de redraw et la reevaluation cible/ancetres. Restent surtout labels generiques, model/sound, callbacks et chains selon callers reels. |
| `Source/artlib/L_Fonts.*`, `L_Print.*` | Aucun | `NOT_STARTED` | Arial 10, mesure/rendu de texte | font rasterizer portable, LANG | Choisir un backend portable et conserver metriques/layout observables. |
| `Source/artlib/L_Keybrd.*`, `L_Mouse.*` | traduction SDL dans `Application`, `MousePointer` partiel | `REPLACED_PORTABLE` | input clavier/souris | SDL3, `LogicalViewport` | Souris reconvertie vers 800x600 et bandes noires rejetees; rendu du pointeur et certains types d'evenements restent partiels. |
| `Source/artlib/L_Sound.*`, `L_Midi.*` | Aucun | `NOT_STARTED` | WAV, musique, mixage, voice | audio portable, data | Inventorier les appels Monopoly et remplacer DirectSound/MIDI legacy. |
| `Source/artlib/L_Video.*` | Aucun | `NOT_STARTED` | opening movies | decoder/service video portable | Identifier fichiers et timing avant choix technique; ne pas porter VFW/Bink litteralement. |

## PC3D consomme

| Original | Equivalent moderne | Statut | Contrat consomme | Dependances | Travail restant / blocage |
|---|---|---|---|---|---|
| cameras, viewports, background (`camera.*`, `D3DDevice.*`, view code) | `GPUFrame`, `World3DProjection`, `World3DRenderer`, `Display::Viewport3D`, `LogicalViewport` | `PORTED_PARTIAL` | rectangles Main/Status/Trade, view/projection, depth, bounds ecran et clear | SDL_GPU | Camera/view/projection portable et viewport letterbox sont raccordes au renderer et testes sur D3D12. Le record camera 7, FOV 144, SetCamera direct et selection par label camera sont portes; l angle est confirme comme FOV complet par `PC3D/Matrix.inl`. Le source 3D ne remonte pas son culling au sequenceur (`SequenceMoved` retourne TRUE); restent surtout les transitions UDBoard/UDPieces au fil du port UI. |
| meshes/scenes/materials (`mesh*`, `NewMesh*`, `Scene.h`, `l_material.h`) | `MeshXRuntime`, `MeshRuntimeCache`, `MeshRenderData`, `MeshGPUCache`, `World3DGPUScene` | `PORTED_PARTIAL` | postload HMD, groupes materiau/texture, poses MIMe, bounds, donnees indexees et buffers GPU | `LegacyMeshData`, SDL_GPU | Pour Monopoly, `USE_OLD_FRAME` rend `hmdload.cpp`/`meshx` autoritatif; `NewMesh.cpp` est le chemin alternatif inactif. Triangles actuellement decodes, textures HMD embarquees, pose 0 + diff MIMe, interpolation/extrapolation mesh choice et buffers vertex GPU par node sont portes. Restent autres primitives/categories et substitutions BMP externes `TextureCatalog`. |
| decodeur HMD / postload MESHX (`HMDData.h`, `NewMesh.cpp`, `hmdload.*`) | `LegacyMeshData`, `openLegacyMeshData`, `MeshXRuntime` | `PORTED_PARTIAL` | HMD disque LE, offsets DWORD, triangles, `GsUIMG1`, `GsVtxMIMe`/`GsNrmMIMe`, puis construction mesh CPU | `DAT_3D`, textures, GPU | Structure bornee, cycles/budgets, triangles categorie 0, image 8-bit CLUT -> RGBA8 et diff blocks vertex/normal MIMe sont testes sur fixtures synthetiques. Reset/joint MIMe et autres categories/primitives restent explicites/non portees. MESHX demeure un resultat memoire, jamais un second format disque. |
| vieux DirectDraw/Direct3D drivers (`DDraw*`, `D3DDevice*`) | SDL3 / SDL_GPU | `REPLACED_PORTABLE` | creation device, swapchain, soumission | SDL3 | Les implementations legacy ne seront pas portees; completer seulement les comportements de rendu consommes. |

## Donnees et verification

| Sous-composant DATA | Equivalent moderne / preuve | Statut | Limite exacte |
|---|---|---|---|
| `DataId` / `DataTag` / groupes | `DataBanks`, y compris `IdWithFileFromParent` | `PORTED_COMPLETE` | Contrat 16 bits groupe + 16 bits tag; groupe zero reserve, tag zero valide. |
| Header et index physique DAT | `LegacyDataArchive` | `PORTED_COMPLETE` | Header Win32 28 octets et records 16 octets lus champ par champ en LE; signature/version/types/ranges/troncatures testes. |
| Codec DAT | zlib via `uncompress2` avec consommation exacte | `PORTED_COMPLETE` | `ZImplode.c` prouve un stream zlib enveloppe (`windowBits=15`), pas un codec PKWARE opaque. |
| Lifecycle, lookup, metadata et ownership | `LegacyDataArchive`, `DataBankRegistry`, `ResourceRuntime` | `PORTED_PARTIAL` | Montage des huit banques raccorde au jeu; publication transactionnelle et conservation des snapshots lecteurs testees sur succes/echec/reprise. Sources runtime, external-file et LRU automatique restent a porter. |
| Resolution des chemins DATA | `ResourcePaths`, `UDUtils`, `ResourceContext` | `REPLACED_PORTABLE` | Racines absolues ordonnees, separateurs legacy, casse ASCII, erreurs de collision/acces et choix USA/Europe + langues 1..10. Raccordement reel SDL/variable de processus teste; adaptation contextuelle des textures encore distincte. |
| CRC global DAT | option `ChecksumPolicy::Verify` | `PORTED_PARTIAL` | Le runtime original ne le verifiait pas et aucune banque retail ne confirme la convention du writer; politique runtime par defaut `Ignore`, audit explicite disponible. |
| Writer DAT portable | `LegacyDataArchiveBuilder` | `PORTED_COMPLETE` | Reconstruit deterministement un conteneur valide depuis des payloads fournis; ne pretend pas reproduire les payloads ou octets retail. |
| Index logique packed 6 octets | `DataIndexTable`, `lookupIndexedDataId` | `PORTED_COMPLETE` | Tri strict, cles dupliquees rejetees, plusieurs cles vers le meme tag permises, groupe parent reapplique. |
| Lecteur CNK | `LegacyChunkReader`, `openLegacyChunkReader` | `PORTED_COMPLETE` | Lecture hierarchique source-compatible sur fixtures, y compris la semantique des sentinelles nulles selectionnees ou sautees; aucune validation retail faute de CNK. |
| Parseurs semantiques CNK / sequence | `LegacySequence` | `PORTED_PARTIAL` | En-tete 12 octets, temps signes 24 bits, records grouping/indirect/bitmap/model/sound/camera/mesh/tweeker et attributs prives dimensionality/offset/matrix/OSRT sont bornes. Le record camera packe fait 21 octets (near/far/label); les chunks 139 `3D_MESH_CHOICE` et 144 `CAMERA_FIELD_OF_VIEW` sont decodes explicitement. Video/preloader et attributs encore inconnus sont refuses tant que leurs effets ne sont pas portes. |
| `SequenceClock` | `SequenceClock` | `PORTED_COMPLETE` | Cadence, premiere evaluation, drop/catch-up enfant, pause/reprise, stop, seek, hold cadence 255 et boucle naturelle a zero sont testes pour les types actuellement executables. |
| `SequenceChildSchedule` | `SequenceChildSchedule` | `PORTED_COMPLETE` | Selection ouverte a gauche/fermee a droite, ordre disque, refs DATA relatives et rewind; description partagee immutable et curseur runtime independant. |
| Arbre runtime et execution | `SequenceProgram`, `SequenceRuntime` | `PORTED_PARTIAL` | DAG descriptif borne puis foret mutable a ownership unique; lifecycle, loops, stop/hold/seek, cycles/profondeur/budgets et snapshots testes. Grouping/indirect/tweeker, feuilles mesh 3D, feuilles bitmap 2D et cameras 3D sont executes. Les bitmaps publient `contentsDataId`, priorite, clock et matrice monde 2D; les cameras exposent matrice monde/FOV/near/far et les meshes `contentsDataId`, world transform et `meshChoice`. Les queries immuables `GetInfo` et `GetChildMeshWorldMatrix` reproduisent le premier match et les bornes de sous-arbre du source. |
| Commandes `L_Seqncr` | `SequenceCommandQueue` | `PORTED_PARTIAL` | FIFO proprietaire 500 commandes, nesting Collect/Execute, Start, Stop, SetEndingAction, MoveTheWorks, MoveXY, MoveRySTxz, SetCamera et ForceRedraw, priorite 16 bits, doublons et recherche top-level/whole-tree portes. ForceRedraw marque tous les matches, force la reevaluation cible/ancetres et expose `needsRedraw` pour le frame courant avant remise a zero au cycle suivant. MoveXY atteint maintenant visuellement les feuilles bitmap via Overlay2D; les callers Monopoly actifs ne donnent aucun chain ID et chains restent differees. |
| Transformations / tweekers | `SequenceTransforms`, etat `SequenceRuntime` | `PORTED_PARTIAL` | Matrices row-vector 2D/3D, offset/matrix/OSRT, composition parentale, commandes Move*, identity/constant/linear et ordre tweeker-avant-position portes. `3D_MESH_CHOICE` interpole la proportion sans clamp; `CAMERA_FIELD_OF_VIEW` constant/linear modifie le FOV brut du parent camera avec defaut 3D pi/4. Restent son et callbacks; `scrollingWorld` demeure un contrat d horloge distinct et non simule. |
| MESHX runtime | `MeshXRuntime`, `MeshRuntimeCache` | `PORTED_PARTIAL` | Le chemin Monopoly `USE_OLD_FRAME` est reproduit pour le sous-ensemble HMD porte : inversion Y, normales de base /4096, deduplication, groupes materiau/texture, pose 0 de base puis poses MIMe `base + delta`, interpolation position/normale non clampee, UV de A et bounds interpoles. Le comportement historique ajoute les diff normals SVECTOR bruts apres conversion de la normale de base; ce contrat est teste. Autres primitives et donnees retail restent bloquees. |
| Render data de sequence | `MeshRenderData`, `SequenceRenderData`, `SequenceBitmapRenderData` | `PORTED_PARTIAL` | Les feuilles mesh publient DataID/node/priority/clock/world matrix, `meshChoice`, asset partage et pose evaluee. Les feuilles bitmap publient leur DataID de contenu, payload immutable et matrice monde 2D jusqu'au slot Overlay2D. Une ressource invalide produit une erreur explicite; les objets SDL_GPU restent crees uniquement dans la couche GPU. |
| Raccordement sequence -> render slots | `SequenceWorld3DSlot`, `World3DGPUScene`, `SequenceWorld2DSlot`, `World2DRenderer` | `PORTED_PARTIAL` | Le slot historique 1 gere startup/moved/shutdown, bounds, camera et culling 3D; la scene GPU utilise buffers index/textures statiques et un vertex buffer MIMe par node. Overlay2D synchronise transactionnellement les feuilles bitmap, conserve l'ordre depth-first/priorite, resout les assets RGBA8 et les dessine en coordonnees logiques 800x600 avec alpha source. Le readback D3D12 2x2 prouve les deplacements -35/-11 des des. Le culling 3D n est volontairement pas renvoye au sequenceur, conforme a `SequenceMoved()` qui retourne toujours TRUE. |
| LANG core | `LanguageCatalog`, `LanguageService`, `ResourceRuntime` | `PORTED_PARTIAL` | Contrats existants conserves et raccordes au startup via les memes archives que DATA; index valide avant publication. Adaptateurs de rendu et migration des callers UI non raccordes. |
| Chaines/audio/dialogues LANG retail | aucun payload | `BLOCKED_MISSING_DATA` | Les neuf fichiers texte bruts sont vides et les `dat_ln/lm/lkNN.dat` sont absents; aucune chaine ne sera inventee. |
| Catalogue `TexInfo` | `TextureCatalog` | `PORTED_COMPLETE` | Huit atlas, recettes USA/Europe, overlays, provenances et tags HMD portes et testes; meshes/resolutions invalides rejetes par erreurs typees. |
| Corpus BMP `TexInfo` | `LegacyBitmap`, manifeste de 1 001 assets | `PORTED_COMPLETE` | 1 001/1 001 fichiers trouves : 497 en 128x128, 504 en 256x256, 881 en 8-bit, 120 en 24-bit, tous `BI_RGB`; offsets, tailles declarees, stride DWORD, raster complet et overflows sont verifies. |
| Loader BMP runtime | `LegacyBitmap`, `BitmapRuntimeCache`, `World2DRenderer`, `LegacyAssets` | `PORTED_PARTIAL` | Les BMP de feuilles sequence sont decodes en RGBA8, caches par DataId + identite de payload puis uploades en R8G8B8A8_UNORM; le cache GPU partage et l'alpha source sont testes par readback D3D12. Le fond historique conserve encore sa voie `LegacyAssets`; les substitutions `TextureCatalog` vers les slots mesh restent a raccorder. |
| Manifestes DMake | `LegacyManifest`, `MonopolyManifestTool` | `PORTED_COMPLETE` | Les dix headers reels donnent 46 423 tags/noms/types contigus et peuvent etre exportes en TSV. |
| DMAKE99 et reconstruction bit-a-bit | aucun outil historique | `MISSING_TOOLING` | Sources/executable DMAKE99, fichiers `.df`, chemins source, payloads, tailles et offsets manquent. |
| Banques DAT retail exactes | absentes | `BLOCKED_MISSING_DATA` | `dat_main`, `dat_pat`, `dat_bord[e]`, `dat_brd2`, `dat_3d`, `dat_ln/lm/lkNN` introuvables dans l'arbre, `Source.zip` et l'ISO inspectes. |
| `2DVIEW01..39` externes | noms portes, fichiers absents | `BLOCKED_MISSING_DATA` | Ne pas confondre ces vues custom avec les vues 2D standard stockees en DAT/TAB. |
| HMD retail / objets MESHX | headers/tags seulement, fixtures HMD synthetiques | `BLOCKED_MISSING_DATA` | Le decodeur HMD partiel est teste depuis le contrat source, mais aucun mesh retail ne permet sa validation; aucun faux payload MESHX disque n'est cree. |

### Dettes de fidelite connues, non bloquantes pour le sequenceur

- Le decodeur LANG moderne valide les paires de substituts et le contenu apres
  NUL plus strictement que les manipulations historiques de code units UTF-16
  Windows. Cette difference reste a arbitrer avant de declarer LANG complet.
- `lookupIndexedDataId` represente l'absence par une erreur typee; le helper
  generique ne reproduit donc pas encore exactement le retour sentinelle
  historique `LED_EI == 0` de tous les callers. Ne pas masquer cette difference
  en `PORTED_COMPLETE` lorsqu'un caller dependra de la sentinelle.

`MonopolyManifestTool <output.tsv> <header-DMake>...` exporte uniquement les
informations effectivement reconstructibles : banque, tag decimal, type et
symbole. Il ne genere jamais silencieusement un DAT ou un payload fictif.

Une absence de DAT ne justifie jamais l'invention d'un chunk, d'une chaine,
d'un offset ou d'une texture. Les fixtures DAT/CNK/LANG presentes sous
`modern/tests` valident le format source et sont explicitement synthetiques.

### Configuration et duree de vie des ressources

Le jeu cherche desormais les banques sous `Dat_Mon/` dans le repertoire de
l'executable. `MONOPOLY_DATA_ROOT` peut designer explicitement une autre racine
absolue d'installation (celle qui contient `Dat_Mon/`). Une valeur relative ou
vide est une erreur de configuration; elle ne reutilise pas une ancienne
configuration et ne retombe pas sur une autre installation. Ce choix portable
remplace la recherche legacy INI/CD/cwd sans chemin propre a une machine.
L'API accepte un contexte edition/langue; le jeu conserve par defaut le contexte
historique USA/English US. Les choix Europe/French sont testes sur fixtures et
ne constituent pas encore un menu de selection utilisateur.

`ResourceRuntime` prepare cinq banques core puis les trois banques LANG dans
un nouveau registre. `LanguageService` utilise les memes instances. Le nouvel
ensemble n'est publie qu'apres validation des huit archives et de l'index LANG.
Une erreur preserve l'ancien snapshot du service. `Game` libere son snapshot
actif apres DISPLAY, souris et slots; un consommateur qui conserve un snapshot
peut encore charger des donnees non cachees. Cela evite le probleme signale par
le commentaire de `Source/monopoly/Main.cpp:450` sur un nettoyage LANG premature.
L'arret retire les expirations des timers de jeu deja en attente, en conservant
les autres evenements et leur ordre FIFO. Les tests de reprise utilisent les
vrais lecteurs DATA/LANG, timers et file UI; les consommateurs graphiques sont
des doubles de test et ne constituent pas une preuve de rendu GPU.

Le startup refuse maintenant aussi une erreur de banque core, dont le retour
etait ignore dans `Userifce.cpp`. C'est un renforcement explicite du diagnostic.
Les banques retail restant absentes, l'application ne peut pas demarrer une
partie complete; aucune fixture n'est installee ou chargee a leur place.
Le fond BMP de `LegacyAssets` conserve pour l'instant son chemin separe
`assets/legacy/BackGround.bmp`; son integration contextuelle reste a faire.

Validation requise pour chaque lot : configure CMake reussi, build reussi,
puis CTest. Un ancien executable de test ne constitue pas une validation.

### HMD et sequences : limites des contrats raccordes

`LegacyMeshData` conserve les octets HMD immuables. Il borne la table des
blocs, les headers, les chaines de primitives et les sections, detecte les
cycles et impose des budgets avant allocation. Les offsets sont des DWORDs de
quatre octets, jamais des pointeurs natifs. Pour Monopoly, les defines
`USE_OLD_FRAME` dans les callers jeu/ArtLib rendent `hmdload.cpp`/`meshx`
autoritatif; `NewMesh.cpp` est un chemin alternatif non compile par ces callers.
Le port suit donc le parcours tail-first de `HMD_MapUnit`, l'ordre append des
diff blocks et les contrats de `HMD_interpolate`, plutot que les typos visibles
dans `NewMesh.cpp`.

`triangle()` valide le sous-ensemble categorie 0 actuellement consomme. Les
images `GsUIMG1` 8-bit + CLUT sont converties en RGBA8 sans GDI. `mimePoses()`
decode `GsVtxMIMe` et `GsNrmMIMe` avec offsets relatifs bornes, budgets de
blocs/SVECTOR et appariement ordinal. La pose 0 reste le mesh de base; les poses
1..N appliquent les deltas. Reset/joint MIMe et autres categories restent
explicites/non portees. Une simple reussite de `parse()` ne pretend toujours pas
valider tous les payloads HMD retail.

`LegacySequence` decode maintenant les huit parties fixes effectivement utiles
a cette etape, dont le record tweeker de 13 octets. Il decode aussi les attributs
prives dimensionality, offset, matrix et OSRT 2D/3D sans copier dans une
structure native. Les octets/records et attributs restent immutables; ils ne
sont jamais utilises comme etat runtime mutable.

`SequenceClock` possede l'etat temporel. Le contrat est extrait de
`L_Seqncr.cpp:3756-3835,6443-6703,7326-7337` : premiere mise a jour immediate,
rattrapage initial des enfants seulement si dropFrames, cadence, pause/reprise,
stop/suicide, maintien a la fin avec cadence 255 activee dans `C_ArtLib.h`,
boucle naturelle a zero (pas modulo du depassement), et seek explicite. EndTime
zero conserve la sentinelle historique 1234567890. Les commandes de remplacement
non nulles de cadence/action sont appliquees avant validation. Une cadence finale
nulle, une action inconnue, une duree negative ou un depassement arithmetique
produit une erreur explicite. Le decoder conserve les champs disque bruts.
Les horloges son/video et la visibilite des mondes defilants ne sont pas simulees.

`SequenceChildSchedule` porte la selection de `AddNewlyBornChildren`
(`L_Seqncr.cpp:5230-5330`) : intervalle ouvert a gauche, ferme a droite,
premier enfant futur bloquant le scan, enfants Stop deja termines ignores.
L'ordre disque n'est jamais trie dans le calendrier. Le loader DATA respecte les
bornes du parent direct et le remplacement par une liste CNK indirecte; le tag
relatif zero est remappe, tandis que l'ID absolu zero signifie aucun enfant. La
description partagee conserve sa lease et chaque runtime possede son propre
curseur. Un type non decode est refuse, pas saute silencieusement.
Une reference indirecte vers le meme item est refusee : le source conserve
dans ce cas les bornes du parent puis echoue en tentant de revenir a zero
(`L_Seqncr.cpp:5269-5279`, `L_Chunk.cpp:2497-2511`). Le port preserve ce refus;
il n'invente pas une lecture recursive pour cette donnee invalide.

`SequenceProgram` construit ensuite un DAG descriptif immutable, avec budgets
de references/descriptions et profondeur configurable plafonnee a 128. Les
cycles indirects sont refuses par couple `(DataId, offset)`; les sous-descriptions
partagees ne sont pas amplifiees exponentiellement. Une construction depuis
`ResourceSnapshot` garde toutes les archives en vie apres remplacement du
snapshot publie. Ce chargement eager et borne remplace volontairement le parcours
paresseux par pointeurs du code Win32 afin que toute dependance invalide echoue
avant publication.

`SequenceRuntime` instancie une foret mutable a ownership unique sous la racine
historique implicite. Les enfants naissent a leur tick, sont mis a jour
recursivement et detruits enfant-avant-parent. Les loops detruisent les anciennes
instances, rembobinent le calendrier et creent de nouveaux IDs; stop, fin
naturelle, hold, pause/reprise et seek reconstruisent ou conservent les enfants
selon le contrat de l'horloge. Les siblings sont ordonnes par priorite croissante,
avec insertion avant l'ancien en cas d'egalite comme `InsertRuntimeChild`.
Aucun handle n'est reutilise et une erreur de construction/mise a jour vide
explicitement la foret plutot que publier un etat partiel trompeur.

`SequenceCommandQueue` reproduit la FIFO owner-thread de 500 entrees et le niveau
Collect/Execute, y compris un Execute surnumeraire negatif. Start cree toujours
une nouvelle instance; Stop et SetEndingAction ciblent le couple DataId/priorite
16 bits, top-level ou arbre entier, avec offset de donnees nul. L'audit des
callers Monopoly ne montre aucun chain ID actif; les listes waiting/dechained ne
sont donc pas inventees. Les commandes MoveTheWorks/MoveXY/MoveRySTxz et SetCamera sont portees.
L ownership des labels camera suit `LE_SEQNCR_LabelArray`: le dernier demarre prend
le label et sa suppression ne restaure pas un overlap plus ancien. Les labels
generiques, callbacks et chains restent partiels. `GetInfo` et
`GetChildMeshWorldMatrix` couvrent maintenant les champs/parcours effectivement lus par Monopoly.
`ForceRedraw` est porte dans la FIFO et le runtime avec un redraw transitoire
source-compatible et propagation vers les ancetres pendant l update.

`SequenceTransforms` porte les conventions row-vector 2D/3D de `L_Matrix.cpp`,
les six attributs offset/matrix/OSRT et la composition local puis parent. Les
tweekers identity, constant et linear appliquent leur transformation avant le
calcul de position du parent (`L_Seqncr.cpp:5484-5672,5789-6275`). Le chunk
prive 139 `3D_MESH_CHOICE` est maintenant un etat non matriciel distinct :
constant/linear le mettent a jour, A/B restent ceux de la premiere cle et la
proportion seule interpole sans clamp; Identity n'efface pas cet etat.

Les feuilles 3D publient la pose MIMe evaluee jusqu'au slot World3D 1. La scene
SDL_GPU conserve indices/textures statiques et alloue un vertex buffer anime par
node, mis a jour par cycling puis prune avec le lifecycle. Un readback D3D12
prouve que le changement de pose deplace reellement les pixels rasterizes. Le record camera 7, le FOV 144 et SetCamera atteignent maintenant la camera
de projection utilisee par la frame; un label absent conserve la camera precedente.
Les effets son/callbacks et les objets sequence non encore consommes restent
explicitement partiels. La visibility 3D a ete auditee : le renderer original
renvoie toujours TRUE a `SequenceMoved`, donc aucun feedback de culling n est ajoute.

### Derniere validation locale

Le 2026-09-07, avec MSVC 19.51 / toolset 14.51, Visual Studio 18 2026,
SDL 3.4.14 et zlib 1.3.2 :

- regeneration CMake du build Visual Studio puis reconstruction **clean-first
  complete** de `modern/build`, code de sortie zero, incluant
  `MonopolyModern.exe`, `MonopolyDataCore` et `MonopolyGPU3DCore` ;
- suite complete depuis ce build neuf : **71/71 suites passees**, zero echec ;
- les suites `MeshGPUResources`, `World3DGPUScene`, `World3DRenderer` et
  `World2DRenderer` utilisent un vrai device SDL_GPU **Direct3D 12** sans
  passing skip ;
- le readback 2D alloue une cible R8G8B8A8, attend une fence GPU puis mappe les
  pixels reels. Une fixture BMP synthetique 2x2 conserve exactement bleu/blanc/
  rouge/vert, puis `DiceDisplay::plan2D` deplace les deux instances de `-35` et
  `-11`; le pixel d'origine est efface et les pixels attendus sont presents aux
  nouvelles positions ;
- le meme test verrouille cache texture partage, priorites, ordre des siblings,
  coordonnees logiques 800x600, scale 1600x1200, letterbox 1000x600 et purge du
  cache apres Stop ;
- `Dice2DPlayback` verrouille `CurrentDiceID`, `CurrentBobDice`, ordre
  Stop/Start/Move, consommation historique de `DiceRollNotification`, bobbing
  gauche/droit, DropDropFrames uniquement a droite et LoopToBeginning ;
- le sequenceur accepte maintenant un `DataUAP` brut comme racine bitmap 2D infinie a 60 Hz / StayAtEnd, exactement comme `LI_SEQNCR_StartUpSequence`; le decodeur `NEWBITMAPHEADER` conserve raster top-down, stride DWORD, palette/alpha et origine signee, puis publie du straight RGBA8 dans Overlay2D ;
- les surfaces `DataNative` runtime disposent maintenant d un registre RGBA8 versionne distinct des banques DAT, avec blits Replace/SourceOver, clipping, publication live dans Overlay2D et `transitionXY` atomique Stop/Start/Move ;
- `BoardBackdropPlayback` reproduit le cache 2D UDBoard USA : 4 buffers Main 800x450, 1 buffer Portfolio/Trade 400x225, `BMP_mybs*`/`BMP_mybss*`, priorite 10, offsets viewport et eviction historique; l index DataBMP est `camera + 39*city` pour les villes USA 0..10, le cache Main est `(city,camera)` et les villes invalides sont rejetees transactionnellement ;
- le board 3D USA selectionne maintenant `HMD_boardmed` pour city 0 et `HMD_board_citymed` pour toute valeur non nulle, contrat source 992-995 verrouille dans `TextureCatalogTests` ;
- `BoardLightingController` et `World3DRenderer` portent le bloc LIGHTING UDBoard : ambient 0.53/0.84, deux directionnelles, spotlight Y+200 avec range/attenuation/falloff/theta/phi, focus 210 ticks, suivi anime 0.3, idle strict >25 s, couleur joueur 0.003/tick; HLSL/MSL sont alignes, DXIL regenere avec le SDK 10.0.26100 et le readback D3D12 prouve eclairage directionnel, spotlight dans le cone et absence hors cone ;
- `viewportBackgroundFillOn` suit maintenant `game3DOn` comme `UDBoard.cpp:913-966`; `GPUFrame` ne reblitte le `BackGround.bmp` 3D que lorsque ce clear/fill est actif, tandis que le mode plateau 2D laisse ce remplissage desactive ;
- `UDBOARD_SelectAppropriateView` est verrouille pour les 40 cases, le fallback prison/off-board `>39 -> 10` et la vue prison fixe; les deux notifications UDIBar actives publient maintenant ces cameras, tandis que le preload 2D reste volontairement absent car son caller retail est sous `#if 0` ;
- le routage input respecte maintenant `UDBOARD -> UDIBAR -> UDPSEL`; les mouvements SDL transportent X/Y et DeltaX/DeltaY dans le repere logique 800x600, et le controle manuel souris de la camera 3D est teste de bout en bout avec release strictement apres 20 secondes ;
- `BoardOwnershipHighlight` verrouille l ordre des 28 proprietes, les formules `DAT_BOARD/DAT_BOARD2`, les HMD ownership/mortgage, les quatre orientations, les priorites `12+square`, le remplacement `2D -> 3D -> Stop`, l origine UAP `StartCXYSlot` et l atomicite en cas de ressource/FIFO insuffisante ;
- `IBarRuleState` verrouille les valeurs 0..26 de `IBAR_STATES`, les quatre
  variantes de sortie de prison, les transitions notification/action acceptee et
  le reset; `UserInterface` expose cette projection au moteur ;
- le playback des boutons UDIBar conserve les priorites 999/1000/1001/1002,
  les atlas couleur/gris, `IBarIsStable` et le cycle outgoing-only
  `IBAR_JustChanged`; Bankrupt, Build/Sell/Mortgage/Unmort, AucHouse/AucHotel,
  PlaceHouse/PlaceHotel et TradeAcc/TradeCnt/TradeRej rejoignent les feuilles
  Overlay2D attendues; les disponibilites BSSM sont calculees depuis le
  `GameState` et `RuleBuildings::testBuildingPlacement` ;
- `IBarLayout` verrouille les neuf rectangles d'action originaux et les variantes
  BuyAuction/TaxDecision/Trading, y compris le chevauchement d'un pixel du source
  filtre par le masque des slots visibles et les bornes right/bottom exclusives ;
- `IBarActionInput` prouve le filtrage souris par masque actif et les actions directes
  Buy/Auction, taxe, prison, Trade, Bankrupt et shortage; les sous-etats BSSM
  restent locaux a l'IBar et reviennent a la projection RULE lorsqu'elle change ;
- `IBarPropertyPlayback` reproduit `propconv`, `IBARPropertyBarOrder`, les 28 titres
  DAT_MAIN, leur ordre de recouvrement 41->0, `DeedActive` et les actions rapides ;
- le feedback `Pressed` des boutons est alimente par `NotifyActionCompleted`, conserve
  l'index legacy du bouton et est consomme sans redemarrage parasite ;
- le mouseover deed UDIBar respecte le delai strict `>36`, la priorite 1003,
  `StartXY(540,130)`, les variantes normale/hypothequee et l'absence de blow-up
  pour un titre low-colour ;
- `IBarCardPlayback` couvre les 32 cartes USA et les 39 cameras : deck-out DAT_MAIN,
  card-in/face/idle/out DAT_LANG2, priorite 1005, DropDropFrames, y=0 sur Main,
  y=136 hors Main, `StayAtEnd` puis `Stop`; Chance et Community Chest parcourent
  le lifecycle complet et `CardSeen`/`NotifyPutAwayCard` conduisent a `Out` ;
- `IBarScoreStripPlayback` porte les feuilles statiques des score boxes : atlas token/couleur
  large/petite, barreaux de prison, positions 1..6 joueurs, priorites 257+t/305+t/306+t
  et hover +1; le snapshot texte conserve cash/nom, gate exact de 20 ticks et direction
  CashUp/CashDown sans creer de surface ou glyphes fictifs ;
- le hit-test joueur/banque partage l etat `playerCurrentMouseOver` du source; le bank hover
  atteint reellement Overlay2D par `MoveXY(755,561)` et revient a y=560 ;
- deux branches ResourcePaths dependent de l'hote restent `[SKIP]` sur ce
  Windows (collision de casse et creation de symlink sans droit). Elles ne sont
  pas presentees comme validees.

Les regressions reproduites puis corrigees restent verrouillees par tests :
chemins `fichier/.`, expirations de timers 0/1 restant dans la file UI, reference
sequence indirecte vers son propre item et fixture tweeker dont le flag
drop-frames etait initialement incoherent avec son attente. Les tests DATA/LANG
couvrent aussi banques manquantes/tronquees, index invalide, rollback et duree
de vie des snapshots.

Preuve historique distincte (2026-08-24, MSVC 19.50) : build autonome sans
arbre `Source`, 16/16 suites, dix manifestes (46 423 entrees) et 1 001 BMP.
Ni Linux ni macOS n'ont ete compiles pour le jalon actuel. Vulkan/Metal ne sont
pas valides ici; le SPIR-V perime n'est plus distribue comme s'il correspondait
au shader texture actuel. Les DAT/HMD retail exacts restent absents, donc les
fixtures synthetiques prouvent les contrats source mais pas un contenu retail
indisponible.

## Prochaines priorites

1. Revenir a `UDIBar` pour les derniers consommateurs purement sequence; les contrats UDBoard auto-contenus actifs sont maintenant couverts, et UDBoard ne doit etre rouvert que lorsqu un caller externe debloque `g_aid2DBoards`, les backdrops DAT statiques, UDOpts ou les effets camera specifiques de UDPieces. Conserver cash/noms comme donnees jusqu au backend minimal fonts/surfaces et ne pas inventer de rendu texte ni de SFX avant leurs services portables.
2. Auditer ensuite les autres types HMD effectivement atteignables sous `USE_OLD_FRAME`;
   ne porter qu un type prouve par caller/source et laisser reset/joint MIMe ou
   primitives non consommees explicitement non supportes.
3. Raccorder la voie **externe** `TextureCatalog -> ResourcePaths/BMP -> mesh`
   pour les substitutions ville/langue/plateau/devise; ne pas dupliquer la voie
   texture HMD embarquee `GsUIMG1 -> SDL_GPUTexture` deja fonctionnelle.
4. Reprendre la Phase D en priorisant `UDIBar -> UDPsel -> UDPieces ->
   UDAuct -> UDTrade -> UDOpts`, et reporter AI, audio/voice, video et fonts tant
   qu ils ne debloquent pas un chemin plus prioritaire.
