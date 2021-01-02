# Riru-IsolatedMagiskHider
## Background
Many applications now detect Magisk for security, Magisk provided "Magisk Hide" to prevent detection, but isolated processes will be skipped. This module tries to enable the feature for these processes.

## Requirement
Rooted Android 7.0+ devices with Magisk and [Riru](https://github.com/RikkaApps/Riru).

## Build
Run gradle task :module:assembleMagiskRelease from Android Studio or command line, magisk module zip will be saved to module/build/outputs/magisk/.

## Known Issues
- Since Android 11, Google has removed /sbin and Magisk will generate random directory to replace it. Now this module hardcoded this path in code, so it may not work in Android 11.

- Some Magisk created mountpoint cannot be unmounted because of SELinux, errno=13 (Permission denied).

## Discussion
- [QQ Group: 949888394](https://shang.qq.com/wpa/qunwpa?idkey=25549719b948d2aaeb9e579955e39d71768111844b370fcb824d43b9b20e1c04)
- [Telegram Group: @DreamlandFramework](https://t.me/DreamlandFramework)

## Credits
- [Magisk](https://github.com/topjohnwu/Magisk)
- [Riru](https://github.com/RikkaApps/Riru)

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