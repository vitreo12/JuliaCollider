#!/bin/bash
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null && pwd )" #path to folder where script build_install script is
cd $DIR
mkdir -p build
cd build

#setup env variables
#MAC: ./build_install_native.sh '~/Desktop/IP/JuliaCollider/vitreo12-julia/julia-native' '~/SuperCollider' '~/Library/Application Support/SuperCollider/Extensions'
#LINUX: ./build_install_native.sh ~/Sources/julia-native/usr ~/Sources/SuperCollider-3.10beta2 ~/.local/share/SuperCollider/Extensions
INPUT_JULIA_PATH="${1/#\~/$HOME}"               #expand tilde on first argument
JULIA_PATH=${INPUT_JULIA_PATH%/}                #remove trailing slash, if there is one

if [ ! -d "$JULIA_PATH" ]; then
    echo "*** ERROR *** '$JULIA_PATH' is not a valid folder"
    exit 1
fi

JULIA_PATH=$(find "$JULIA_PATH" -maxdepth 2 -type d -name "lib") #find lib folder, in case the user inserts the path to julia source and not to built stuff in /usr inside the source directory

#if JULIA_PATH is empty or the last three characters are not "lib", it means the find command didn't find the folder
if [ -z ${JULIA_PATH} ] || [ ${JULIA_PATH:(-3)} != "lib" ]; then
    echo "*** ERROR *** Couldn't find path to Julia at: '$INPUT_JULIA_PATH'"
    exit 1
fi
#else, just stip "/lib out"
JULIA_PATH=${JULIA_PATH::${#JULIA_PATH}-4}                       #remove "/lib"

SC_PATH="${2/#\~/$HOME}"                  #expand tilde on second argument
SC_PATH=${SC_PATH%/}                      #remove trailing slash, if there is one

if [ ! -d "$SC_PATH" ]; then
    echo "*** ERROR *** '$SC_PATH' is not a valid folder"
    exit 1
fi

SC_EXTENSIONS_PATH="${3/#\~/$HOME}"        #expand tilde on third argument
SC_EXTENSIONS_PATH=${SC_EXTENSIONS_PATH%/} #remove trailing slash, if there is one

if [ ! -d "$SC_EXTENSIONS_PATH" ]; then
    echo "*** ERROR *** '$SC_EXTENSIONS_PATH' is not a valid folder"
    exit 1
fi

echo "Julia path : $JULIA_PATH"
echo "SuperCollider source path : $SC_PATH"
echo "SuperCollider Extensions folder path : $SC_EXTENSIONS_PATH"

#build the files
cmake -DJULIA_PATH=$JULIA_PATH -DSC_PATH=$SC_PATH -DCMAKE_BUILD_TYPE=Release -DNATIVE=ON ..
make 

#make a Julia dir, inside of ./build, and put all the built stuff and libs in it
mkdir -p JuliaCollider
mkdir -p JuliaCollider/julia
mkdir -p JuliaCollider/julia/startup

echo "Copying files over..."

rsync -r --links --update "$JULIA_PATH/lib" ./JuliaCollider/julia               #copy julia lib from JULIA_PATH to the new Julia folder, inside of a julia/ sub directory, maybe also copy include?
rsync --update "$JULIA_PATH/etc/julia/startup.jl" ./JuliaCollider/julia/startup #copy startup.jl
rsync -r -L --update "$JULIA_PATH/share/julia/stdlib" ./JuliaCollider/julia     #copy /stdlib. Need to deep copy all the symlinks (-L flag)

if [[ "$OSTYPE" == "darwin"* ]]; then                     
    cp Julia.scx ./JuliaCollider                                                #copy compiled Julia.scx
elif [[ "$OSTYPE" == "linux-gnu" ]]; then 
    cp Julia.so ./JuliaCollider                                                 #copy compiled Julia.so file
fi          

rsync --update ../src/Julia.sc ./JuliaCollider                                  #copy .sc class
rsync -r --update ../src/HelpSource ./JuliaCollider                             #copy .schelp(s)
rsync -r --links --update ../src/Examples ./JuliaCollider                       #copy /Examples 

#copy stuff over to SC's User Extension directory
rsync -r --links --update ./JuliaCollider "$SC_EXTENSIONS_PATH"

echo "Done!"