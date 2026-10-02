# Monopoly — Portage moderne

<div class="doc-hero">
  <div class="doc-kicker">DOCUMENTATION TECHNIQUE</div>
  <h2>Portage moderne du Monopoly de 1999</h2>
  <p>
    Réécriture portable en <strong>C/C++23</strong> avec
    <strong>SDL3</strong> et <strong>SDL_GPU</strong>, en conservant le code
    historique comme référence de comportement.
  </p>
  <div class="doc-actions">
    <a class="doc-button primary" href="porting_status.html">Suivi du portage</a>
    <a class="doc-button" href="porting_audit.html">Audit automatisé</a>
    <a class="doc-button" href="https://github.com/Giscolab/Monopoly">Dépôt GitHub</a>
  </div>
</div>

<div class="progress-panel">
  <img src="porting-progress.svg"
       alt="Progression automatisée du portage Monopoly" />
</div>

## État au 2 octobre 2026

Le point courant couvre le code publié jusqu’à `bc9d85f`. Le parcours
USA/anglais dispose d’une scène procédurale jouable, de pions modernes et de
présentations actualisées pour les menus, cartes, Portfolio et échanges.
Les qualifications consignées dans le dépôt distinguent les sessions réelles,
les mesures ponctuelles et les contrôles ciblés. Les derniers réglages de vue
de dessus et d’options attendent encore leur qualification visuelle.

La [synthèse du portage](PORTING_STATUS.md) indique les limites restantes et
la référence CI du 2 octobre : compilation Windows et **158/158 suites CTest**
au code `bb0bb6a`. Les banques Europe/français restent
manquantes ; le décor Paris n’en tient pas lieu.

## Explorer la documentation

<div class="doc-grid">
  <a class="doc-card" href="annotated.html">
    <strong>API moderne</strong>
    <span>Classes, structures et composants du runtime moderne.</span>
  </a>
  <a class="doc-card" href="namespaces.html">
    <strong>Namespaces</strong>
    <span>Organisation fonctionnelle du code C/C++23.</span>
  </a>
  <a class="doc-card" href="files.html">
    <strong>Fichiers sources</strong>
    <span>Navigation dans les fichiers documentés de modern/src.</span>
  </a>
  <a class="doc-card" href="porting_status.html">
    <strong>Suivi du portage</strong>
    <span>Écarts connus, comparaisons restantes et qualification.</span>
  </a>
  <a class="doc-card" href="porting_audit.html">
    <strong>Audit structurel</strong>
    <span>Contrôles automatiques, incohérences et file de revue.</span>
  </a>
  <a class="doc-card" href="https://github.com/Giscolab/Monopoly/actions">
    <strong>Validation continue</strong>
    <span>Build Windows/MSVC, CTest et génération documentaire.</span>
  </a>
</div>

## Démarrage rapide

Sur l’installation Windows déjà construite et équipée des ressources requises,
lancer `modern/PlayModern.cmd`. Il privilégie Release, ouvre la présentation
procédurale en 1920x1080 et permet de basculer le plein écran avec F11.
Le [guide de jeu procédural](PROCEDURAL_PLAY.md) détaille les exports et
le [guide des ressources](RESOURCE_SETUP.md) décrit les banques retail à fournir.

Pour construire le code depuis la racine du dépôt :

```bash
cmake -S modern -B modern/build -DBUILD_TESTING=ON
cmake --build modern/build --config Debug
ctest --test-dir modern/build -C Debug --output-on-failure
```

Les tests vidéo nécessitent aussi FFmpeg et FFprobe accessibles dans le PATH
ou via les variables documentées dans `modern/VIDEO_RUNTIME.md`.

Les dépendances nécessaires sont récupérées par CMake lorsque cela est prévu
par le projet. Le détail des cibles et composants est visible dans la
documentation des fichiers et dans `modern/CMakeLists.txt`.

## Guides et preuves de qualification

| Document | Contenu |
|---|---|
| [Ressources au démarrage](RESOURCE_SETUP.md) | Banques DAT, langues et staging des assets |
| [Présentation moderne](PRESENTATION.md) | Fenêtre, F11, espaces UI/3D et rendu GPU |
| [Scène procédurale jouable](PROCEDURAL_PLAY.md) | Lanceur Windows, exports et premières parties réelles |
| [Intégration Blender/glTF](BLENDER_INTEGRATION_SPEC.md) | Contrats d’export, matériaux et repli retail |
| [Premières passes visuelles](VISUAL_POLISH.md) | Cadrage, ombres, MSAA et preuves initiales |
| [Journal UI/runtime des 1er et 2 octobre](VISUAL_UI_POLISH.md) | Corrections récentes, mesures et qualifications encore ouvertes |

## Organisation du dépôt

| Zone | Rôle |
|---|---|
| `Source/` | Code historique conservé comme référence sémantique |
| `modern/src/` | Implémentation portable active |
| `modern/tests/` | Tests de contrat, régression et intégration |
| `modern/PORTING_STATUS.md` | Plan courant et validation de référence |
| `modern/PORTING_MATRIX.md` | Inventaire complet des contrats et statuts |
| `modern/PORTING_AUDIT.md` | Rapport généré automatiquement |
| `.github/workflows/` | Audit, validation et publication de la documentation |

## Lire les indicateurs correctement

Le graphique est généré automatiquement depuis l’inventaire. Ses compteurs
ne mesurent pas la fidélité fonctionnelle au jeu de 1999 : les familles et
leurs sous-contrats se recouvrent. Aucun pourcentage fonctionnel n’est établi.

Pour le détail, utiliser en priorité le
[suivi du portage](porting_status.html), l’[inventaire](porting_matrix.html) et
[l'audit automatisé](porting_audit.html).

<div class="doc-footer-note">
  Cette documentation est reconstruite et publiée sur GitHub Pages à chaque
  modification pertinente de la branche <code>master</code>.
</div>
