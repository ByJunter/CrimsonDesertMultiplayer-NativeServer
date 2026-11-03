# Ultimate ASI Loader (not included)

The mod is loaded by **Ultimate ASI Loader v9.7.4** by ThirteenAG
(https://github.com/ThirteenAG/Ultimate-ASI-Loader, MIT license). Its binary is not
redistributed in this repository. To install it:

1. Download `Ultimate-ASI-Loader-NoPDB_x64.zip` from the v9.7.4 release:
   https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/tag/v9.7.4
   (SHA-256 of the zip: `e5860e7d9a1805267535b65749575b5e406cc6ea3325c7392189c578815045d1`).
2. Extract `dinput8.dll` to `third_party/ultimate-asi-loader/v9.7.4/dinput8.dll`
   (this path is ignored by git).
3. `scripts/deploy.ps1` installs it as `bin64\winmm.dll`, because CrimsonDesert.exe
   imports WINMM.dll. `scripts/package_kit.ps1` and `scripts/package_coop.ps1` copy it too.
