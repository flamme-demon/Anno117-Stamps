# Anno117 Stamps

[English](#english) · [Français](#français)

## English

Create, organize and place stamps from the construction menu in
**Anno 117 — Pax Romana**.

Version **0.4.0**, in development. Tested on Linux with Proton.
**The in-game interface is currently in French.** The other language XML files
currently use the same French text. The English mod description is translated,
but the menus and DLL dialogs are not yet localized.

### Features

- Two levels: **Stamps → folders → stamps**, for Rome and Albion.
- Create a stamp in the selected folder using the copy tool.
- Refresh the library during a game session.
- Create, rename and delete folders; rename and delete stamps.
- Change stamp and folder icons.

Stamps keep the game's native FileDB format. Loading reads the original file
without converting it to `.bin`. Explicitly changing a stamp's icon changes
only its root `IconGUID` in the decompressed data. Folder icons are stored
separately in `folder-icons.json`.

The FileDB reader and zlib code are compiled into the DLL. Playing requires
neither Python, .NET nor an external FileDB tool.

### Repository and build

This repository contains the DLL sources, the required MinHook/miniz sources,
and a complete mod with its generated XML and Lua files. It does not include
development tools, tests, session logs, research notes or personal stamps.

To compile the Windows x64 DLL on Linux, install GNU Make and MinGW-w64
(`x86_64-w64-mingw32-gcc`), then run:

```bash
git clone https://github.com/flamme-demon/Anno117-Stamps.git
cd Anno117-Stamps
make -C dll -f Makefile.native-ui
```

Output: `build/native-stamps-ui/dbghelp.dll`. MinHook and miniz are compiled
statically; their sources are included, so no submodule download is needed.
Use `make -B -C dll -f Makefile.native-ui` to force a full rebuild.

### Installation on Linux / Proton

Close Anno completely before replacing files. Adapt these paths to your own
Steam prefix and game installation:

```bash
ANNO_DRIVE_C="/path/to/Steam/steamapps/compatdata/PREFIX_ID/pfx/drive_c"
ANNO_DOCUMENTS="$ANNO_DRIVE_C/users/steamuser/Documents/Anno 117 - Pax Romana"
ANNO_GAME="$ANNO_DRIVE_C/Program Files (x86)/Ubisoft/Ubisoft Game Launcher/games/Anno 117 - Pax Romana"
```

`ANNO_GAME` must contain `Bin/Win64` and `maindata`. The prefix ID and the game
location depend on your setup. Run the game and its mod manager once if the
Documents folders and `mods/profiles/default.txt` do not exist yet.

1. Preserve the original DLL. If `dbghelp_orig.dll` is absent, copy the game's
   **unmodified** `Bin/Win64/dbghelp.dll` to `Bin/Win64/dbghelp_orig.dll`.
   Keep an existing, verified original. A DLL from another mod cannot serve as
   the game's original DLL. Back up any files you are about to replace.

   ```bash
   cp -n "$ANNO_GAME/Bin/Win64/dbghelp.dll" \
     "$ANNO_GAME/Bin/Win64/dbghelp_orig.dll"
   ```

2. Copy the complete mod folder from the repository:

   ```bash
   mkdir -p "$ANNO_DOCUMENTS/mods" "$ANNO_DOCUMENTS/stamps"
   cp -R mods/anno117-stamps-native-ui "$ANNO_DOCUMENTS/mods/"
   ```

3. Install the icon index and initial catalogue in the prefix's `C:` drive.
   `cp -n` preserves an existing catalogue:

   ```bash
   mkdir -p "$ANNO_DRIVE_C/anno117-stamps/native-ui"
   cp mods/anno117-stamps-native-ui/native-ui/icons.tsv \
     "$ANNO_DRIVE_C/anno117-stamps/native-ui/icons.tsv"
   cp -n mods/anno117-stamps-native-ui/native-ui/catalogue.tsv \
     "$ANNO_DRIVE_C/anno117-stamps/native-ui/catalogue.tsv"
   cp build/native-stamps-ui/dbghelp.dll "$ANNO_GAME/Bin/Win64/dbghelp.dll"
   ```

4. Enable `anno117-stamps-native-ui` in the game's mod manager. With the default
   profile, check that `mods/profiles/default.txt` contains the following
   uncommented line, preserving the other mods:

   ```text
   anno117-stamps-native-ui
   ```

5. Allow the native proxy DLL in the Steam launch options for the shortcut
   that starts the game:

   ```text
   WINEDLLOVERRIDES="dbghelp=n,b" %command%
   ```

   If `WINEDLLOVERRIDES` is already set, add `dbghelp=n,b` to its existing value,
   separated by a semicolon. An equivalent `dbghelp = native,builtin` override
   already configured in the Wine prefix is sufficient.

The DLL and the mod work together. The proxy forwards the game's normal
`dbghelp` exports to `dbghelp_orig.dll`, which must remain beside it.
For an existing installation, updating the DLL only replaces `dbghelp.dll`.
Putting it inside the mod folder does not load it with the current setup.

On native Windows, use the player's actual Documents folder, the game's
`Bin\Win64` folder and `C:\anno117-stamps\native-ui` for the same files.
The currently validated installation environment is Linux/Proton.

### In game

Open **Stamps** in the construction menu, open a folder and click a stamp icon.
Click the map to place its preview.

To save a selection, open the destination folder, choose **Créer un stamp ici**
(Create a stamp here), then select the area with the copy tool. The destination
is chosen before selection. New stamps appear during the session.

**Gérer les dossiers** manages folders. **Gérer ce dossier** opens the stamps
and their rename, delete and icon commands. Management pages show six names.
The icon picker currently uses named buttons and an ID entry field.

Original stamps stay under `Documents/Anno 117 - Pax Romana/stamps/`, in their
region and folder. Runtime preferences live in `C:\anno117-stamps\native-ui`:
`slots.tsv` keeps stable button assignments and `folder-icons.json` keeps folder
icons. Keep these files when updating.

### Updates, removal and troubleshooting

For an update, close the game, rebuild the DLL, and replace the mod folder,
`dbghelp.dll` and `icons.tsv`. Keep `dbghelp_orig.dll`, your stamps, the existing
catalogue, `slots.tsv` and `folder-icons.json`.

To remove the mod, close the game, disable its profile entry, and restore the
original `dbghelp.dll` from your backup. The mod folder can then be removed.
Keep your stamps and any metadata you want to reuse later.

If the menu is missing, check the active mod profile and native DLL override.
If the menu appears but does not load stamps, inspect
`drive_c/anno117-pipette.log`, the runtime index files and the stamp paths.

The menu reserves 16 folders per region and 64 stamps per folder. Game updates
may require changes to the DLL's internal hooks or the native popup widget.
The new icon picker rendering still needs confirmation in game after restart;
automated tests simulate engine objects and do not validate visual rendering.

### License

Project code is under the [MIT license](LICENSE). MinHook and miniz retain their
licenses in `external/`. The modified native popup widget included in `mods/`
originates from the game.

## Français

Créer, organiser et poser des stamps directement depuis le menu de construction
d’**Anno 117 — Pax Romana**.

Version **0.4.0**, en développement. Testée sous Linux avec Proton.
**L’interface en jeu est actuellement en français.** Les fichiers XML des autres
langues reprennent les mêmes textes français. La description anglaise du mod
est traduite, mais les menus et dialogues de la DLL ne sont pas encore localisés.

### Fonctionnalités

- Deux niveaux : **Stamps → dossiers → stamps**, pour Rome et Albion.
- Création d’un stamp dans le dossier choisi avec la pipette.
- Actualisation de la bibliothèque pendant la partie.
- Création, renommage et suppression des dossiers ; renommage et suppression des stamps.
- Choix des icônes des stamps et des dossiers.

Les stamps gardent le format FileDB natif du jeu. Le chargement lit le fichier
d’origine sans conversion en `.bin`. Le changement explicite d’icône modifie
uniquement le champ racine `IconGUID` dans les données décompressées.
Les icônes des dossiers sont enregistrées séparément dans `folder-icons.json`.

Le lecteur FileDB et zlib sont intégrés à la DLL. Aucun outil FileDB externe,
Python ou .NET n’est nécessaire pour jouer.

### Dépôt et compilation

Le dépôt contient les sources de la DLL, les sources MinHook/miniz nécessaires,
et un mod complet avec ses fichiers XML et Lua déjà générés. Les outils de
développement, tests, journaux de session, notes de recherche et stamps personnels
restent hors Git.

Pour compiler la DLL Windows x64 sous Linux, installer GNU Make et MinGW-w64
(`x86_64-w64-mingw32-gcc`), puis exécuter :

```bash
git clone https://github.com/flamme-demon/Anno117-Stamps.git
cd Anno117-Stamps
make -C dll -f Makefile.native-ui
```

Résultat : `build/native-stamps-ui/dbghelp.dll`. MinHook et miniz sont compilés
statiquement et leurs sources sont incluses : aucun sous-module à télécharger.
Pour tout recompiler : `make -B -C dll -f Makefile.native-ui`.

### Installation sous Linux / Proton

Fermer complètement Anno avant de remplacer les fichiers. Adapter ces chemins
à son préfixe Steam et au dossier du jeu :

```bash
ANNO_DRIVE_C="/chemin/Steam/steamapps/compatdata/IDENTIFIANT/pfx/drive_c"
ANNO_DOCUMENTS="$ANNO_DRIVE_C/users/steamuser/Documents/Anno 117 - Pax Romana"
ANNO_GAME="$ANNO_DRIVE_C/Program Files (x86)/Ubisoft/Ubisoft Game Launcher/games/Anno 117 - Pax Romana"
```

`ANNO_GAME` doit contenir `Bin/Win64` et `maindata`. L’identifiant du préfixe et
l’emplacement du jeu dépendent de l’installation. Lancer une première fois le
jeu et son gestionnaire de mods si les dossiers Documents et le profil
`mods/profiles/default.txt` n’existent pas encore.

1. Conserver la DLL d’origine. Si `dbghelp_orig.dll` est absent, copier le
   `Bin/Win64/dbghelp.dll` du jeu **non modifié** sous le nom
   `Bin/Win64/dbghelp_orig.dll`. Garder un original déjà présent et vérifié.
   La DLL d’un autre mod ne remplace pas l’original du jeu. Sauvegarder les
   fichiers qui vont être remplacés.

   ```bash
   cp -n "$ANNO_GAME/Bin/Win64/dbghelp.dll" \
     "$ANNO_GAME/Bin/Win64/dbghelp_orig.dll"
   ```

2. Copier le dossier complet du mod depuis le dépôt :

   ```bash
   mkdir -p "$ANNO_DOCUMENTS/mods" "$ANNO_DOCUMENTS/stamps"
   cp -R mods/anno117-stamps-native-ui "$ANNO_DOCUMENTS/mods/"
   ```

3. Installer l’index des icônes et le catalogue initial dans le lecteur `C:` du
   préfixe. `cp -n` conserve un catalogue déjà présent :

   ```bash
   mkdir -p "$ANNO_DRIVE_C/anno117-stamps/native-ui"
   cp mods/anno117-stamps-native-ui/native-ui/icons.tsv \
     "$ANNO_DRIVE_C/anno117-stamps/native-ui/icons.tsv"
   cp -n mods/anno117-stamps-native-ui/native-ui/catalogue.tsv \
     "$ANNO_DRIVE_C/anno117-stamps/native-ui/catalogue.tsv"
   cp build/native-stamps-ui/dbghelp.dll "$ANNO_GAME/Bin/Win64/dbghelp.dll"
   ```

4. Activer `anno117-stamps-native-ui` dans le gestionnaire de mods du jeu. Avec
   le profil par défaut, vérifier que `mods/profiles/default.txt` contient cette
   ligne sans commentaire, en conservant les autres mods :

   ```text
   anno117-stamps-native-ui
   ```

5. Autoriser la DLL native dans les options de lancement Steam du raccourci
   qui démarre le jeu :

   ```text
   WINEDLLOVERRIDES="dbghelp=n,b" %command%
   ```

   Si `WINEDLLOVERRIDES` existe déjà, ajouter `dbghelp=n,b` à sa valeur en
   séparant les entrées par un point-virgule. Un réglage équivalent
   `dbghelp = native,builtin` déjà présent dans le préfixe Wine suffit.

La DLL et le mod fonctionnent ensemble. Le proxy transmet les exports habituels
de `dbghelp` à `dbghelp_orig.dll`, qui doit rester dans le même dossier.
Sur une installation existante, mettre à jour la DLL remplace seulement
`dbghelp.dll`. La placer dans le dossier du mod ne la charge pas avec le montage actuel.

Sous Windows natif, utiliser les Documents réels du joueur, le dossier
`Bin\Win64` du jeu et `C:\anno117-stamps\native-ui` pour les mêmes fichiers.
L’environnement d’installation actuellement validé est Linux/Proton.

### Dans la partie

Ouvrir **Stamps** dans le menu de construction, puis un dossier et cliquer sur
l’icône d’un stamp. Cliquer ensuite sur la carte pour poser son aperçu.

Pour enregistrer une sélection, ouvrir le dossier de destination, choisir
**Créer un stamp ici**, puis sélectionner la zone avec la pipette. Le dossier
est choisi avant la sélection. Le nouveau stamp apparaît pendant la partie.

**Gérer les dossiers** ouvre la gestion des dossiers. **Gérer ce dossier** donne
accès aux stamps et aux commandes de renommage, suppression et changement
d’icône. La gestion affiche six noms par page. Le sélecteur d’icônes utilise
actuellement des boutons nommés et un champ de saisie d’identifiant.

Les stamps d’origine restent dans `Documents/Anno 117 - Pax Romana/stamps/`,
classés par région et dossier. Les préférences sont dans
`C:\anno117-stamps\native-ui` : `slots.tsv` conserve les associations des boutons
et `folder-icons.json` les icônes des dossiers. Les garder lors des mises à jour.

### Mise à jour, désinstallation et dépannage

Pour mettre à jour, fermer le jeu, recompiler la DLL et remplacer le dossier du
mod, `dbghelp.dll` et `icons.tsv`. Conserver `dbghelp_orig.dll`, les stamps,
le catalogue existant, `slots.tsv` et `folder-icons.json`.

Pour désinstaller, fermer le jeu, désactiver le mod dans le profil et restaurer
le `dbghelp.dll` d’origine depuis la sauvegarde. Le dossier du mod peut ensuite
être retiré. Conserver les stamps et les métadonnées que l’on souhaite réutiliser.

Si le menu manque, vérifier le profil actif et le chargement natif de la DLL.
Si le menu apparaît mais ne charge rien, consulter `drive_c/anno117-pipette.log`,
les fichiers d’index et les chemins des stamps.

Le menu réserve 16 dossiers par région et 64 stamps par dossier. Une mise à jour
du jeu peut nécessiter une adaptation des hooks internes ou du widget de grille.
Le rendu du nouveau sélecteur d’icônes reste à confirmer en jeu après redémarrage ;
les tests automatisés simulent les objets du moteur sans valider le rendu visuel.

### Licence

Le code du projet est sous [licence MIT](LICENSE). MinHook et miniz conservent
leurs licences dans `external/`. Le widget natif modifié fourni dans `mods/`
provient du jeu.
