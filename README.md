[![Build Status](https://secure.travis-ci.org/janbar/pvr.mythtv.png?branch=doityourself)](http://travis-ci.org/janbar/pvr.mythtv) [![Coverity Scan Build Status](https://scan.coverity.com/projects/3115/badge.svg)](https://scan.coverity.com/projects/3115)

## Building

### Linux, BSD, OSX

Start by creating a build folder
<pre><code>mkdir -p build
rm -rf build/*
cd build/</code></pre>

To build PVR addon execute the following:
<pre><code>cmake -DCMAKE_BUILD_TYPE=Release -DPACKAGE_ZIP=ON ../
make</code></pre>

Finally generate the individual ZIP archive (which can then be installed manually): 
<pre><code>make package</code></pre>

### Windows

Run cmake GUI and create the VC project in a dedicated folder.
Open the generated solution ```pvr.mythtv.sln``` in Visual C++, then generate the target ```BUILDALL```.
Finally generate the target ```PACKAGE``` to create the individual ZIP archive.

