# Semplice - beta guide for testers

Thank you for testing Semplice, an offline music player with a managed library. It has no internet features: nothing is sent anywhere. This is a beta: expect rough edges, and **keep your original music files** (Semplice only copies them into its own library folder and never changes the originals).

## Install
1. Run `Semplice-<version>-setup.exe` (no administrator rights needed). Windows 10 (1809 or newer) and Windows 11, 64-bit.
   *No installer?* Unzip `Semplice-<version>.zip` anywhere and start `Semplice.exe`.
2. Read and accept the license on first start.
3. Choose an EMPTY folder for your library ("Select folder"). Semplice organises everything inside it (`Artists`, `Playlists`, a hidden `.library` folder with the database). Do not move or rename files inside it by hand.
4. Add music: drag a folder or files onto the middle window, or use "+" in the Library panel. Check the proposed names, then press the check mark.

## What to try
Follow `docs/QA_CHECKLIST.md` if you have it (tick the boxes of the milestone you are testing). Otherwise, use it like a normal player for a week and report anything strange: wrong names or covers after import, music that does not start or skips, slow moments, texts that are cut off or untranslated (Russian, Chinese, Japanese are machine drafts), anything that feels confusing.

## Languages
Settings, Language. Needs a restart. The Russian, Chinese and Japanese texts are machine translations: please tell us about wrong or awkward ones.

## If something goes wrong
1. Note what you did just before, and what you expected.
2. Open **Settings, Logs folder, Open** and send us the newest `semplice.log` and, if one exists, the newest `crash-*.txt`. (Folder: `%LOCALAPPDATA%\Semplice\logs`.) The logs contain file names of your music but no personal data and nothing is sent automatically.
3. Do not delete the library folder before telling us: it can usually be repaired (Settings, Rescan library).

## Report template
```
Semplice version: (Settings, or the file properties of Semplice.exe)
Windows version:
What I did:
What I expected:
What happened:
Always / sometimes / once:
Attached: semplice.log, crash-....txt, screenshot
```

## Known issues (maintainers: keep this list current)
| # | Where | What | Workaround | Status |
|---|---|---|---|---|
| 1 | Settings | the "Official site" button opens a placeholder address | - | open (address pending) |
| 2 | Texts | ru / zh_CN / ja are machine drafts | tell us | open |
| 3 | Installer | not yet verified on clean machines | - | open (release checklist) |

## Uninstall
Windows Settings, Apps, Semplice, Uninstall. **Your music library folder is not touched**, and neither are your settings (`%APPDATA%\Semplice`) and logs. To start completely fresh, delete those two folders yourself after uninstalling.
