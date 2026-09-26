# Inventaire du portage {#porting_matrix}

Cette matrice conserve les 79 entrées de référence. Le [plan courant](PORTING_STATUS.md) définit les travaux C, D, A et Q cités ci-dessous. Les familles et leurs sous-contrats se recouvrent : leur nombre ne mesure pas un pourcentage de jeu terminé.

## Lecture des statuts

- `PORTED_COMPLETE` : contrat indiqué implémenté ; la qualification retail ou multiplateforme peut rester ouverte.
- `REPLACED_PORTABLE` : contrat remplacé par un mécanisme moderne explicite.
- `PORTED_PARTIAL` : écart actif identifié, décrit dans la ligne.
- `REVIEW_REQUIRED` : comparaison sémantique inachevée ; ce statut ne prétend ni à une omission prouvée ni à une conformité complète.
- `NOT_STARTED` : travail actif identifié sans implémentation.
- `BLOCKED_MISSING_DATA` : données nécessaires absentes.
- `MISSING_TOOLING` : outil manquant ; son caractère nécessaire ou optionnel est précisé.
- `LEGACY_UNUSED` : hors cible sur preuve d’absence d’usage actif ; jamais sur le seul âge du code.

Les noms de modules désignent les sources et équivalents à examiner, pas une preuve de qualification. Les tests de référence et leurs limites sont dans le plan. Les exclusions doivent être réexaminées si un nouvel appelant ou un asset consommé les contredit.

## Jeu Monopoly

