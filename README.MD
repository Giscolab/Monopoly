# Monopoly — portage moderne

Ce dépôt conserve le code source historique de *Monopoly* et accueille le
portage moderne "work in progress".

Le portage, encore en cours, utilise C et C++23 avec des technologies portables
actuelles. Il se trouve dans [`modern/`](modern/), tandis que le code d'origine
reste disponible dans [`Source/`](Source/).

## 📊 Progression

![Workflow](https://github.com/Giscolab/Monopoly/actions/workflows/main.yml/badge.svg)

![Portabilité du code](modern/porting-progress.svg)

Le suivi est automatisé à partir de [`modern/PORTING_STATUS.md`](modern/PORTING_STATUS.md).
Le SVG ci-dessus est régénéré par GitHub Actions à chaque changement suivi sous `modern/`
(hors SVG généré), lors d'une modification du workflow/script, chaque nuit ou manuellement.

Les trois barres ont volontairement des sens différents :

- **Estimation fonctionnelle** : estimation prudente maintenue dans `PORTING_STATUS.md` ; GitHub Actions la lit et l'affiche automatiquement.
- **Familles engagées** : calcul automatique des familles `Jeu Monopoly` + `Services ArtLib` qui ne sont plus `NOT_STARTED`.
- **Indice mécanique** : calcul automatique sur toute la matrice active : complet/remplacé = 100 %, partiel = 50 %, non démarré = 0 % ; bloqué/tooling/legacy-unused sont exclus.

Ainsi, l'indice mécanique peut rester stable pendant qu'une grosse famille déjà `PORTED_PARTIAL` progresse fortement ; les trois barres évitent de confondre cette mesure avec la progression fonctionnelle globale.


## Sources et crédits

Le code d'origine est désormais accessible publiquement. Sa provenance reste
citée ici :

- [Dépôt public du code source d'origine](https://github.com/RetailGameSourceCode/Monopoly)
- [Publication citée dans le README d'origine](https://twitter.com/MrTalida/status/1025016038394613760)

Le [README d'origine](README_ORIGINAL.MD) est conservé tel quel dans ce dépôt.