clang src/*.cpp --target=x86_64-windows-msvc -shared -O3 -D_CRT_SECURE_NO_WARNINGS -I7zip -o mkwcat7z-x64.dll -fuse-ld=lld-link -lOleAut32 -lUser32 -ffunction-sections -fdata-sections
clang src/*.cpp --target=i386-windows-msvc -shared -O3 -D_CRT_SECURE_NO_WARNINGS -I7zip -o mkwcat7z-x86.dll -fuse-ld=lld-link -lOleAut32 -lUser32 -ffunction-sections -fdata-sections
