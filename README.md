# OpenLara
Classic Tomb Raider open-source engine

[WebGL build with demo level](http://xproger.info/projects/OpenLara/)

inspired by OpenTomb project http://opentomb.github.io/

[![License](https://img.shields.io/badge/License-BSD%202--Clause-orange.svg)](https://opensource.org/licenses/BSD-2-Clause)  

## Building

### Linux

Dependencies (Debian-based systems):
```
sudo apt-get install build-essential libgl1-mesa-dev,
```

Building the core:
```
make -C src/platform/libretro 
```

The core will be build to `src/platform/libretro/openlara_libretro.so`

## Links
* [Discord channel](https://discord.gg/EF8JaQB)
* [Tomb Raider Forums thread](http://www.tombraiderforums.com/showthread.php?t=216618)
* [Libretro blog post "New Core: OpenLara"](https://www.libretro.com/index.php/new-core-openlara-windowslinux/)
