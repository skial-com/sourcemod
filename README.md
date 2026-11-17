SourceMod Skial Version
=========

Why the fork
-------
For many years the sourcemod maintainers have maintained an ok level of backwards compatibility. From my experience and several others, the maintainers are very resistant to adding code from outsiders unless it is a dead simple bug fix but it usually wasn't an issue as you can make your own extension. 

However over the past months, I've found they want to remove features that have been around for over 10 years like extension reloading after having several pull requests fixing this being ignored or skimmed over with 0 thought. The argument seems to be that they don't use it themselves.

As somebody that uses this feature at least once a month for a large number of players to fix extensions or patch exploits without rebooting, removing this was the tipping point.

I will try to maintain compatibility so we can pull fixes and improvements with a simple merge, but it is not going to be possible to do this forever. 

But I don't think this will be a big problem. Sourcemod is a mature project that doesn't really need much more added to it, and the last few breaking changes from Valve, I was able to fix faster myself.

First class support will be provided for TF2 on Linux as that is what we use.

Improvements over the original so far
-------
Extensions
- Old extensions built against the previous SourcePawn ABI still load.
- `sm exts reload` and `sm exts unload` accept a name, file name or number. Reloading waits for the old image to unmap, so C++ globals reinitialize, and plugins that depend on the extension reload automatically.
  https://github.com/alliedmodders/sourcemod/pull/2418
- OnLibraryAdded fires for extension-based libraries.
  https://github.com/alliedmodders/sourcemod/pull/2417
- On x64, `.autoload` files next to the binaries in `extensions/x64/` are picked up.

Core
- `DisableAutoExecConfig` option in core.cfg skips creating and exec'ing per-plugin auto-configs, for servers that share one game folder.
- MAXCLIENTS raised to 256.
- Fixed a double free and handle-table corruption when a handle is cloned while it is being destroyed (reachable through menu cancellation).
- Fixed a crash on shutdown when plugin unloads requested in OnMapEnd were left pending.

Bintools (x64)
- Fixed by-value object classification, argument passing for objects larger than 16 bytes, float returns overflowing the return buffer, 16-byte integer returns, and unaligned float loads. Added an x64 test suite.

MySQL
- Connection charset is utf8mb4 and stays that way after the driver auto-reconnects.
- Builds with the MariaDB connector and runs on OpenSSL 3 hosts.

SourcePawn ([skial-com/sourcepawn](https://github.com/skial-com/sourcepawn), `skial` branch)
- Retags such as `Float:x` and `view_as<float>(x)` now produce float math; they used to silently do integer math (`w *= Float:GetNativeCell(1)` gave 0.0).
- Heap arrays passed to sized parameters of scripted functions (rows of 2D arrays, string literals) are flattened instead of corrupting the heap.
- Copyback into a callback's sized array or string parameter no longer overwrites the rest of the native's buffer.
- Ternaries whose arms differ in width (int/int64, float/double) compile correctly.

SafetyHook ([skial-com/safetyhook](https://github.com/skial-com/safetyhook))
- Pages stay executable while `trap_threads` patches them on Linux; upstream's read-write-only window crashes multithreaded servers.
- Nearby allocation reads `/proc/self/maps` once per search instead of once per candidate.

Upstream SourceMod and SourcePawn are merged regularly. The commit history here and in the submodules has the full details.

General
-------
- [SourceMod website](http://www.sourcemod.net): Source Engine scripting and server administration
- [Forum](https://forums.alliedmods.net/forumdisplay.php?f=52): Discussion forum including plugin/extension development
- [General documentation](https://wiki.alliedmods.net/Category:SourceMod_Documentation): Miscellaneous information about SourceMod
- [Stable builds](http://www.sourcemod.net/downloads.php?branch=stable): The latest stable SourceMod releases
- [Dev builds](http://www.sourcemod.net/downloads.php?branch=dev): Builds of recent development versions
 
License
-------
SourceMod is licensed under the GNU General Public License version 3. Special exceptions are outlined in the LICENSE.txt file inside of the licenses folder.
