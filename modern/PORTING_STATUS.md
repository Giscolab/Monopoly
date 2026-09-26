# État et plan du portage {#porting_status}

## Objectif et périmètre

Reproduire les comportements actifs du Monopoly d’origine dans le runtime C++23/SDL3/SDL_GPU de `modern/`. `Source/` reste la référence en lecture seule. Le mot **legacy** signifie « code d’origine » : une fonctionnalité encore utilisée reste à porter. Seuls les chemins désactivés, retirés ou sans usage actif démontré peuvent être exclus.

Le portage est fonctionnel sur des contrats testés, mais **sa fidélité complète au jeu retail n’est pas établie**. Aucun pourcentage fonctionnel n’est actuellement justifié. Les 79 entrées sont conservées dans l’[inventaire détaillé](PORTING_MATRIX.md), avec les écarts connus séparés des comparaisons restant à mener.

## Ce qui est implémenté

- Initialisation, boucle de jeu, timers, slots, règles, IA, messages locaux, archives et restauration de partie.
- Surfaces UI : enchères, plateau, pièces, sélection locale, Trade/Future/Immunity, IBar et Banque maisons/hôtels, chat avec texte et défilement, statistiques et deed floater, options Load/Save, Credits et QuickHelp.
- **GIS-8** : capture SDL3, PCM 11025 Hz/8-bit/mono, GSM 6.10 WAV49, DAT1/DATN, playback et transport TCP de voix. La propriété des joueurs distants reste un travail distinct (C01).
- **GIS-9** : TextureCatalog → ResourcePaths/BMP → mesh raccordé. **GIS-10** : types HMD consommés portés. La suite porte sur les données et scénarios réels, pas sur la recréation de types désactivés.
- Lecture DAT/CNK/LANG, ressources, séquences, fonts, rendu GPU, vidéo avec frames/PCM et cycle de vie, films d’ouverture. Les codecs vidéo utilisent FFmpeg/FFprobe externes ; voir [vidéo](VIDEO_RUNTIME.md).

« Implémenté » décrit la présence du contrat moderne ; les comparaisons sémantiques et qualifications encore ouvertes sont détaillées ci-dessous. Le panneau Future/Immunity est terminé et n’est plus une tâche restante.

## Écarts actifs à fermer

| ID | Travail | Critère de fermeture |
|---|---|---|
| C01 | Raccorder la sélection réseau, la propriété des joueurs et leurs actions à la messagerie moderne. Le transport voix ne constitue pas un multijoueur complet. | Scénario hôte/client avec attribution, admission, messages et actions autorisées prouvés par les appelants de `UDPsel`/`Mess`. Pas de simulation DirectPlay ; la migration d’hôte est désactivée dans la source. |
| C02 | Fournir un accès FullHelp portable. Le hook Windows utilise WinHelpW ; aucun lecteur équivalent n’est raccordé sur les autres plateformes. | Aide accessible avec les fichiers disponibles et erreurs explicites pour les ressources absentes. `Mono01.hlp`, `Mono_US.hlp` et `Documents/WestwoodMonopoly Rules.hlp` existent dans `Source/monopoly/` ; leur présence ne prouve pas leur déploiement ni la disponibilité du lecteur Windows. |
| D01 | Résoudre les six libellés/références Europe manquants de l’historique maisons/hôtels et hypothèque/levée d’hypothèque. | Références issues des données/langues originales, sans identifiants ni textes inventés. L’achat utilise déjà la référence LANG 3178. |

QuickHelp n’est plus bloqué par les accents hors Windows : son décodage CP1252 vers UTF-8 est explicite et testé. Les fichiers fournis sont compatibles avec ce choix ; leur contenu ne permet pas de distinguer CP1252 de Latin-1 pour les octets qu’ils emploient.

## Comparaisons encore nécessaires

Ces points remplacent les anciennes mentions vagues « partiel » ou « futur ». Ils demandent une comparaison ciblée ; ils ne constituent pas tous des fonctionnalités manquantes. Pour chaque divergence : citer l’appelant ou l’asset, reproduire le comportement, corriger et tester. Une exclusion demande une preuve source.

| ID | Zone | Vérification attendue |
|---|---|---|
| A01 | RULE / UserInterface | Branches internes, transitions et scénarios complets de règles, au-delà des fixtures existantes. |
| A02 | IA / trade | Décisions, évaluations et transitions des échanges. `Trade_SendItems` est déjà raccordé. |
| A03 | UI / audio / Penny | Notifications, animations, transitions et surfaces résiduelles effectivement consommées ; ne pas rouvrir les propriétaires de texte déjà portés. |
| A04 | LANG / FONTS / GRAFIX | Formatage, traitement UTF-16, métriques et clipping ; corriger les divergences reproduites. |
| A05 | DATA | Contrats mémoire/LRU et sentinelles exigés par les appelants. Bitmaps runtime, fichiers externes, snapshots et leases existent déjà. |
| A06 | Séquenceur | Préchargement, modèle, attributs audio avancés, tweekers, labels et callbacks selon les usages C++ ou DAT. L’absence d’appel C++ seule n’exclut pas un usage par données. |
| A07 | PC3D | Caméras, scènes et matériaux au-delà des contrats HMD consommés déjà fermés. |

MIDI est désactivé par `CE_ARTLIB_EnableSystemMidi=0`. Les cas HMD reset/joint/UIMG0/ground/envmap sont commentés dans `hmdload.cpp`. Le chemin `NewMesh` alternatif n’est pas celui sélectionné par les appelants actifs. Ces éléments ne sont pas des tâches actives sans nouvelle preuve contraire. Les sept modules exclus et leurs justifications figurent dans la matrice.

## Qualification sur données et plateformes réelles

| ID | Qualification | Limite actuelle |
|---|---|---|
| Q01 | Parties jouables avec DAT/LANG/CNK retail, règles, IA, UI et langues. | Les payloads retail nécessaires ne sont pas fournis. Des fixtures ne prouvent pas une partie complète. |
| Q02 | Voix entre deux processus puis deux machines, capture et écoute physiques. | Les tests de transport/codec ne prouvent pas le parcours utilisateur ni le matériel. Voir [réseau et voix](NETWORK_VOICE.md). |
| Q03 | Textures/UV/HMD, éditions, devises et scénarios Board Editor personnalisés. | Corpus BMP disponible ; HMD retail et vues externes personnalisées manquants. Le plateau standard ne dépend pas de ces vues externes. |
| Q04 | Films Indeo/Bink réels et synchronisation audiovisuelle, installation FFmpeg. | Runtime et tests disponibles ; médias retail absents. |
| Q05 | Linux/macOS, POSIX, Vulkan/Metal et disponibilité de FullHelp. | La validation de référence est Windows/D3D12, pas une qualification de toutes les plateformes. |

La reproduction binaire exacte de DMAKE99 est un outil de reconstruction optionnel. Le writer DAT moderne accepte des payloads fournis ; aucun outil ne peut recréer les assets absents à partir des seuls manifestes.

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
