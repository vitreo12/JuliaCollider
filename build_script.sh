#!/bin/bash

#If any command fails, exit
set -e

#path to folder where script build_install script is
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null && pwd )"

#path to julia folder
JULIA_PATH=$DIR/deps/julia

#path to built julia
JULIA_BUILD_PATH=$JULIA_PATH/usr

#SC folder path
SC_PATH=$DIR/deps/supercollider

#Unpack arguments
CORES=$1                                   #first argument, number of cores to build julia

SC_EXTENSIONS_PATH="${2/#\~/$HOME}"        #second argument, the extensions path for SC. (expand tilde)
SC_EXTENSIONS_PATH=${SC_EXTENSIONS_PATH%/} #remove trailing slash, if there is one

if [ ! -d "$SC_EXTENSIONS_PATH" ]; then
    echo "*** ERROR *** '$SC_EXTENSIONS_PATH' is not a valid folder"
    exit 1
fi
echo "SuperCollider Extensions folder path : $SC_EXTENSIONS_PATH"

#First, build julia.
#MAYBE CAN SET THE -MARCH and -JULIA_CPU_TARGET flags here when it's building instead of Make.user??
cd $JULIA_PATH
make -j $CORES

#cd back to JuliaCollider folder
cd $DIR

#create build dir
mkdir -p build
cd build

#Native build:
cmake -DSC_PATH=$SC_PATH -DJULIA_BUILD_PATH=$JULIA_BUILD_PATH -DCMAKE_BUILD_TYPE=Release -DNATIVE=ON ..
make 

#Generic x86_64 build:
#Requires this to be set in Make.user:
#JULIA_THREADS=0
#MARCH=x86-64
#JULIA_CPU_TARGET=x86-64

#cmake -DSC_PATH=$SC_PATH -DJULIA_BUILD_PATH=$JULIA_BUILD_PATH -DCMAKE_BUILD_TYPE=Release -DNATIVE=OFF .. 
#make

#make a JuliaCollider dir, inside of ./build, and put all the built stuff and libs in it
mkdir -p JuliaCollider
mkdir -p JuliaCollider/julia
mkdir -p JuliaCollider/julia/startup

echo "Copying files over..."

rsync -r --links --update "$JULIA_BUILD_PATH/lib" ./JuliaCollider/julia               #copy julia lib from JULIA_PATH to the new Julia folder, inside of a julia/ sub directory, maybe also copy include?
rsync --update "$JULIA_BUILD_PATH/etc/julia/startup.jl" ./JuliaCollider/julia/startup #copy startup.jl
rsync -r -L --update "$JULIA_BUILD_PATH/share/julia/stdlib" ./JuliaCollider/julia     #copy /stdlib. Need to deep copy all the symlinks (-L flag)

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