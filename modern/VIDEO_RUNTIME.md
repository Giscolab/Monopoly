# Lecture vidéo moderne

Le lecteur utilise deux exécutables externes, `ffmpeg` et `ffprobe`, accessibles
dans le `PATH` du processus Monopoly. Ils ne sont ni téléchargés ni distribués
par le jeu. `DecoderOptions::ffmpeg` et `ffprobe` permettent aux intégrateurs de
fournir des chemins explicites. Les tests acceptent `MONOPOLY_TEST_FFMPEG` et
`MONOPOLY_TEST_FFPROBE` ; ces variables ne configurent pas l'application.

SDL3 lance les processus avec une liste d'arguments, sans shell. Un worker
possède les processus et leurs flux non bloquants. Les files d'images RGBA et
de PCM sont bornées ; une interruption arrête et récupère les processus,
sans attendre de vider les files. Sous Windows, les processus restent cachés
et leur code de sortie est vérifié.

`SequenceRuntimeBridge::sync(SequencePlayback&)` raccorde le lecteur au
sequenceur avant son unique mise à jour par cycle. Les images utilisent le
magasin de bitmaps runtime et le renderer 2D existants. L'audio est converti
en PCM signé 16 bits, stéréo, 48 kHz et transmis à un `SDL_AudioStream`.
Le volume de PCM consommé pilote la progression ; sans audio, le lecteur
utilise l'horloge de séquence. Pause, recherche, boucle et alternatives
conservent ce même propriétaire.

L'introduction reprend le trademark, puis HLogo, ALogo et MIntro. Le choix
Bink/AVI, le rectangle AVI et le doublement centré Bink suivent les callers
legacy. Une touche ou un bouton de souris interrompt l'étape selon le contrat
source. Un fichier ou décodeur indisponible est signalé et rend la main à la
sélection des joueurs.

Les fixtures de qualification sont des films MPEG-4/PCM synthétiques créés
par FFmpeg. Elles peuvent prouver le décodage, les commandes, l'horloge SDL et
le rendu GPU ; elles ne prouvent pas la lecture des films Indeo/Bink retail,
ni le fonctionnement d'une sortie audio physique. Les réglages Indeo de
luminosité, contraste et saturation non nuls ne sont pas portés : le lecteur
les rejette explicitement, et les callers d'ouverture audités utilisent zéro.
