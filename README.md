JuliaCollider
=============

What is JuliaCollider?
----------------------

**JuliaCollider** is a project that aims to bring the JIT compilation and ease of use of a high performance programming language like [Julia] to the [SuperCollider] audio synthesis language. 

[SuperCollider]:https://supercollider.github.io/

Download binaries
-----------------

Precompiled binaries for MacOS (10.10+) and Linux (Intel 64 bit) are available under the releases of the repository.

Once you have downloaded the zipped release, extract it and simply put the **JuliaCollider** folder in your SuperCollider extensions folder. You can find yours by evaluating
    
    Platform.userExtensionDir

in SuperCollider.

Build from source
-----------------

`Requirements:`

The requirements for JuliaCollider are the same of [Julia]. Check Julia's README for required software that you might need to install on your machine in order to correctly build JuliaCollider.

[Julia]: https://github.com/JuliaLang/julia

`Clone and build:`

    1) git clone --recursive https://github.com/vitreo12/JuliaCollider
    2) cd to git folder
    3) run ./build_install.sh script (check its help file for flags)

If the build_install.sh script gives error, you are probably missing some of Julia's dependencies. Make sure you have all of them installed. If they are all installed and you're still given an error, run the script again, as Julia's build, sometimes, fails in downloading all its other necessary dependencies.

**NOTE**: As **JuliaCollider** uses a custom fork of [Julia], which needs to be built from the ground up, the first time you will be compiling  **JuliaCollider** will take some time.

Getting started
---------------

**FOR LINUX USERS**: In order to use **JuliaCollider**, you must make sure that your distribution has [pmap] installed.

[pmap]: https://linux.die.net/man/1/pmap