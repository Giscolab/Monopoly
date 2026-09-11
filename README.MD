# Monopoly — portage moderne

Ce dépôt conserve le code source historique de *Monopoly* et accueille le
portage moderne "work in progress".

Le portage, encore en cours, utilise C et C++23 avec des technologies portables
actuelles. Il se trouve dans [`modern/`](modern/), tandis que le code d'origine
reste disponible dans [`Source/`](Source/).

## 📊 Progression

![Workflow](https://github.com/Giscolab/Monopoly/actions/workflows/main.yml/badge.svg)

Le suivi est automatisé à partir de [`modern/PORTING_STATUS.md`](modern/PORTING_STATUS.md),
la matrice de référence du portage. Le workflow publie quotidiennement la répartition
des composants et un indice reproductible de progression.

- **Statut** : Work In Progress
- **Dernière mise à jour** : voir les [logs du workflow](https://github.com/Giscolab/Monopoly/actions/workflows/main.yml)
- **Formule de l'indice** : complet/remplacé = 100 %, partiel = 50 %, non démarré = 0 % ; les éléments bloqués, tooling manquant et legacy inutilisé sont exclus du dénominateur.

Cet indice est un indicateur mécanique de la matrice de portage, pas une estimation de la jouabilité globale.

## Sources et crédits

Le code d'origine est désormais accessible publiquement. Sa provenance reste
citée ici :

- [Dépôt public du code source d'origine](https://github.com/RetailGameSourceCode/Monopoly)
- [Publication citée dans le README d'origine](https://twitter.com/MrTalida/status/1025016038394613760)

Le [README d'origine](README_ORIGINAL.MD) est conservé tel quel dans ce dépôt.
