# État et plan du portage {#porting_status}

## Objectif et périmètre

Reproduire les comportements actifs du Monopoly d’origine dans le runtime C++23/SDL3/SDL_GPU de `modern/`. `Source/` reste la référence en lecture seule. Le mot **legacy** signifie « code d’origine » : une fonctionnalité encore utilisée reste à porter. Seuls les chemins désactivés, retirés ou sans usage actif démontré peuvent être exclus.

Le portage est fonctionnel sur des contrats testés, mais **sa fidélité complète au jeu retail n’est pas établie**. Aucun pourcentage fonctionnel n’est actuellement justifié. Les 79 entrées sont conservées dans l’[inventaire détaillé](PORTING_MATRIX.md), avec les écarts connus séparés des comparaisons restant à mener.

## Ce qui est implémenté

- Initialisation, boucle de jeu, timers, slots, règles, IA, messages locaux, archives et restauration de partie.
- Surfaces UI : enchères, plateau, pièces, sélection locale, Trade/Future/Immunity, IBar et Banque maisons/hôtels, chat avec texte et défilement, statistiques et deed floater, options Load/Save, Credits et QuickHelp.
- **GIS-8** : capture SDL3, PCM 11025 Hz/8-bit/mono, GSM 6.10 WAV49, DAT1/DATN, playback et transport TCP de voix. Les sessions de jeu C01 ont désormais leur admission, ownership, routage et retour local ; voir [sessions TCP](NETWORK_GAME.md). Sa validation sur le HEAD courant reste à confirmer.
- **GIS-9** : TextureCatalog → ResourcePaths/BMP → mesh raccordé. **GIS-10** : types HMD consommés portés. La suite porte sur les données et scénarios réels, pas sur la recréation de types désactivés.
- Lecture DAT/CNK/LANG, ressources, séquences, fonts, rendu GPU, vidéo avec frames/PCM et cycle de vie, films d’ouverture. Les codecs vidéo utilisent FFmpeg/FFprobe externes ; voir [vidéo](VIDEO_RUNTIME.md).

« Implémenté » décrit la présence du contrat moderne ; les comparaisons sémantiques et qualifications encore ouvertes sont détaillées ci-dessous. Le panneau Future/Immunity est terminé et n’est plus une tâche restante.

## Code à terminer

Aucun manque de code confirmé n’est actuellement listé ici après implémentation de C03. Les comparaisons A01–A07 restent ouvertes et peuvent révéler d’autres écarts.

QuickHelp n’est plus bloqué par les accents hors Windows : son décodage CP1252 vers UTF-8 est explicite et testé. Les fichiers fournis sont compatibles avec ce choix ; leur contenu ne permet pas de distinguer CP1252 de Latin-1 pour les octets qu’ils emploient.

Un écart actif connu est un comportement utilisé dans la version d’origine dont une différence ou une absence moderne a été identifiée. C03 est désormais implémenté et reste à qualifier visuellement ; D01 dépend de données Europe absentes. Les lignes UDStats et UDPenny de la matrice partagent C03 : elles ne représentent pas deux travaux distincts. Les comparaisons A et qualifications Q ci-dessous ne sont pas, à elles seules, des défauts démontrés.

## Comparaisons encore nécessaires

Ces points remplacent les anciennes mentions vagues « partiel » ou « futur ». Ils demandent une comparaison ciblée ; ils ne constituent pas tous des fonctionnalités manquantes. Pour chaque divergence : citer l’appelant ou l’asset, reproduire le comportement, corriger et tester. Une exclusion demande une preuve source.

| ID | Zone | Vérification attendue |
|---|---|---|
| A01 | RULE / UserInterface | Branches internes, transitions et scénarios complets de règles, au-delà des fixtures existantes. |
| A02 | IA / trade | Décisions, évaluations et transitions des échanges. `Trade_SendItems` est déjà raccordé. |
| A03 | UI / audio / Penny | Notifications, animations, transitions et surfaces résiduelles effectivement consommées ; ne pas rouvrir les propriétaires de texte déjà portés. |
| A04 | LANG / FONTS / GRAFIX | 96 DPI d’origine raccordés ; formatage, UTF-16, métriques et clipping encore à comparer. |
| A05 | DATA | Contrats mémoire/LRU et sentinelles exigés par les appelants. Bitmaps runtime, fichiers externes, snapshots et leases existent déjà ; LRU global des blocs DAT ajouté, autres caches à comparer. |
| A06 | Séquenceur | Préchargement consommé raccordé ; modèle, attributs audio avancés, tweekers, labels et callbacks à comparer selon les usages C++ ou DAT. L’absence d’appel C++ seule n’exclut pas un usage par données. |
| A07 | PC3D | Caméras, scènes et matériaux au-delà des contrats HMD consommés déjà fermés. |

MIDI est désactivé par `CE_ARTLIB_EnableSystemMidi=0`. Les cas HMD reset/joint/UIMG0/ground/envmap sont commentés dans `hmdload.cpp`. Le chemin `NewMesh` alternatif n’est pas celui sélectionné par les appelants actifs. Ces éléments ne sont pas des tâches actives sans nouvelle preuve contraire. Les sept modules exclus et leurs justifications figurent dans la matrice.

## Code implémenté : validations restantes

