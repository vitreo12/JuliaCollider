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

The requirements for JuliaCollider are the same that [Julia] has. Check Julia's [README] for required libraries and tools that you might need to install on your machine in order to correctly build Julia and, thus, **JuliaCollider**.

[README]: https://github.com/vitreo12/julia/blob/master/README.md#required-build-tools-and-external-libraries

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

**FOR LINUX USERS**: In order to use **JuliaCollider**, you must make sure that your distribution has [pmap] installed.

[pmap]: https://linux.die.net/man/1/pmap