| Origine / contrat | Équivalent moderne | Statut | Portée, preuve et suite |
|---|---|---|---|
| Source/monopoly/Main.cpp | Application, Engine, Game, RenderSlots, Timers, GPUFrame, UserInterface | `REPLACED_PORTABLE` | Boucle SDL, slots, timers, arrêt et rollback portés ; ResourceLifecycleTests. |
| Source/monopoly/GameInc.cpp/.h | DataBanks, ResourceContext, ExtendedInitialization, CMake et includes modernes explicites | `REPLACED_PORTABLE` | Initialisation étendue et groupes DAT portés ; films d’ouverture raccordés. Qualification Q04. |
| Source/monopoly/Mdef.cpp | DataBanks::LanguageId, ResourceContext, resultats d initialisation modernes | `REPLACED_PORTABLE` | Langue courante et résultats d’initialisation portés par ResourceContext. |
| Source/monopoly/Mess.cpp | Messaging | `REVIEW_REQUIRED` | Sessions de jeu TCP, ownership et actions distantes raccordés (C01) ; qualification Q02 ouverte, lot non compilé/testé. |
| Source/monopoly/Rule.cpp | RulesEngine, Rule*, BoardRules, PhaseStack, CardDeck* | `REVIEW_REQUIRED` | Règles et archives exécutables ; comparaison exhaustive des branches encore ouverte (A01). |
| Source/monopoly/trade.cpp | RuleTrade, TradeUI | `REVIEW_REQUIRED` | Trade_SendItems raccordé ; parité des décisions et transitions à terminer de comparer (A02). |
| Source/monopoly/Ai.cpp | AIDecisionUtility, AIUtility, AIMessageIngress, AITradeIngress, BoardRules | `REVIEW_REQUIRED` | IA opérationnelle ; couverture des décisions à établir (A02) ; garde des actions en cours avant décision trade corrigé, non testé. |
| Source/monopoly/Ai_load.cpp | AIProfile, AIProfileRuntime | `PORTED_COMPLETE` | Chargement des paramètres IA porté. |
| Source/monopoly/Ai_trade.cpp | AITradeUtility, AIDecisionUtility, AICounterTradeRuntime, AITradeSendRuntime, AITradeIngress | `REVIEW_REQUIRED` | Évaluation des échanges à comparer aux décisions originales (A02) ; garde des actions en cours avant décision trade corrigé, non testé. |
| Source/monopoly/Ai_util.cpp | AIUtility, BoardRules | `PORTED_COMPLETE` | Les 45 exports ont un équivalent ou un propriétaire moderne : AIUtility, AITradeUtility, AIDecisionUtility et AIMessageIngress. La comparaison globale des décisions IA reste en A02. |
| Source/monopoly/Lang.cpp | LanguageCatalog, LanguageService, ResourceRuntime, DataBanks, LegacyTextIds | `REPLACED_PORTABLE` | Dix langues, triplets DAT, publication transactionnelle, snapshots, lookup/fallback et nettoyage portés. Mesure/impression déléguées à FontRuntime ; décodage et métriques restent suivis en A04. |
| Source/monopoly/TexInfo.cpp | TextureCatalog | `PORTED_COMPLETE` | Catalogue TexInfo et résolution de ressources portés ; qualification Q03. |
| Source/monopoly/Tickler.cpp | Aucun requis | `LEGACY_UNUSED` | Utilitaire Win95 de mémoire, lié au projet mais sans appelant externe actif ; Tickler.cpp et TheGame.dsp. |
| Source/monopoly/L_voice.cpp | VoiceChatLegacyContract, VoiceChatAudioRuntime, Gsm610Codec, VoiceChatPacket, Messaging, Engine | `REPLACED_PORTABLE` | Capture SDL3, PCM 11025 Hz/8-bit/mono, GSM WAV49, DAT1/DATN et playback portés ; Q02. |
| Source/monopoly/display.cpp | Display, GPUFrame, RenderSlots, LogicalViewport, World2DRenderer | `REVIEW_REQUIRED` | Slots, fonds, animations et notifications présents ; transitions à comparer (A03). |
| Source/monopoly/Userifce.cpp | UserInterface, LocalPlayers, RuntimeState, TimeStep, ExtendedInitialization, ResourceRuntime, IBarRuleState | `REVIEW_REQUIRED` | Dispatcher UI et cycle de jeu présents ; scénarios et transitions à comparer (A01, A03). |
| Source/monopoly/UDAuct.cpp | AuctionUI, AuctionPlayback, AuctionTextPlayback, AuctionPennyBagsPlayback, UserInterface, RuleAuction | `PORTED_COMPLETE` | Enchères et surfaces texte portées ; validation avec ressources du jeu en Q01. |
| Source/monopoly/UDBoard.cpp | Display, BoardCameraController, BoardGeometry, BoardBackdropPlayback, BoardOwnershipHighlight, BoardLightingController, World3DRenderer | `REVIEW_REQUIRED` | Plateau et textures présents ; comparaison du rendu et du Board Editor (A03, A07, Q03). |
| Source/monopoly/UDIBar.cpp | IBar, IBarLayout, IBarBackdropPlayback, IBarPropertyPlayback, IBarCardPlayback, IBarJailCardPlayback, IBarBankPlayback, IBarCameraButtonPlayback, IBarCurrentPlayerPlayback, IBarRuleState | `REVIEW_REQUIRED` | Textes Trade/IBar et Banque maisons/hôtels portés ; notifications/animations à comparer (A03). |
| Source/monopoly/UDOpts.cpp | OptionsUI, OptionsFilePlayback, OptionsNavigationPlayback, OptionsOptionPlayback, OptionsTogglePlayback, OptionsHelpPlayback, OptionsSaveRuntime, OptionsSavePlayback, UserInterface, IBar, IBarBackdropPlayback | `REVIEW_REQUIRED` | Load/Save, Credits, QuickHelp et FullHelp portable via exporteur externe raccordés (C02) ; export/navigation à qualifier. |
| Source/monopoly/UDPieces.cpp | PiecePlacement, PieceRuntime, PieceCamera, PieceMovePlan, PieceMovePlayback, PieceMoveIngress, PieceInterpolation, PieceJailPlan, PieceJailPlayback, PieceIdleTransition, PieceIdlePlayback, PieceIdleDisplay, PieceBuildingDisplay, PieceShadowDisplay, DiceIngress, DiceDisplay, BoardGeometry | `REVIEW_REQUIRED` | Pièces et animations présentes ; transitions à comparer (A03). |
| Source/monopoly/UDPsel.cpp | PlayerSelection, PlayerSetupFlow, LocalPlayers, IBar | `REVIEW_REQUIRED` | Phases locales et entrée réseau raccordées (C01), attente de synchronisation et retour local ; qualification Q02 ouverte. |
| Source/monopoly/UDSound.cpp | UDSoundRuntime, TokenVoiceCatalog, PennybagsCatalog, AudioRuntime, Engine, OptionsUI, PieceMovePlan | `REVIEW_REQUIRED` | Audio et transitions présents ; comparer les usages actifs (A03, A06). MIDI désactivé dans C_ArtLib.h. |
| Source/monopoly/UDStats.cpp | StatsUI, StatsPlayback, StatsBankPlayback, StatsCalculatorPlayback, StatsCalculatorDeedPickerPlayback, StatsCalculatorLogic, StatsCalculatorUI, StatsPlayerPlayback, StatsPlayerCashPlayback, StatsPlayerAuxPlayback, StatsFutureImmunityUI, StatsFutureImmunityPlayback, StatsFutureImmunityTextPlayback, StatsDeedPlayback, StatsDeedFloaterPlayback, StatsDeedFloaterTextPlayback, StatsDeedBarPlayback, StatsDeedValueTextPlayback, UserInterface, Engine, AIUtility, BoardRules | `PORTED_PARTIAL` | Deed floater, value bars, calculateur, textes et historique portés ; actes Europe dynamiques à raccorder (C03) ; six références Europe absentes du corpus (D01), branches désactivées par USA_VERSION=1 dans la source livrée. |
| Source/monopoly/UDTrade.cpp | TradeUI, TradePropertyPlayback, TradeOfferIconPlayback, TradeCashDialogPlayback, TradeContractDialogPlayback, TradeBackdropPlayback, TradeTokenPlayback, TradeActionButtonPlayback, UserInterface, IBar, LocalPlayers::tradeSourcePlayer, RuleTrade | `REVIEW_REQUIRED` | Échanges, listes et panneau Future/Immunity portés ; comparaison des transitions (A02, A03). |
| Source/monopoly/UDCGE.cpp | Aucun requis | `LEGACY_UNUSED` | Éditeur retiré vers une application séparée : UDCGE.cpp, appels commentés dans Userifce/display. |
| Source/monopoly/UDChat.cpp | ChatRuntime, ChatRecipientPlayback, ChatOptionPlayback, ChatFluffPlayback, Messaging, UserInterface, Engine | `REVIEW_REQUIRED` | Texte, messages publics/privés/spectateurs, Fluff, wrap/scroll et alpha présents ; raccordement distant C01 ajouté ; comparaison A03 et qualification Q02 ouvertes. |
| Source/monopoly/UDPlrCfg.cpp | Aucun requis | `LEGACY_UNUSED` | Module absent de TheGame.dsp ; seul appelant identifié dans UDPlrSum également exclu. |
| Source/monopoly/UDPlrSum.cpp | Aucun requis | `LEGACY_UNUSED` | Module absent de TheGame.dsp ; aucun appelant externe actif identifié. |
| Source/monopoly/UDRules.cpp | Aucun requis | `LEGACY_UNUSED` | Module absent de TheGame.dsp ; aucun appelant externe actif identifié. |
| Source/monopoly/UDTitle.cpp | Aucun requis | `LEGACY_UNUSED` | Module absent de TheGame.dsp ; aucun appelant externe actif identifié. |
| Source/monopoly/UDPenny.cpp | UDPennyVoice, TokenVoiceCatalog, PennybagsCatalog, UserInterface, Engine, UDSoundRuntime | `PORTED_PARTIAL` | Voix et animations présentes ; génération des 28 actes recto/verso Europe encore absente (C03). Notifications et transitions restent à comparer (A03). |
| Source/monopoly/UDUtils.cpp | UDUtils, ResourcePaths, ResourceContext, TextureCatalog | `REVIEW_REQUIRED` | Sauvegarde/restauration des 39 vues et textures portée. Ombres des onze pions : conversion de luminance avec éclaircissement de 15 % et blending ZERO/SRC_ALPHA intégrés. ZBias et rendu retail restent à qualifier (A03, Q03). |
| Source/monopoly/Unility.cpp | Aucun requis dans la cible livree | `LEGACY_UNUSED` | Lié au projet mais appels sous #if 0 ou FOREMAILVERSION=0 ; Userifce/Main/GameInc. |
| Source/monopoly/Debugart.cpp | DebugDialogs | `PORTED_COMPLETE` | Diagnostics et contrats de debug portés. |

