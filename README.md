# MIDIRenderer

A MIDI to RPGMV-compatible looping OGG converter

RPG Maker 2000 through VX Ace are capable of using MIDI files for music. However, RPG Maker MV is not. This command-line tool renders MIDI files as OGG Vorbis files with the looping metadata tags used by RPG Maker MV.

## Usage

From `midirenderer --help`:

```
midirenderer [OPTION...] <files> -f <soundfont>

      --help                    Show this help document
  -f, --soundfont soundfont.sf2
                                (Required) The path to the soundfont to use
  -d, --destination output      The folder to place the rendered files in
      --loop                    Render the audio looped to help make the loop
                                more seamless at the cost of filesize
      --end-on-division 4       Align the end of the song to a note division
                                up to a 64th note
```

### Usage tips

MIDIRenderer does not supply its own SoundFont. SF2 soundfonts are supported and, depending on the version of FluidSynth used to build the application, SF3 soundfonts are also supported. The official build should support SF3 soundfonts. Due to limitations in the current versions of FluidSynth and libinstpatch, DLS files are not supported as they fail to render even remotely accurately. This means that the recommended way to use MIDIRenderer to render MIDI files to mimic the sound of Windows' Microsoft GS Wavetable Synth is to use Windows' built-in gm.dls soundfont converted to SF2. The conversion is not 100% accurate but is close to the original with few obvious discrepancies.

The `--loop` option is used to loop playback once in-file. This means that when the end of the song is reached, playback repeats once from the loop point, if present, or the start of the song. Using this switch is recommended to prevent an audible hard loop point when the rendered file loops.

The `--end-on-division` option is used to end the song on a beat of the given beat division - for example, `--end-on-division 4` aligns the end of the song to the next quarter note. This is useful because the last MIDI message in a song often comes before the end of the last beat. While the effects are usually subtle, songs whose last notes end before the logical end of the song will loop too early when looping without proper use of this option.

## Information on looping

MIDIRenderer uses RPG Maker's loop point marker in MIDI files and writes the LOOPSTART and LOOPLENGTH metadata tags in the Ogg Vorbis file output. If you want to render your own MIDI songs with a defined loop point, MIDIRenderer interprets a MIDI custom controller event sent to controller 111 as the loop start point (for more information, see [this article](https://rpgmaker.net/articles/104/) ([Internet Archive link](http://web.archive.org/web/20201111230241/https://rpgmaker.net/articles/104/))). The loop end point is always the end of the song.

## Building

This project uses CMake to generate a build system for your platform. Its dependencies are as follows:
- [ogg](https://github.com/xiph/Ogg)
- [vorbis](https://github.com/xiph/Vorbis)
- [FluidSynth](https://github.com/FluidSynth/fluidsynth)

The following tools are also required:
- A C++ compiler toolchain with C++17 support
- CMake >= 3.16

The directory `build/` is ignored in the project .gitignore for your convenience, should you choose to use it as a CMake build folder.

### Building on Windows
#### Using vcpkg

All of MIDIRenderer's dependencies are available through vcpkg, which makes it the most convenient way to buid MIDIRenderer. The dependences may be installed with the following commands, depending on your target platform:

32-bit target: `<path to vcpkg.exe> --triplet=x86-windows install libogg libvorbis FluidSynth`

64-bit target: `<path to vcpkg.exe> --triplet=x64-windows install libogg libvorbis FluidSynth`

CMake handles the rest of the build process:

32-bit target: `cmake -A Win32 <path to project root> -DCMAKE_TOOLCHAIN_FILE="<path to vcpkg root>\scripts\buildsystems\vcpkg.cmake"`

64-bit target: `cmake -A x64 <path to project root> -DCMAKE_TOOLCHAIN_FILE="<path to vcpkg root>\scripts\buildsystems\vcpkg.cmake"`

To compile, run `cmake --build . --config <Release|Debug|MinSizeRel|RelWithDebInfo>`

#### Without vcpkg

If you cannot or do not wish to use vcpkg to build MIDIRenderer, the steps are similar, though you will likely have to build Ogg and Vorbis yourself. You may set `CMAKE_PREFIX_PATH` to help CMake locate the project dependencies or set the following variables manually to define the locations of their respective libraries:
- `Vorbis_ROOT`
- `OGG_ROOT`
- `FLUIDSYNTH_ROOT`

### Building on Linux

The build process for your distribution should be straightforward, especially if your distribution provides packages for MIDIRenderer's dependencies. If some packages are not provided or you wish to use custom builds of the project dependencies, you may use the same variables in the "Without vcpkg" section to define their locations.

`cmake <path to project root> && make` should be all you need to do if you have the prerequisites installed on your system.

### Packaging

The project supports packaging itself for distribution using `cmake --build . --target PACKAGE` on Windows. This generates a standalone distribution in your CMake working directory called `midirenderer-<version>-<target>.zip`. On Windows, MIDIRenderer's DLL dependency tree is automatically copied into the build folder. The packaging target uses CPack, so you may change the packaging parameters according to the [CPack documentation](https://cmake.org/cmake/help/latest/module/CPack.html). MIDIRenderer has not necessarily been configured for proper installer creation; your mileage may vary.

The same CPack packager may be used with `make package`, but it is recommended you use `make install` or `checkinstall` instead.