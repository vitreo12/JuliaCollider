<p align="center">
  <img width="300" height="200" src="src/HelpSource/Overviews/JuliaCollider_logo.png">
</p>

What is JuliaCollider?
======================

**JuliaCollider** is a project that aims to bring the JIT compilation and ease of use of a high performance programming language like [Julia] to the [SuperCollider] real-time audio synthesis server.

[SuperCollider]:https://supercollider.github.io/
[Julia]: https://github.com/JuliaLang/julia

Download binaries
=================

Precompiled binaries for MacOS (10.10+) and Linux (Intel 64 bit) are available under the releases of the repository.

Once you have downloaded the zipped release, extract it and simply put the **JuliaCollider** folder in your SuperCollider extensions folder. You can find yours by evaluating
    
    Platform.userExtensionDir

in SuperCollider. For a system wide installation, put the folder in your

    Platform.systemExtensionDir

instead.

Build from source
=================

Requirements:
-------------

The requirements to build **JuliaCollider** are the same that [Julia] has:

- **[GNU make]**                — building dependencies.
- **[gcc & g++][gcc]** (>= 4.7) or **[Clang][clang]** (>= 3.1, Xcode 4.3.3 on OS X) — compiling and linking C, C++.
- **[libatomic][gcc]**          — provided by **[gcc]** and needed to support atomic operations.
- **[python]** (>=2.7)          — needed to build LLVM.
- **[gfortran]**                — compiling and linking Fortran libraries.
- **[perl]**                    — preprocessing of header files of libraries.
- **[wget]**, **[curl]**, or **[fetch]** (FreeBSD) — to automatically download external libraries.
- **[m4]**                      — needed to build GMP.
- **[awk]**                     — helper tool for Makefiles.
- **[patch]**                   — for modifying source code.
- **[cmake]** (>= 3.4.3)        — needed to build `libgit2`.
- **[pkg-config]**              — needed to build `libgit2` correctly, especially for proxy support.

Check Julia's [README] for additional platform specific build informations. You should ignore the [Architecture Customization] section.

[README]: https://github.com/vitreo12/julia/blob/master/README.md#platform-specific-build-notes

[Architecture Customization]: https://github.com/vitreo12/julia/blob/master/README.md#architecture-customization

[GNU make]:     http://www.gnu.org/software/make
[patch]:        http://www.gnu.org/software/patch
[wget]:         http://www.gnu.org/software/wget
[m4]:           http://www.gnu.org/software/m4
[awk]:          http://www.gnu.org/software/gawk
[gcc]:          http://gcc.gnu.org
[clang]:        http://clang.llvm.org
[python]:       https://www.python.org/
[gfortran]:     https://gcc.gnu.org/fortran/
[curl]:         http://curl.haxx.se
[fetch]:        http://www.freebsd.org/cgi/man.cgi?fetch(1)
[perl]:         http://www.perl.org
[cmake]:        http://www.cmake.org
[pkg-config]:   https://www.freedesktop.org/wiki/Software/pkg-config/

Clone and build:
----------------

Once you have all the requirements in place, building **JuliaCollider** simply requires to:

1) Clone the repository with all initialized submodules:
 
        git clone --recursive https://github.com/vitreo12/JuliaCollider

2) Move inside the cloned repository:
        
        cd JuliaCollider/

3) Run the **build_install.sh** script:
   
    - To see which flags are available for the script, run the help file by evaluating:

          ./build_install.sh -h

        Available flags are:

            [-c] [default = 4] : 

                - Number of cores to build Julia with.

            [-e] [default MacOS = ~/Library/Application\ Support/SuperCollider/Extensions]
                 [default Linux = ~/.local/share/SuperCollider/Extensions] :

                - Your SuperCollider's "Platform.userExtensionDir" or "Platform.systemExtensionDir".
          
            [-a] [OPTIONAL] [default = native] :

                - Build architecture.
    
    - Once you chose the number of cores to build Julia with and found your SuperCollider's Extensions directory, run the script with the correct flags. This is an example of build with 8 cores, with the Extensions directory in "~/Library/Application\ Support/SuperCollider/Extensions":

          ./build_install.sh -c 8 -e ~/Library/Application\ Support/SuperCollider/Extensions


If the **build_install.sh** script gives error, you are probably missing some of Julia's required software. Make sure you have all of them installed. If they are all installed and you're still given an error, run the script again, as Julia's build, sometimes, fails in downloading all the rest of its necessary dependencies.

**NOTE**: As **JuliaCollider** uses a custom fork of [Julia], which needs to be built from the ground up, the first time you'll compile **JuliaCollider** will take some time.

Getting started
===============

**NOTE FOR MACOS USERS:** **JuliaCollider** (either the downloaded binaries or the custom built version) only works with at least MacOS 10.10.

**NOTE FOR LINUX USERS:** In order to use **JuliaCollider** (either the downloaded binaries or the custom built version), you must make sure that your distribution has [pmap] installed.

To get started with **JuliaCollider** simply navigate to the `Browse` section of the `Help Browser`. If **JuliaCollider** has been correctly installed in either the `Platform.userExtensionDir` or the `Platform.systemExtensionDir`, you should be able to see the **JuliaCollider** section:


The **JuliaCollider** `Browse` section contains an overview, a series of 10 tutorials and help files for both the `Julia` and `JuliaDef` new `Classes`.

[pmap]: https://linux.die.net/man/1/pmap