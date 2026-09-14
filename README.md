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
- The latest version of sourcemod blocks old extensions from loading. This fork allows old extensions to load. 
- You can now do sm exts reload with name or number. Plugins that depend on the extension will automatically reload.
  https://github.com/alliedmodders/sourcemod/pull/2418
- OnLibraryAdded now fires for extension based libraries.
  https://github.com/alliedmodders/sourcemod/pull/2417
- Mysql now sets the connection charset to utf8mb4. This is needed because when the driver autoreconnects, it will revert the charset back to the default.

General
-------
- [SourceMod website](http://www.sourcemod.net): Source Engine scripting and server administration
- [Forum](https://forums.alliedmods.net/forumdisplay.php?f=52): Discussion forum including plugin/extension development
- [General documentation](https://wiki.alliedmods.net/Category:SourceMod_Documentation): Miscellaneous information about SourceMod
- [Stable builds](http://www.sourcemod.net/downloads.php?branch=stable): The latest stable SourceMod releases
- [Dev builds](http://www.sourcemod.net/downloads.php?branch=dev): Builds of recent development versions
 
Development
-----------
- [Issue tracker](https://github.com/alliedmodders/sourcemod/issues): Issues that require back and forth communication
- [Building SourceMod](https://wiki.alliedmods.net/Building_SourceMod): Instructions on how to build SourceMod itself using [AMBuild](https://github.com/alliedmodders/ambuild)
- [SourcePawn scripting](https://wiki.alliedmods.net/Category:SourceMod_Scripting): SourcePawn examples and introduction to the language
- [SourceMod plugin API](https://sm.alliedmods.net/new-api): Online SourceMod plugin API reference generated from the include files
- [SourceMod extension development](https://wiki.alliedmods.net/Category:SourceMod_Development): C++ examples and introduction to various extension interfaces
- [Translation project](https://github.com/orgs/alliedmodders/projects/1): Help [translate SourceMod](https://wiki.alliedmods.net/Translations_(SourceMod_Scripting)) into your language

Contact
-------
- Connect with us on [GameSurge](https://gamesurge.net) IRC in #sourcemod
- Alternatively feel free to join our [Discord](https://discord.gg/HgZctSS) server

License
-------
SourceMod is licensed under the GNU General Public License version 3. Special exceptions are outlined in the LICENSE.txt file inside of the licenses folder.
