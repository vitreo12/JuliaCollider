#!/bin/bash

#Check if user is looking for help
if [ "$1" == "-h" ]; then
  echo "******************************************************************"
  echo 
  echo " First argument is the number of cores to build Julia with."
  echo " Second argument is your SuperCollider Platform.userExtensionDir."
  echo " e.g. ./build_script.sh 8 ~/Library/Application\ Support/SuperCollider/Extensions "
  echo
  echo "******************************************************************"
  exit 1
fi

if [ "$1" == "--help" ]; then
  echo "******************************************************************"
  echo 
  echo " First argument is the number of cores to build Julia with."
  echo " Second argument is your SuperCollider Platform.userExtensionDir."
  echo " e.g. ./build_script.sh 8 ~/Library/Application\ Support/SuperCollider/Extensions "
  echo
  echo "******************************************************************"
  exit 1
fi

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

re='^[0-9]+$'
if ! [[ $CORES =~ $re ]] ; then
   echo "*** ERROR ***: First argument is not a number. Insert a number for the cores to build julia with." >&2
   exit 1
fi

SC_EXTENSIONS_PATH="${2/#\~/$HOME}"        #second argument, the extensions path for SC. (expand tilde)
SC_EXTENSIONS_PATH=${SC_EXTENSIONS_PATH%/} #remove trailing slash, if there is one

if [ ! -d "$SC_EXTENSIONS_PATH" ]; then
    echo "*** ERROR *** '$SC_EXTENSIONS_PATH' is not a valid folder. Insert your SuperCollider user Extensions folder"
    exit 1
fi
#echo "SuperCollider Extensions folder path : $SC_EXTENSIONS_PATH"

#First, build julia.
#MAYBE CAN SET THE -MARCH and -JULIA_CPU_TARGET flags here when it's building instead of Make.user??
cd $JULIA_PATH
make -j $CORES

#cd back to JuliaCollider folder
cd $DIR

#create build dir, deleting a previous one if it was there.
rm -rf build; mkdir -p build
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

#rename julia/lib into julia/scide_lib. So that (Linux SC problem) SC won't be looking in that folder and attempt to load all the .so files in there.
mv -f ./JuliaCollider/julia/lib ./JuliaCollider/julia/scide_lib

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