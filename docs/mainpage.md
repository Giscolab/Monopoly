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
    <span>Matrice de référence, campagnes et état des familles.</span>
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

Le projet moderne se configure directement depuis la racine du dépôt :

```bash
cmake -S modern -B modern/build -DBUILD_TESTING=ON
cmake --build modern/build --config Debug
ctest --test-dir modern/build -C Debug --output-on-failure
```

Les dépendances nécessaires sont récupérées par CMake lorsque cela est prévu
par le projet. Le détail des cibles et composants est visible dans la
documentation des fichiers et dans `modern/CMakeLists.txt`.

## Organisation du dépôt

| Zone | Rôle |
|---|---|
| `Source/` | Code historique conservé comme référence sémantique |
| `modern/src/` | Implémentation portable active |
| `modern/tests/` | Tests de contrat, régression et intégration |
| `modern/PORTING_STATUS.md` | Carte de référence du portage |
| `modern/PORTING_AUDIT.md` | Rapport généré automatiquement |
| `.github/workflows/` | Audit, validation et publication de la documentation |

## Lire les indicateurs correctement

Le graphique de progression est généré automatiquement. Les différents
indicateurs n'ont pas la même signification : un pourcentage structurel ne
constitue pas à lui seul une preuve de fidélité fonctionnelle au jeu de 1999.

Pour le détail, utiliser en priorité le
[suivi du portage](porting_status.html) et
[l'audit automatisé](porting_audit.html).

<div class="doc-footer-note">
  Cette documentation est reconstruite et publiée sur GitHub Pages à chaque
  modification pertinente de la branche <code>master</code>.
</div>
