# M11 - Packaging and beta

Goal: a folder / installer that works on a clean Windows 10 1809+ and Windows 11 PC, and a guide for the owner's three-month test.

## Pieces
| File | Job |
|---|---|
| `scripts/package.ps1` | One command: configure + build Release **with symbols**, run the tests, `windeployqt` into `dist\Semplice-<version>\`, copy notices and licence, split the symbols into `dist\Semplice-<version>-symbols\`, write a `.zip` of the folder (portable copy) and, when Inno Setup is installed, compile the installer. Stops at the first failure. |
| `installer/Semplice.iss` | Inno Setup script: per-user install (no admin), licence page from `resources\license\LICENSE.txt`, Start menu entry, optional desktop icon, optional note / task about Windows long paths, refuses to install over a running Semplice (mutex), uninstaller removes only what the installer put there. |
| `src/app/Semplice.rc.in` | Windows version resource (file version, product name, copyright) filled from the CMake project version, plus the application icon. |
| `resources/Icon.ico` | The owner's icon: the exe icon, the installer icon and the volume-panel icon (shipped next to the program). |
| `docs/BETA_GUIDE.md` | For testers: install, first start, where the logs are and how to send them, what to report, known-issues template, uninstall. |
| `docs/RELEASE_CHECKLIST.md` | For the owner: the clean-VM install / uninstall test (Windows 10 1809 and 11) as ticks. |

## Decisions
* **Per-user install** in `%LOCALAPPDATA%\Programs\Semplice` (no UAC, nothing in Program Files, no registry outside the uninstall entry). Settings stay in `%APPDATA%\Semplice`, logs in `%LOCALAPPDATA%\Semplice\logs`.
* **The uninstaller never touches a library folder** (the installer does not know where it is) and leaves `%APPDATA%\Semplice\settings.ini` and the logs in place; a note on the last page says so. Removing them is the user's choice.
* **Long paths**: the manifest already declares `longPathAware`; Windows also needs `LongPathsEnabled=1` (admin). The installer does not change system settings (CLAUDE.md: no system / security setting changes without asking); the finish page mentions how to enable it and the app reports `E_PATH_TOO_LONG` otherwise.
* **Symbols**: Release is built with `-g`, then `objcopy` keeps the debug information in a `.debug` file and strips the exe / DLLs; the symbol folder is kept by the owner for crash reports (`crash-*.txt` in the logs folder contain addresses).
* **Compiler for the package**: MinGW (the owner's kit); the script takes `-Compiler msvc` as well.
* **Not done here** (not possible on this machine): compiling the installer (Inno Setup is not installed) and testing on clean VMs. Both are on the release checklist.

## Tests
`scripts/package.ps1 -SkipInstaller` run for real: the produced folder starts `Semplice.exe` on its own (no Qt on PATH), opens a library, grabs a screenshot; the dependency list of the folder is compared with `windeployqt` output (nothing missing at start). The `.iss` is checked by reading only.
