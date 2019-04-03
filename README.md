JuliaCollider

BUILD INSTRUCTIONS:
Run the build_script.sh with two arguments:
    1) The number of cores to build Julia with
    2) The path to your SuperCollider's user extenstions directory (run Platform.userExtensionDir in sclang to find yours).

(e.g: 
    cd to source folder
    ./build_script.sh 8 ~/Library/Application\ Support/SuperCollider/Extensions
)