| ID | Code présent | Validation attendue |
|---|---|---|
| C01 | Code des sessions de jeu TCP raccordé : menu/CLI, admission, ownership, actions, notifications privées, déconnexion et retour local. | Qualification Q02 encore ouverte ; résultat de validation du HEAD courant à confirmer. Voir [usage](NETWORK_GAME.md). |
| C02 | FullHelp portable raccordé : conversion HLP → HTML asynchrone puis ouverture navigateur. | Exporteur externe `winhlp` requis ; conversion réelle, sujets/images et plateformes à qualifier. Voir [aide complète](FULL_HELP.md). |
| C03 | Génération des 28 rectos/28 versos Europe et catalogue runtime raccordés aux enchères, IBar, Trade et Stats ; variantes de langue, plateau, devise et règle maisons/hôtel. | Qualification visuelle avec les modèles DAT retail en Q03 ; génération et remplacement du catalogue couverts par tests ciblés, résultat consigné ci-dessous. |

## Données manquantes

| ID | Données | Périmètre et condition de résolution |
|---|---|---|
| D01 | Six références Europe d’historique absentes du corpus livré ; aucune valeur inventée. | Les appels sont dans les branches Europe de `UDIBar.cpp`, désactivées par `USA_VERSION=1` dans le build source livré. Les définitions/données Europe restent nécessaires pour cette édition ; l’achat utilise déjà LANG 3178. |

## Qualification sur données et plateformes réelles

| ID | Qualification | Limite actuelle |
|---|---|---|
| Q01 | Parties jouables avec DAT/LANG/CNK retail, règles, IA, UI et langues. | Les payloads retail nécessaires ne sont pas fournis. Des fixtures ne prouvent pas une partie complète. |
| Q02 | Voix entre deux processus puis deux machines, capture et écoute physiques. | Les tests de transport/codec ne prouvent pas le parcours utilisateur ni le matériel. Voir [réseau et voix](NETWORK_VOICE.md). |
| Q03 | Textures/UV/HMD, éditions, devises et scénarios Board Editor personnalisés. | Corpus BMP disponible ; HMD retail et vues externes personnalisées manquants. Le plateau standard ne dépend pas de ces vues externes. |
| Q04 | Films Indeo/Bink réels et synchronisation audiovisuelle, installation FFmpeg. | Runtime et tests disponibles ; médias retail absents. |
| Q05 | Linux/macOS, POSIX, Vulkan/Metal et disponibilité de FullHelp. | La validation de référence est Windows/D3D12, pas une qualification de toutes les plateformes. |

La reproduction binaire exacte de DMAKE99 est un outil de reconstruction optionnel. Le writer DAT moderne accepte des payloads fournis ; aucun outil ne peut recréer les assets absents à partir des seuls manifestes.

## Lot de code suivant la référence

Sessions réseau C01, aide C02, attente des actions IA avant trade (A02), tailles de fonts normalisées à 96 DPI (A04), cache DAT LRU global (A05) et préchargement des ressources de séquence (A06) sont implémentés dans les commits `ce78e39` à `ccf5f28`. Les autres comparaisons A01–A07 restent ouvertes : ces corrections ciblées ne prouvent pas leur clôture exhaustive.

Les onze branches `assistant/*` sont intégrées (dix têtes distinctes). Elles ajoutent la durée WAV, le backend de surfaces GRAFIX partagé, le blending des ombres et des contrats de tests. Leurs conclusions utiles sont reprises dans la matrice ; les anciennes réserves déjà résolues ne sont pas réintroduites. La génération des actes Europe C03 est implémentée dans le lot suivant, avec le survol Trade et le remplacement transactionnel des 56 surfaces.

**Compilations et tests autorisés via GitHub Actions.** Les premiers commits de ce lot avaient été publiés sans validation, avec `[skip ci]`. Cette restriction est levée. La référence ci-dessous précède ces modifications ; elle ne les qualifie pas. Pour chaque résultat CI, vérifier le SHA testé : un succès sur un commit antérieur ne qualifie pas les changements suivants.

## Validation de référence

Validation de référence : **132/132 suites CTest passées** — code `d77be96`, Windows/MSVC Debug, 26 septembre 2026.

Application compilée ; test OptionsVisualPlayback ciblé puis CTest global réussi en 37,78 s. Deux cas internes ResourcePaths restent non exécutés (collision dépendant de la casse et permissions de liens symboliques). Aucun test CTest ni test GPU n’est converti en skip. Cette validation comprend le décodage portable QuickHelp ; elle ne remplace pas Q01–Q05.

Les journaux locaux se trouvent sous `modern/build/options-help-*-20260926.log`. Les exécutions CI et leurs logs restent la preuve de validation distante ; la référence ci-dessus n’est pas une affirmation sur le dernier run GitHub.

## Suivi automatique et maintenance

- Ce fichier contient le plan courant et la référence de qualification ; [PORTING_MATRIX.md](PORTING_MATRIX.md) contient l’unique inventaire des statuts.
- GitHub Actions contrôle la structure CMake et les scripts, génère [PORTING_AUDIT.md](PORTING_AUDIT.md) et `porting-progress.svg`, puis compile et exécute CTest sous Windows. FFmpeg/FFprobe sont provisionnés et vérifiés pour les tests vidéo.
- La documentation Doxygen inclut le plan, la matrice et le rapport généré. Les changements de ces documents déclenchent leur publication.
- L’audit structurel n’est pas un audit de fidélité. Les compteurs d’inventaire ne sont pas des pourcentages d’achèvement : familles et sous-contrats se recouvrent.
- Ne modifier manuellement ni le SVG ni le rapport généré. Supprimer une tâche seulement après correction vérifiée ou exclusion justifiée ; conserver les décisions utiles dans la matrice, les anciens checkpoints dans l’historique Git.