## Services ArtLib consommes

| Origine / contrat | Équivalent moderne | Statut | Portée, preuve et suite |
|---|---|---|---|
| Source/artlib/L_Main.* | Application, Engine, Game | `REPLACED_PORTABLE` | Initialisation et arrêt ArtLib remplacés par les owners modernes. |
| Source/artlib/L_Timers.* | Timers + UIMessages | `REPLACED_PORTABLE` | Timers remplacés par le runtime moderne. |
| Source/artlib/L_UIMsg.* | UIMessages, Application, Timers | `REPLACED_PORTABLE` | FIFO 100, payload possédé, attente, flush, pourcentage et délestage MouseMoved au-delà de 50 % portés. Tests de contrat intégrés, non exécutés sur ce HEAD ; producteurs séquence/vidéo suivis séparément en A06. |
| Source/artlib/L_Data.* | DataBanks, LegacyDataArchive, DataBankRegistry, LegacyDataArchiveBuilder | `REVIEW_REQUIRED` | Bitmaps runtime, fichiers externes, snapshots et leases présents ; LRU global DAT ajouté ; mémoire des autres caches/sentinelles encore à comparer (A05). |
| Source/artlib/L_Chunk.* | LegacyChunkReader, openLegacyChunkReader | `PORTED_COMPLETE` | Lecture des chunks portée. |
| Source/artlib/L_Grafix.*, L_Rend2D.*, L_Sprite.* | Display, SequenceBitmapRenderData, SequenceWorld2DSlot, World2DRenderer | `REPLACED_PORTABLE` | Surfaces natives, clé verte, alpha absolu, dimensions, fill/blit avec clipping et publication immuable portés dans RuntimeBitmapStore ; BoardBackdrop utilise ce backend. Compositions UI et métriques restent en A03/A04, actes Europe en C03. |
| Source/artlib/L_Rend3D.* | GPUFrame, SequenceWorld3DSlot, World3DGPUScene, World3DProjection, World3DRenderer | `REPLACED_PORTABLE` | Les 21 appels consommés ont un propriétaire moderne : slot 1, viewport, caméra, clipping 10/1540, fond, lumières et rendu indexé. Contrats ajoutés aux tests, non exécutés sur ce HEAD ; scènes/matériaux et qualification restent en A07/Q03. |
| Source/artlib/L_Seqncr.* | LegacySequence, SequenceClock, SequenceChildSchedule, SequenceProgram, SequenceRuntime, SequenceCommandQueue, SequenceTransforms, SequenceRenderData, SequenceBitmapRenderData | `REVIEW_REQUIRED` | Séquences et cycle des médias présents ; contrats consommés encore à comparer (A06). |
| Source/artlib/L_Fonts.*, L_Print.* | FontRuntime, SDL3_ttf | `REPLACED_PORTABLE` | Sélection Arial, taille, styles, reset préservant police/taille, dix slots, mesure et rasterisation portés. Saisie Chat remplacée par SDL ; L_Print sans appelant Monopoly. Métriques GDI/FreeType et clipping restent en A04. |
| Source/artlib/L_Keybrd.*, L_Mouse.* | traduction SDL dans Application, MousePointer, MousePointerPlayback | `REPLACED_PORTABLE` | Clavier et souris remplacés par SDL3. |
| Source/artlib/L_Sound.*, L_Midi.* | AudioRuntime, UDSoundRuntime, VoiceChatRuntime, VoiceChatPacket, feuilles Sound de SequenceRuntime, Engine | `REVIEW_REQUIRED` | Durée RIFF/WAVE portée par legacyWaveDurationTicks ; playback, gains des six appels UDSound et son WAV_tmpnext présents. Audio vidéo désormais raccordé ; qualification Q04. Aucun appel Monopoly de pan/pitch/position 3D/cache DirectSound ; usages par séquences à comparer en A06. MIDI désactivé. |
| Source/artlib/L_Video.* | VideoRuntime, VideoDecoder, VideoPresentation, SequenceVideoRuntime, OpeningMovies | `REVIEW_REQUIRED` | Vidéo CNK, frames, PCM, EOF et Stop/Stay/Loop présents ; autres contrats consommés à comparer (A06, Q04). |

