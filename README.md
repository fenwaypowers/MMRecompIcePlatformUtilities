# Majora's Mask Recompiled: Ice Platform Utilities

This is a mod for [Zelda64Recompiled](https://github.com/Zelda64Recomp/Zelda64Recomp) that allows ice platforms to be spawned in any area in Majora's Mask. It also allows up to five ice platforms to exist concurrently and lets you adjust their lifetime.

Features:
- Allow ice platforms anywhere in the game (on by default, can be turned off)
- Change how many ice platforms can exist concurrently (default = 3, which is the vanilla value)
- Set how long ice platforms last before melting (default = 15 seconds, which is the vanilla value)
- Specify which areas allow ice platform creation in the config

### Dependencies
- This mod depends on [Global Objects by YAZMT](https://thunderstore.io/c/zelda-64-recompiled/p/YAZMT/Global_Objects/v/0.1.1/). You must first install this before being able to use Ice Platform Utilities.

### Wasn't there another mod that did the same thing?
- I made two ice platform mods, [IcePlatformsAnywhere](https://github.com/fenwaypowers/MMRecompIcePlatformsAnywhere) and [MoreIcePlatforms](https://github.com/fenwaypowers/MMRecompMoreIcePlatforms). This mod combines those two previous mods into one mod.
- I did this because there is a [game-breaking bug in IcePlatformsAnywhere](https://github.com/fenwaypowers/MMRecompIcePlatformsAnywhere/issues/2) and having these mods combined allows the IcePlatformsAnywhere functionality to take advantage of the improved ice platform instance tracking provided by MoreIcePlatforms, which fixes some bugs and enforces dynamic collision limits to ensure the game doesn't crash from having too many ice platforms existing concurrently. More info below.

### Why only up to five concurrent platforms? Why not more?
- Five was decided on as the limit, as it's a noticeable bump from the vanilla limit of three, while not being too taxing on the game's dynamic collision limits.
- Each ice platform takes up 22 dynamic collision polygons and 13 vertices. Usually, the cap for each scene is 544 dynamic collision polygons and 512 vertices.
- Ice platforms are generally not that expensive, but when a new ice platform is spawned and an old one starts melting, the game spends 50 frames (2.5 seconds) melting the platform before its collision is unloaded.
- This means that even if the max concurrent platforms is set as five, ten or even more platforms could exist in the same scene if, for example, 5 were melting and 5 were just spawned.
- This can make the game go over the dynamic collision limit, leading to a crash.
- This mod implements a hard check to make sure the ice platforms do not go over the dynamic collision limits. Meaning that **this mod should theoretically never cause a crash due to spawning one too many platforms**.
- If you try to spawn a platform that *would* have gone over the limit, the arrow will simply hit the water and do nothing like a normal arrow, and the oldest platform (if it was not already melting) would start melting to make room for a new platform. In normal gameplay, the player will not likely encounter this behavior. This behavior is really only noticable if you're spamming ice arrows and are trying to get the game to load as many platforms as it can as fast as possible.

If you run into any errors, please [open an issue on GitHub](https://github.com/fenwaypowers/MMRecompIcePlatformUtilities/issues).

### Writing mods
See [this document](https://hackmd.io/fMDiGEJ9TBSjomuZZOgzNg) for an explanation of the modding framework, including how to write function patches and perform interop between different mods.

### Tools
You'll need to install `clang` and `make` to build this template.
* On Windows, using [chocolatey](https://chocolatey.org/) to install both is recommended. The packages are `llvm` and `make` respectively.
  * The LLVM 19.1.0 [llvm-project](https://github.com/llvm/llvm-project) release binary, which is also what chocolatey provides, does not support MIPS correctly. The solution is to install 18.1.8 instead, which can be done in chocolatey by specifying `--version 18.1.8` or by downloading the 18.1.8 release directly.
* On Linux, these can both be installed using your distro's package manager. You may also need to install your distro's package for the `lld` linker. On Debian/Ubuntu based distros this will be the `lld` package.
* On MacOS, these can both be installed using Homebrew. Apple clang won't work, as you need a mips target for building the mod code.

On Linux and MacOS, you'll need to also ensure that you have the `zip` utility installed.

You'll also need to grab a build of the `RecompModTool` utility from the releases of [N64Recomp](https://github.com/N64Recomp/N64Recomp). You can also build it yourself from that repo if desired.

### Building
* First, run `make` (with an optional job count) to build the mod code itself.
* Next, run the `RecompModTool` utility with `mod.toml` as the first argument and the build dir (`build` in the case of this template) as the second argument.
  * This will produce your mod's `.nrm` file in the build folder.
  * If you're on MacOS, you may need to specify the path to the `clang` and `ld.lld` binaries using the `CC` and `LD` environment variables, respectively.

### Updating the Majora's Mask Decompilation Submodule
Mods can also be made with newer versions of the Majora's Mask decompilation instead of the commit targeted by this repo's submodule.
To update the commit of the decompilation that you're targeting, follow these steps:
* Build the [N64Recomp](https://github.com/N64Recomp/N64Recomp) repo and copy the N64Recomp executable to the root of this repository.
  * Make sure you pass `KEEP_MDEBUG=1` to `make` when building the decomp in order to keep debug information. This must be done from a clean build if you have built the decomp already without `KEEP_MDEBUG=1`.
* Build the version of the Majora's Mask decompilation that you want to update to and copy the resulting .elf file to the root of this repository.
* Update the `mm-decomp` submodule in your clone of this repo to point to the commit you built in the previous step.
* Run `N64Recomp generate_symbols.toml --dump-context`
* Rename `dump.toml` and `data_dump.toml` to `mm.us.rev1.syms.toml` and `mm.us.rev1.datasyms.toml` respectively.
  * Place both files in the `Zelda64RecompSyms` folder.
* Try building.
  * If it succeeds, you're done.
  * If it fails due to a missing header, create an empty header file in the `include/dummy_headers` folder, with the same path.
    * For example, if it complains that `assets/objects/object_cow/object_cow.h` is missing, create an empty `include/dummy_headers/objects/object_cow.h` file.
  * If RecompModTool fails due to a function "being marked as a patch but not existing in the original ROM", it's likely that function you're patching was renamed in the Majora's Mask decompilation.
    * Find the relevant function in the map file for the old decomp commit, then go to that address in the new map file, and update the reference to this function in your code with the new name.
