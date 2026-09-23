# Respiration — plugin F4SE (Fallout 4, ancienne génération 1.10.163)

Fait respirer le personnage en déplaçant (ou en gonflant) un os du torse en rythme.
Plus l'endurance (points d'action, consommés par le sprint) baisse, plus le rythme
s'accélère et plus le mouvement est ample.

## Prérequis dans le jeu
- F4SE 0.6.23 (ancienne génération)
- Address Library for F4SE Plugins (version ancienne génération)
- Un corps qui a l'os visé pondéré (CBBE, Fusion Girl... comme pour CBP/OCBP)

## Obtenir la DLL (compilation sur GitHub, gratuit)
1. Envoie le contenu de ce dossier dans ton dépôt GitHub existant (remplace les fichiers modifiés).
2. Onglet **Actions** : la compilation démarre au push. Compte 5 à 15 minutes.
3. Une fois vert : ouvre l'exécution → **Artifacts** → télécharge `Respiration`.
4. Copie le dossier `Data` de l'archive dans le dossier `Data` de Fallout 4.
5. Lance le jeu **par F4SE**.

## Trouver le bon os (nouveau)
Le nom d'un os doit être exact, casse comprise — une supposition comme `Belly_02` peut très
bien ne rien donner si le vrai nom est légèrement différent. Pour le trouver sans deviner :

1. Dans `Respiration.ini`, mets `Mode = ListBones`, sauvegarde, lance le jeu (ou attends le
   rechargement à chaud si le jeu tourne déjà).
2. Ouvre `Documents/My Games/Fallout4/F4SE/Respiration.log`. Tu verras une liste de lignes
   `os disponible : "NomExact"` pour chaque os du torse détecté.
3. Repère celui qui correspond au niveau voulu (diaphragme, ventre...), copie son nom exact
   dans `Bones =`.
4. Repasse `Mode` à `Translate` (recommandé) ou `Scale`, sauvegarde.

## Modes
- **Translate** (par défaut) : déplace l'os selon un axe (`TranslateAxis`). Plus naturel
  qu'un gonflement — le ventre "avance" au lieu de se dilater uniformément. Si le mouvement
  part dans le mauvais sens, change `TranslateAxis` entre `x`, `y` et `z`.
- **Scale** : gonfle l'os dans les 3 dimensions à la fois. Plus simple mais donne un effet
  plus artificiel ("ballon").

## Ce que dit le log (avec Debug = 1)
Toutes les 2 s : l'AP lue, le ratio, la période et `nœuds modifiés N`.
`N = 0` veut dire qu'aucun os de `Bones =` n'a été trouvé — vérifie le nom avec `ListBones`.

## Limite connue
En mode `Translate`, le point de départ de chaque os est mémorisé à la première frame où il
est vu. Si tu changes de tenue en jeu, le squelette peut recréer ses nœuds : la respiration
peut "sauter" une fois juste après le changement, puis se stabilise normalement.

## Test hors jeu de la logique
    cd tests && g++ -std=c++20 -I../src test_breath.cpp -o test_breath && ./test_breath