## PC3D consomme

| Origine / contrat | Équivalent moderne | Statut | Portée, preuve et suite |
|---|---|---|---|
| cameras, viewports, background (camera.*, D3DDevice.*, view code) | GPUFrame, World3DProjection, World3DRenderer, Display::Viewport3D, LogicalViewport | `REVIEW_REQUIRED` | Caméras, viewports et fonds présents ; comparaison sémantique encore ouverte (A07). |
| meshes/scenes/materials (mesh*, NewMesh*, Scene.h, l_material.h) | MeshXRuntime, MeshRuntimeCache, MeshRenderData, MeshGPUCache, World3DGPUScene | `REVIEW_REQUIRED` | Chemin oldframe actif porté ; comparaison scènes/matériaux (A07). NewMesh alternatif non consommé. |
| decodeur HMD / postload MESHX (HMDData.h, NewMesh.cpp, hmdload.*) | LegacyMeshData, openLegacyMeshData, MeshXRuntime | `PORTED_COMPLETE` | GIS-10 fermé pour les types HMD consommés ; reset/joint/UIMG0/ground/envmap commentés dans hmdload.cpp. Q03. |
| vieux DirectDraw/Direct3D drivers (DDraw*, D3DDevice*) | SDL3 / SDL_GPU | `REPLACED_PORTABLE` | DirectDraw/Direct3D remplacés par SDL_GPU ; Windows D3D12 testé, autres backends Q05. |

