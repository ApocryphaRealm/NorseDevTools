# NorseDevTools

Reload, inspect and test the Norse frameworks without restarting (NORSE: Oath of Blood, UE4SS v4.0.0). Survey:
`RESEARCH-more-frameworks.md` (#10).

- `norse.status` - every Norse mod: loaded or not, its version (NorseManifest), its log and the error / warning lines in it.
- `norse.reload` - every framework re-reads its files (np.reload, nd.reload, nt.reload, nc.reload, pl.reload, ni.reload,
  nrn.movesets, nrn.bindings - run as console commands).
- `norse.help` - every framework's console commands in one list.
- Auto reload (`bAutoReload=1`): each framework's config folder in the game's `ue4ss\Mods` is checked once a second; a
  saved change runs that framework's reload. Edit the files in the game folder while playing - Root Builder copies them
  back into the MO2 mod when the game closes. A removed patch keeps its values until the next map load (NorsePatcher
  writes values; it does not undo them).

0.1.0 tested in game (TEST-RESULT-0.1.0.txt).

## Licence

GPL-3.0-or-later (see LICENSE).
