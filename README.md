# mkwcat 7-zip plugin
A WIP plugin for 7-Zip File Manager that adds supports for some video game archive formats.
Currently supports reading DARCH (`.arc` files from e.g. New Super Mario Bros. Wii), and GFArch (`.gfa` files from
Good-Feel developed games such as Kirby's Epic Yarn).

## Building
You will need LLVM/Clang on the system PATH, or to edit `build.bat` to point to where `clang.exe` is located.
Then run `build.bat` and if successful, the output should take the form of `mkwcat7z-x64.dll` and `mkwcat7z-x86.dll`
in the root of the repo.

## Installing
Locate your install directory of 7-Zip File Manager (the location of `7zFM.exe`, this is usually
`C:\Program Files\7-Zip`) and create a directory named `Formats` if it doesn't already exist, then copy
`mkwcat7z-x64.dll` (or `mkwcat7z-x86.dll`) into the directory.

7-Zip doesn't provide an easy way to tell if the plugin loaded successfully, other than just trying to open
one of the supported files.

## License
All source code in this repository is available under LGPLv2.1. See the [[LICENSE]] file in the root for the full
text of the license.
