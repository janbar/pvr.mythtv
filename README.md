[![License: GPL-2.0-or-later](https://img.shields.io/badge/License-GPL%20v2+-blue.svg)](LICENSE.md)
[![Build and run tests](https://github.com/janbar/pvr.mythtv/actions/workflows/build.yml/badge.svg?branch=Matrix)](https://github.com/janbar/pvr.mythtv/actions/workflows/build.yml)
[![Build Status](https://jenkins.kodi.tv/view/Addons/job/janbar/job/pvr.mythtv/job/Matrix/badge/icon)](https://jenkins.kodi.tv/blue/organizations/jenkins/janbar%2Fpvr.mythtv/branches/)
[![Coverity Scan Build Status](https://scan.coverity.com/projects/3115/badge.svg)](https://scan.coverity.com/projects/3115)

# MythTV PVR
MythTV PVR client addon for [Kodi](http://kodi.tv)

## Build instructions
When building the addon you have to use the correct branch depending on which version of Kodi you're building against.
For example, if you're building the `Matrix` branch of Kodi you should checkout the `Matrix` branch of this repository. Also make sure you follow this README from the branch in question.

### Linux

    git clone --branch Matrix --depth=1 https://github.com/xbmc/xbmc.git
    git clone --branch Matrix https://github.com/janbar/pvr.mythtv.git
    cd pvr.mythtv && mkdir build && cd build
    cmake -DADDONS_TO_BUILD=pvr.mythtv -DADDON_SRC_PREFIX=../.. -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_INSTALL_PREFIX=../../xbmc/addons -DPACKAGE_ZIP=ON ../../xbmc/cmake/addons
    make

The addon files will be placed in `../../xbmc/kodi-build/addons` so if you build Kodi from source and run it directly
the addon will be available as a system addon.

##### Useful links

* [Kodi's PVR user support](http://forum.kodi.tv/forumdisplay.php?fid=170)
* [Kodi's PVR development support](http://forum.kodi.tv/forumdisplay.php?fid=136)
