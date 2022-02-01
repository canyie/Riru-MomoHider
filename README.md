# Riru - MomoHider (aka IsolatedMagiskHider)
## Deprecation Notice
Hi, today is 2022/2/1, happy Chinese new year! 

One year ago, I made this project because my bank app detected the device is rooted and reject to run. Initially I just want to help others so I made this public. But in the past year, things are not going the way I want -- someone just downloads my module, changes the author and claim it's their work; more seriously, my module with unknown changes was built into a cheat program. Finally my kindness ended up being a tool for outlaws. So, I chose to develop a new hide module "Shamiko" with other developers in the LSPosed team. The new module will only support Zygisk, and provides more functionality than MagiskHide. I believe the module will be the complete solution if you want to use MagiskHide on Magisk v24+! But, To keep things from getting out of hand again, the new module will NOT open source, and rejects any modification. 

We expect to officially release Shamiko on February 2nd. [Click here to download Shamiko.](https://lsposed.github.io/)

## Background
Many applications now detect Magisk for security, Magisk provided "Magisk Hide" to hide the modified traces but not completely hidden, magisk still can be detected by [MagiskDetector](https://github.com/vvb2060/MagiskDetector). This module tries to make it more hidden.

Features:
| Config name | Description |
|  ----  | ----  |
| isolated | Apply Magisk Hide for isolated process and app zygotes. This feature is deprecated because it will unmount Magisk modified files for every isolated processes, and the unmounting time cannot be well controlled, which may cause some modules to not work. For almost apps, [Magisk Alpha](https://github.com/vvb2060/magisk/tree/alpha) or the latest Magisk canary + [Riru-Unshare](https://github.com/vvb2060/riru-unshare) is enough.|
| setns | Faster new way to hide Magisk in isolated processes. Requires config "isolated" is enabled. |
| app_zygote_magic | Make a app named "Momo" cannot detect Magisk hide is running. |
| initrc | Hide the modified traces of init.rc |

Note: Since 0.0.3, all features are disabled by default, you need to create a file named `/data/adb/(lite_)modules/riru_momohider/config/<config name>` to enable it.

## Requirement
Rooted Android 7.0+ devices with Magisk and [Riru](https://github.com/RikkaApps/Riru) V25+.

## Test
[Momo](https://www.coolapk.com/apk/io.github.vvb2060.mahoshojo) is the strongest detection app known.

## Troubleshoot
### Find the "config dir"
The really config dir is `$MODULES/riru_momohider/config`. For magisk lite, the `$MODULES` is `/data/adb/lite_modules`; For the original and almost everything, the `$MODULES` is `/data/adb/modules`.

If the module doesn't work, please check the config dir first. You should see a file called magisk_tmp under the config dir.

### Momo shows "environment is broken, service not responding"
Please check your "overlay modules" first. Iterate through $MODULES and check each of its subfolders. For overlay modules, you should see `system/vendor/overlay` or `system/product/overlay` under it.

If you can't find any overlay modules, please go to the "without overlay modules" section.
#### With overlay modules
1. Check your android version. For Android < 10, [Magisk Alpha](https://github.com/vvb2060/magisk/tree/alpha) or the latest Magisk canary + [Riru-Unshare](https://github.com/vvb2060/riru-unshare) is almost enough. After installing the recommended things, you can turn off `isolated` and try again.
2. Disable overlay modules if possible. Or, we can't support this case yet.
3. Try again. If the problem not solved, please try the "without overlay modules" section.

Note: We needs more info to try to support overlay modules, please file a issue with the full log and stacktrace to help me to solve it even if you have solved the problem.

#### Without overlay modules
1. Turn on `setns` and try again.
2. If the problem not solved, please file a bug with your device info and full log.

### Momo still shows "environment is modified"
MomoHider only hide "MagiskHide is enabled", "Found su file", "Found Magisk" and "init.rc is modified" for momo. If you not see these, this is not our problem, please hide it yourself.

But if you see these after enabling these features... please check the following steps:
1. Try run `magiskhide exec which su`, if you see something found, this usually indicates that there are other superuser programs in your system that cause magiskhide not work properly. Please remove other superuser programs.
2. Try installing [MagiskDetector](https://github.com/vvb2060/MagiskDetector), if you see "magiskhide not working", then report to Magisk.
3. Report to me with your device info and logs.

There is our suggestion:
1. Always keep SELinux is enforcing and make sure any sepolicy rules is necessary.
2. Use modern Xposed framework implementations (like [LSPosed](https://github.com/LSPosed/LSPosed) or [Dreamland](https://github.com/canyie/Dreamland) ) and do not use "global mode", only enables Xposed for actually needed apps.

## Build
Run gradle task :module:assembleMagiskRelease from Android Studio or command line, magisk module zip will be saved to module/build/outputs/magisk/.

## Create your own MOD
Welcome to create mod of this project! But, this project is under the GPL V3 License. So please, do NOT make a mod that just changes the author to yourself, and make the source code of your mod is public to your users. Note, just release a patch but not release the complete source code is NOT enough, if you want to ask why, please ask the Free Software Foundation, not me. 
https://www.gnu.org/licenses/gpl-faq.en.html#DistributingSourceIsInconvenient

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