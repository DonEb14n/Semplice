# Release checklist (owner, before each beta build)

Build: `powershell -ExecutionPolicy Bypass -File scripts\package.ps1` (install Inno Setup 6 first to also get the installer; `-Compiler msvc` for a Visual Studio build). Output: `dist\`.

## On the build PC
- [✓] The script finished without warnings and all tests passed.
- [✓] `dist\Semplice\Semplice.exe` starts by double click and shows the window (the PC has Qt installed: also test on the VMs below).
- [✓] Keep `dist\Semplice-symbols\` (needed to read crash reports): copy it away under the version number, the next build replaces it.
- [✓] `resources\license\LICENSE.txt` is the REAL license text (it is still a placeholder until you supply it).
- [✓] The "Official site" address in `src\ui\SettingsVm.cpp` is the real one.

## Clean Windows 10 1809 and Windows 11 (fresh VMs, no Qt, no Visual C++ runtime installed by you)
- [✓] The installer runs without administrator rights, shows the license page, installs to `%LOCALAPPDATA%\Programs\Semplice`.
- [✓] Start menu entry works; the optional desktop icon appears only when ticked.
- [✓] First start: license dialog, then "Select folder"; create a library in an empty folder.
- [✓] Import a folder of music (drag and drop), play it, change volume, next / previous, media keys, taskbar buttons.
- [✓] Add and edit an artist, album and playlist; delete one (it goes to the Recycle Bin).
- [✓] Settings: change language, restart: texts change. Logs folder opens.
- [✓] Installing a second time over the first works (the running program is asked to close).
- [✓] Uninstall removes the program and the shortcuts; the **library folder is untouched** (check it still opens in a fresh install); settings and logs are still there.
- [✓] Windows 11 only: the volume flyout shows title, artist and cover of the playing track.
