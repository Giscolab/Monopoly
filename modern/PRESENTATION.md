# Présentation moderne

Le runtime conserve deux espaces logiques distincts :

- UI/2D historique : 800x600, pour préserver les coordonnées et les écrans retail.
- Monde 3D historique : 800x450, donc déjà 16:9.

Le rendu 3D est maintenant projeté indépendamment sur la surface moderne.
Sur un écran 16:9, le viewport 3D principal peut donc remplir la présentation
sans étirer l'UI 4:3. Les viewports Status et Trade gardent leurs proportions
historiques relatives dans ce canevas 16:9.

## Mode de fenêtre

Le mode par défaut est le plein écran sans bordure à la résolution du bureau.

Options disponibles :

```powershell
.\MonopolyModern.exe --windowed
.\MonopolyModern.exe --windowed --resolution 1920x1080
.\MonopolyModern.exe --exclusive-fullscreen --resolution 2560x1440
.\MonopolyModern.exe --exclusive-fullscreen --resolution 3840x2160
```

`--fullscreen` demande explicitement le plein écran sans bordure.
`--resolution WIDTHxHEIGHT` configure la fenêtre ou le mode exclusif.
En plein écran sans bordure, la résolution du bureau reste la référence.

F11 bascule entre le plein écran sans bordure et la taille de fenêtre configurée.

## Présentation GPU

Le swapchain SDL_GPU est configuré explicitement en SDR avec deux frames
maximum en vol. Le mode par défaut est VSync :

```powershell
.\MonopolyModern.exe --present-mode vsync
.\MonopolyModern.exe --present-mode mailbox
.\MonopolyModern.exe --present-mode immediate
```

Si Mailbox ou Immediate n'est pas pris en charge par le GPU/fenêtre,
le runtime revient à VSync au lieu d'échouer.

Le moteur de jeu conserve son horloge fixe historique de 60 Hz via
`std::chrono::steady_clock`. Le rythme de rendu est indépendant :
un écran 120/144 Hz peut présenter davantage de frames sans accélérer
la simulation.

## Mesure réelle

Le titre de la fenêtre affiche environ une fois par seconde :

```text
Monopoly Modern - 1920x1080 - 60.0 FPS
```

La résolution affichée est la taille physique en pixels de la fenêtre/surface,
pas le canevas logique 800x600. Le FPS est le nombre de frames GPU soumises
par seconde ; il sert au diagnostic de performance, pas à une promesse
indépendante du matériel.

## Coordonnées souris

Quand un viewport 3D est actif, les événements situés dans sa zone visible
sont remappés avec le canevas 800x450 16:9. En dehors du viewport 3D,
les écrans et contrôles restent remappés avec le canevas UI 800x600.

Cette séparation évite de déformer les écrans historiques tout en préparant
le plateau et les futurs assets Blender/glTF à une présentation 1080p,
1440p ou 4K.

## Matériaux et scènes modernes

Le pipeline HMD conserve ses shaders Gouraud et ses ombres. Les meshes GLB
utilisent un chemin PBR distinct : facteurs métal/rugosité, cinq cartes,
samplers/mipmaps, normales tangentes, émission et alpha OPAQUE/MASK. Les
54 contrôles CPU GLB et 86 contrôles GPU ciblés passent ; BLEND et les scènes
glTF animées conservent un refus explicite. Le décor Paris n'implémente pas
d'éclairage IBL.

Les options suivantes demandent les adaptateurs de scène ; elles restent
dépendantes des ressources et du contexte de jeu :

```powershell
.\MonopolyModern.exe --edition=europe --language=fr `
  --modern-board=paris --modern-buildings=house --modern-environment=paris
```

Le plateau aligné possède la correspondance des 40 cases retail ; maison,
fontaine, gare et colonne Morris sont des exports distincts. Leur placement
hérite des transformations CNK, sans modifier les règles. Le plateau Paris ne
remplace que le contexte Europe/français/Paris/euro sans plateau personnalisé.
L'installation locale manque encore des quatre banques Europe/français : cette
commande ne peut donc pas qualifier actuellement un démarrage français réel.
Voir les [ressources requises](RESOURCE_SETUP.md).

Un probe SDL_GPU séparé soumet 4 objets, 246 batches et 915 731 triangles. Sa
mesure de cadence est documentée dans le contrat Blender. Un chronométrage de
frames avec fence exclut chargement et readback et ne mesure pas la boucle
complète du jeu. Neuf chargements d'idles lors d'un démarrage borné ne constituent
pas davantage une qualification visuelle ou de gameplay. Les sorties et limites
de preuve figurent dans le [contrat Blender](BLENDER_INTEGRATION_SPEC.md).

Le probe de frames de pions évalue un tick CNK de production puis le rendu PBR
réel. Vingt-trois captures 1920x1080 couvrent les onze idles, quatre poses du
chien, six du cheval et deux états du bateau. La mosaïque a été examinée ; ce
contrôle de frames demandées ne constitue pas une animation continue en partie
ni un film de gameplay. Il ne transforme pas les neuf chargements observés au
démarrage en onze pions qualifiés dans une partie réelle.
