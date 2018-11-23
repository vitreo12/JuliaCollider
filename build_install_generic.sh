#!/bin/bash
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null && pwd )" #path to folder where script build_install script is
cd $DIR
mkdir -p build
cd build

#setup env variables
#./build_install_generic.sh '~/Desktop/IP/JuliaCollider/vitreo12-julia/julia-generic' '~/SuperCollider' '~/Library/Application Support/SuperCollider/Extensions'
JULIA_PATH="${1/#\~/$HOME}"               #expand tilde on first
JULIA_PATH=${JULIA_PATH%/}                #remove trailing slash, if there is one

if [ ! -d "$JULIA_PATH" ]; then
    echo "Couldn't find path to Julia at: $JULIA_PATH"
    exit 1
fi

JULIA_PATH=$(find "$JULIA_PATH" -maxdepth 2 -type d -name "lib") #find lib folder, in case use puts the path to julia source and not to built stuff in /usr inside the source directory
JULIA_PATH=${JULIA_PATH::${#JULIA_PATH}-4}                       #remove "/lib"

SC_PATH="${2/#\~/$HOME}"                  #expand tilde on second argument
SC_PATH=${SC_PATH%/}                      #remove trailing slash, if there is one

if [ ! -d "$SC_PATH" ]; then
    echo "Couldn't find path to SuperCollider source at: $SC_PATH"
    exit 1
fi

SC_EXTENSIONS_PATH="${3/#\~/$HOME}"        #expand tilde on third argument
SC_EXTENSIONS_PATH=${SC_EXTENSIONS_PATH%/} #remove trailing slash, if there is one

if [ ! -d "$SC_EXTENSIONS_PATH" ]; then
    echo "Couldn't find path to SuperCollider Extensions at: $SC_EXTENSIONS_PATH"
    exit 1
fi

echo "Julia path : $JULIA_PATH"
echo "SuperCollider source path : $SC_PATH"
echo "SuperCollider Extensions folder path : $SC_EXTENSIONS_PATH"

#build the files
cmake -DJULIA_PATH=$JULIA_PATH -DSC_PATH=$SC_PATH -DCMAKE_BUILD_TYPE=Release -DNATIVE=OFF ..
make 

#make a Julia dir, inside of ./build, and put all the built stuff with includes and libs
mkdir -p Julia
echo "Copying files over..."
rsync -r --links --update "$JULIA_PATH/lib" ./Julia/julia #copy julia lib from JULIA_PATH to the new Julia folder, inside of a julia/ sub directory, maybe also copy include?
rsync -r --links --update ../src/JuliaDSP ./Julia/julia   #copy JuliaDSP/.jl stuff to the same julia/ subdirectory of Julia
if [[ "$OSTYPE" == "darwin"* ]]; then                     #copy compiled .scx/.so file
    cp Julia.scx ./Julia     
elif [[ "$OSTYPE" == "linux-gnu" ]]; then  
    cp Julia.so ./Julia    
fi          
cp ../src/Julia.sc ./Julia                                #copy .sc class

#copy stuff over to SC's User Extension directory
rsync -r --links --update ./Julia "$SC_EXTENSIONS_PATH"