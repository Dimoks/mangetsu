# Mangetsu 満月

A collection of tools for reading/writing the data formats packaged in the
Nintendo Switch version of the Tsukihime Remake.

## Building on Debian / Ubuntu / APT-based Linux

### Dependencies

To build all tools, you will need at least the following

```bash
sudo apt install -y build-essential cmake zlib1g-dev libssl-dev
```
If you also wish to build the graphical tools, you will need some additional dependencies
```bash
sudo apt install -y libopengl-dev libglfw3-dev
```
**Note:** At least GCC 8<sup>[1]</sup> and cmake 3.13<sup>[2]</sup> are required. If you're on an older system,
newer GCCs are available [via the Ubuntu test toolchain PPA][3], and newer cmakes are available [via the official kitware PPA][4], though neither of these have been tested. Ubuntu 20.04 LTS and Debian 11 Buster both come with compatible build tools out of the box, as do newer releases. 

[1]: https://stackoverflow.com/a/39231488/299981 "Stack Overflow note on changes in libstdc namespace"
[2]: https://cmake.org/cmake/help/latest/command/add_link_options.html "CMake Reference for add_link_options() noting when it was introduced"
[3]: https://launchpad.net/~ubuntu-toolchain-r/+archive/ubuntu/test
[4]: https://apt.kitware.com/

### Build commands

```bash
git clone git@github.com:Dimoks/mangetsu.git
cd mangetsu
mkdir build && cd build
cmake ..                # No UI programs
cmake -DBUILD_GUI=On .. # With UI programs
make
```

### Installation (root)

For convenience, the CMake file also generated installation logic which can install the Mangetsu apps to `/usr/local/bin`. Just run:
```bash
sudo make install
```
As implied by the use of sudo, the destination folder requires administrator permissions in order to be written to.

## Building on Windows

### Dependencies

Mangetsu can be can also be built natively for Windows using the MINGW64 or UCRT64 environments of [the MSYS2 build platform](https://www.msys2.org/). If you aren't already set up, consider using UCRT64, which links against Microsoft's new, more consistent, and more standards conformant Universal C Runtime. The UCRT ships with Windows 10 and above, but if you're on an earlier version, installation packages are available [here](https://www.microsoft.com/en-us/download/details.aspx?id=48234). 

To get started setting up the actual build environment, download and run the MSYS2 installer from [their site](https://www.msys2.org/). After your initial setup, you will also need to install the following dependencies to build mangetsu:
```bash
# ucrt64
pacman -S git mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-gcc-libs mingw-w64-ucrt-x86_64-headers-git mingw-w64-ucrt-x86_64-winpthreads-git mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-pkgconf mingw-w64-ucrt-x86_64-openssl mingw-w64-ucrt-x86_64-zlib
# mingw64
pacman -S git mingw-w64-x86_64-gcc mingw-w64-x86_64-gcc-libs  mingw-w64-x86_64-headers-git mingw-w64-x86_64-winpthreads-git mingw-w64-x86_64-cmake mingw-w64-x86_64-pkgconf mingw-w64-x86_64-openssl  mingw-w64-x86_64-zlib
```
If you wish to build the graphical tools, you'll also need:
```bash
# ucrt64
pacman -S mingw-w64-ucrt-x86_64-glfw
# mingw64
pacman -S mingw-w64-x86_64-glfw
```
All told these packages and their dependencies will take up a bit over 1GiB of space on a clean install. Please note that during the build process, one additional dependency, [a small support library](https://github.com/bilditup1/mman-win32) that translates memory mapping API syntax, is downloaded, built, and installed within the confines of your MSYS2 environment; you are encouraged to investigate CMakeLists.txt as well as the library itself for more details. 

### Build commands

CMake on the MSYS platform specifies the Ninja build system be installed and adds it during the installation process above. You can manually install GNU make later and use that instead, but it's much slower, especially when rebuilding. The build commands are more or less the same as on \*nix, with only the final line differing to account for the use of ninja:
```bash
git clone git@github.com:Dimoks/mangetsu.git
cd mangetsu
mkdir build && cd build
cmake ..                # No UI programs
cmake -DBUILD_GUI=On .. # With UI programs
ninja
```

### Installation (all users)

For convenience, the CMake file also generated installation logic which can install Mangetsu to `%LOCALAPPDATA%/mangetsu` and then adds that folder to your path. Administrator permissions are not required! Just run:
```bash
ninja install
```
If you get a warning about the Powershell script that adds your install directory to your path being unsigned when you try this, you'll have to open a Powershell window (again, as a regular user, not as an administrator) and allow for unsigned local scripts:
```powershell
Set-ExecutionPolicy RemoteSigned -Scope CurrentUser
```
and then run `ninja install` once more.
Afterwards, you can make the script execution policy more restrictive again if you like:
```powershell
Set-ExecutionPolicy AllSigned -Scope CurrentUser
``` 
That's it! You're done! The Mangetsu apps should now be accessible from any directory on your PC using the command line. 

## Tool Overview

### MRG files

The MRG files operate on the mrg/hed/nam file triplets.
MRG files are archive formats, and can be unzipped / zipped using
the tools with the `mrg_` prefix. Nam files are optional - when found, they
will be used.

- `mrg_info`: Print the file list contained in a mrg. May optionally output in
  machine readable format
- `mrg_extract`: Unpack all files in a mrg. If a nam file is present, filenames
  will include the nam entry.
- `mrg_pack`: Construct a new mrg/hed/nam from individual files. Files will be
  packed in the order they are specified.
- `mrg_replace`: Given a base mrg/hed, create a new mrg/hed with archive
  entries at certain offsets in the original file replaced by new files.
- `nam_read`: Print the names in a nam file.

### MZP files

Like MRG files, MZPs are archive formats that contain multiple sections. Unlike
MRG, these files are self-describing and do not have a separate HED file.

- `mzp_info`: List information about existing mzp file
- `mzp_compress`: Combine multiple files into an mzp archive
- `mzp_extract`: Extract all sections from an mzp archive

### MZX

MZX is a basic LZ-adjacent compression format. It is purely a compression
format, with no archive capabilities.

- `mzx_decompress`: Decompress a MZX-compressed file
- `mzx_compress`: Compress a raw file using MZX compression. NOTE: This program
currently does not attempt to actually do _useful_ compression - it will
generate a valid MZP output, but the output _will_ be larger than the input
file.

### NXX

NXGX / NXCX files are GZIP / LZ compressed data formats with a small header.
Note that NXCX support is not tested due to lack of sample files.

- `nxx_decompress`: Given a file in either NXGZ or NXCX format, uncompress the
  data to a new file.
- `nxgx_compress`: Given a raw file, compress in NXGZ format.

### GUI Programs

If GUI support is enabled, the `data_explorer` file will be built. This UI
allows reinterpreting a file as any of the formats above on the fly, as well as
recursively extracting and displaying sub-archives of those files. It also
features a hex view and other tools designed to make analysis of raw formats
easier.
