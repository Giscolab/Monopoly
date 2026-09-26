# Monopoly — portage moderne

Ce dépôt conserve le code source historique de *Monopoly* et accueille le
portage moderne "work in progress".

Le portage, encore en cours, utilise C et C++23 avec des technologies portables
actuelles. Il se trouve dans [`modern/`](modern/), tandis que le code d'origine
reste disponible dans [`Source/`](Source/).

## 📊 Progression

![Workflow](https://github.com/Giscolab/Monopoly/actions/workflows/main.yml/badge.svg)

![Portabilité du code](modern/porting-progress.svg)

Le [plan courant](modern/PORTING_STATUS.md) distingue les écarts connus, les comparaisons
restantes et la qualification sur données réelles. L’[inventaire détaillé](modern/PORTING_MATRIX.md)
conserve les contrats du portage et leurs statuts.

GitHub Actions contrôle cet inventaire, régénère le graphique et le rapport structurel,
puis compile et exécute les tests sous Windows. Les compteurs ne mesurent pas un
pourcentage de fidélité au jeu d’origine ; aucun pourcentage fonctionnel n’est établi.

## 📚 Documentation technique

![Documentation](https://github.com/Giscolab/Monopoly/actions/workflows/docs.yml/badge.svg)

La documentation Doxygen du portage moderne est publiée automatiquement sur
[GitHub Pages](https://giscolab.github.io/Monopoly/). Elle couvre l'API de
`modern/src/`, le suivi de portage et l'audit structurel, avec le thème
Doxygen Awesome.

## Sources et crédits

Le code d'origine est désormais accessible publiquement. Sa provenance reste
citée ici :

- [Dépôt public du code source d'origine](https://github.com/RetailGameSourceCode/Monopoly)
- [Publication citée dans le README d'origine](https://twitter.com/MrTalida/status/1025016038394613760)

Le [README d'origine](README_ORIGINAL.MD) est conservé tel quel dans ce dépôt.