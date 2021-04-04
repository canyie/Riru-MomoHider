# Riru-MomoHider (aka IsolatedMagiskHider)
## Background
Many applications now detect Magisk for security, Magisk provided "Magisk Hide" to hide the modified traces but not completely hidden, magisk still can be detected by [MagiskDetector](https://github.com/vvb2060/MagiskDetector). This module tries to make it more hidden.

Features:
| Config name | Description |
|  ----  | ----  |
| isolated | Apply Magisk Hide for isolated process and app zygotes. This feature is deprecated because it will unmount Magisk modified files for every isolated processes, and the unmounting time cannot be well controlled, which may cause some modules to not work. [Magisk Alpha](https://github.com/vvb2060/magisk/tree/alpha) or the latest Magisk canary + [Riru-Unshare](https://github.com/vvb2060/riru-unshare) is recommended.|
| setns | Faster new way to hide Magisk in isolated processes. Requires config "isolated" is enabled. |
| app_zygote_magic | Make a app named "Momo" cannot detect Magisk hide is running. |
| initrc | Hide the modified traces of init.rc |

Note: Since 0.0.3, all features are disabled by default, you need to create a file under /data/adb/momohider/ to enable it.

## Requirement
Rooted Android 7.0+ devices with Magisk and [Riru](https://github.com/RikkaApps/Riru).

## Build
Run gradle task :module:assembleMagiskRelease from Android Studio or command line, magisk module zip will be saved to module/build/outputs/magisk/.

## Discussion
- [QQ Group: 949888394](https://shang.qq.com/wpa/qunwpa?idkey=25549719b948d2aaeb9e579955e39d71768111844b370fcb824d43b9b20e1c04)
- [Telegram Group: @DreamlandFramework](https://t.me/DreamlandFramework)

## Credits
- [Magisk](https://github.com/topjohnwu/Magisk)
- [Riru](https://github.com/RikkaApps/Riru)
- [xHook](https://github.com/iqiyi/xHook)

## License
The project uses Magisk's source code, so its license follows Magisk's license.
```
Magisk, including all git submodules are free software:
you can redistribute it and/or modify it under the terms of the
GNU General Public License as published by the Free Software Foundation,
either version 3 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
```