## Donnees et verification

| Origine / contrat | Équivalent moderne | Statut | Portée, preuve et suite |
|---|---|---|---|
| DataId / DataTag / groupes | DataBanks, y compris IdWithFileFromParent | `PORTED_COMPLETE` | Identifiants groupe/tag 16+16 bits portés et testés. |
| Header et index physique DAT | LegacyDataArchive | `PORTED_COMPLETE` | En-tête DAT 28 octets et index 16 octets little-endian portés. |
| Codec DAT | zlib via uncompress2 avec consommation exacte | `PORTED_COMPLETE` | Décompression zlib via uncompress2, fenêtre 15, portée. |
| Lifecycle, lookup, metadata et ownership | LegacyDataArchive, DataBankRegistry, ResourceRuntime | `REVIEW_REQUIRED` | Lifecycle, lookup et ownership présents ; différences mémoire/sentinelles à comparer (A05). |
| Resolution des chemins DATA | ResourcePaths, UDUtils, ResourceContext | `REPLACED_PORTABLE` | Résolution de chemins portable, recherche des ressources et éditions portée. |
| CRC global DAT | option ChecksumPolicy::Verify | `REVIEW_REQUIRED` | CRC Ignore conforme au runtime ; Verify disponible. Convention des fichiers retail à qualifier (Q01). |
| Writer DAT portable | LegacyDataArchiveBuilder | `PORTED_COMPLETE` | Writer DAT pour des payloads fournis ; ne recrée pas les assets retail absents. |
| Index logique packed 6 octets | DataIndexTable, lookupIndexedDataId | `PORTED_COMPLETE` | Index logique 6 octets porté ; erreurs typées au lieu d’une sentinelle nulle, appelants à comparer (A05). |
| Lecteur CNK | LegacyChunkReader, openLegacyChunkReader | `PORTED_COMPLETE` | Lecteur CNK et validation des limites portés. |
| Parseurs semantiques CNK / sequence | LegacySequence | `REVIEW_REQUIRED` | Parsers dont vidéo portés ; Model type 8 et autres contrats conditionnés par preuve d’usage (A06). |
| SequenceClock | SequenceClock | `PORTED_COMPLETE` | Horloge de séquence et synchronisation des médias portées. |
| SequenceChildSchedule | SequenceChildSchedule | `PORTED_COMPLETE` | Planification des enfants portée. |
| Arbre runtime et execution | SequenceProgram, SequenceRuntime | `REVIEW_REQUIRED` | Arbre et feuilles courantes dont son/vidéo présents ; préchargement consommé raccordé ; modèle/callbacks à comparer (A06). |
| Commandes L_Seqncr | SequenceCommandQueue | `PORTED_COMPLETE` | Commandes consommées portées ; chaînes sans appelant actif identifié. |
| Transformations / tweekers | SequenceTransforms, etat SequenceRuntime | `REVIEW_REQUIRED` | Transformations présentes ; tweekers, audio avancé et scrollingworld à vérifier par appelant ou DAT (A06). |
| MESHX runtime | MeshXRuntime, MeshRuntimeCache | `PORTED_COMPLETE` | MESHX et postload du chemin oldframe consommé portés ; ressources retail à qualifier (Q03). |
| Render data de sequence | MeshRenderData, SequenceRenderData, SequenceBitmapRenderData | `PORTED_COMPLETE` | Échange mesh/bitmap/média vers le rendu porté ; qualification Q03. |
| Raccordement sequence -> render slots | SequenceWorld3DSlot, World3DGPUScene, SequenceWorld2DSlot, World2DRenderer | `PORTED_COMPLETE` | Séquences raccordées aux slots GPU et readback testés ; qualification Q03. |
| LANG core | LanguageCatalog, LanguageService, ResourceRuntime | `REVIEW_REQUIRED` | Lecture et adaptateurs LANG présents ; formatage et UTF-16 à comparer (A04). |
| Chaines/audio/dialogues LANG retail | aucun payload | `BLOCKED_MISSING_DATA` | Payloads LANG du jeu absents ; validation multilingue/éditions bloquée sur ces données (Q01). |
| Catalogue TexInfo | TextureCatalog | `PORTED_COMPLETE` | Catalogue de textures porté. |
| Corpus BMP TexInfo | LegacyBitmap, manifeste de 1 001 assets | `PORTED_COMPLETE` | Corpus de 1001 BMP 8/24 bits BI_RGB contrôlé ; ce corpus ne remplace pas tous les DAT retail. |
| Loader BMP runtime | LegacyBitmap, BitmapRuntimeCache, World2DRenderer, LegacyAssets | `PORTED_COMPLETE` | BMP 8/24 bits BI_RGB consommés décodés ; chemin LegacyAssets distinct existant, pas une omission prouvée (Q03). |
| Manifestes DMake | LegacyManifest, MonopolyManifestTool | `PORTED_COMPLETE` | Manifestes DMAKE : groupes, tags et types exportables ; pas les payloads absents. |
| DMAKE99 et reconstruction bit-a-bit | aucun outil historique | `MISSING_TOOLING` | Reproduction binaire exacte de DMAKE99 non fournie ; outil optionnel de reconstruction, pas prérequis du jeu moderne. |
| Banques DAT retail exactes | absentes | `BLOCKED_MISSING_DATA` | DAT retail exacts absents ; ne pas inventer leurs payloads (Q01). |
| 2DVIEW01..39 externes | noms portes, fichiers absents | `BLOCKED_MISSING_DATA` | Fichiers VIEW01..39 externes de scénarios personnalisés absents ; qualification Board Editor (Q03), pas blocage du plateau standard. |
| HMD retail / objets MESHX | headers/tags seulement, fixtures HMD synthetiques | `BLOCKED_MISSING_DATA` | HMD retail absents ; décodeur consommé implémenté mais qualification réelle ouverte (Q03). |
