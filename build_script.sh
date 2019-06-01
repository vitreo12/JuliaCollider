#!/bin/bash

PRINT_HELP=0

#-c default value
CORES=4

#-e default value
if [[ "$OSTYPE" == "darwin"* ]]; then  
  SC_EXTENSIONS_PATH=${SC_EXTENSIONS_PATH:-~/Library/Application\ Support/SuperCollider/Extensions} 
elif [[ "$OSTYPE" == "linux-gnu" ]]; then 
  SC_EXTENSIONS_PATH=${SC_EXTENSIONS_PATH:-~/.local/share/SuperCollider/Extensions}
fi

#-a default value
BUILD_MARCH=native

#Unpack -c (CORES) -e (EXTENSIONS DIR) -a (BUILD_MARCH) arguments
while getopts "a:c:e:" opt; do
  case $opt in
    a) BUILD_MARCH="$OPTARG"
    ;;
    c) CORES="$OPTARG"
    ;;
    e) SC_EXTENSIONS_PATH="$OPTARG"
    ;;
    \?) PRINT_HELP=1                 #If no recognizable args
    ;;
  esac
done

#Check if user has inputted some error stuff (like "-a -h", "-a -e", etc... which would assign -a the value -h / -e)
if [[ ${BUILD_MARCH:0:1} == '-' ]] || [[ ${CORES:0:1} == '-' ]] || [[ ${SC_EXTENSIONS_PATH:0:1} == '-' ]]; then #Variable starts with "-"
  PRINT_HELP=1
fi

#Check if user is looking for help
if [ $PRINT_HELP == 1 ]; then
  echo
  echo "*************************************************************************************"
  echo "* JuliaCollider: build script help file.                                            *"
  echo "*                                                                                   *"
  echo "* 1) -c argument is the number of cores to build Julia with.                        *"
  echo "*    (default = 4)                                                                  *"
  echo "*                                                                                   *"
  echo "* 2) -e argument is your SuperCollider \"Platform.userExtensionDir\".                 *"
  echo "*    (default MacOS = ~/Library/Application\ Support/SuperCollider/Extensions)      *"
  echo "*    (default Linux = ~/.local/share/SuperCollider/Extensions)                      *"
  echo "*                                                                                   *"
  echo "* 3) -a argument is optional. It allows to make builds for different architectures. *"
  echo "*    (default = native)                                                             *"
  echo "*                                                                                   *"
  echo "*************************************************************************************"
  echo
  exit 1
fi

#If any command fails, exit
set -e

#Path to folder where this bash script is
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null && pwd )"

#Path to julia folder
JULIA_PATH=$DIR/deps/julia

#Path to built julia (inside julia folder)
JULIA_BUILD_PATH=$JULIA_PATH/usr

#SC folder path
SC_PATH=$DIR/deps/supercollider
                                        
#-c argument
re='^[0-9]+$'
if ! [[ $CORES =~ $re ]] ; then
   echo "*** ERROR ***: First argument is not a number. Insert a number for the cores to build julia with." >&2
   exit 1
fi

#-e argument
SC_EXTENSIONS_PATH="${SC_EXTENSIONS_PATH/#\~/$HOME}"   #expand tilde, if there is one
SC_EXTENSIONS_PATH=${SC_EXTENSIONS_PATH%/}             #remove trailing slash, if there is one

if [ ! -d "$SC_EXTENSIONS_PATH" ]; then
    echo "*** ERROR *** '$SC_EXTENSIONS_PATH' is not a valid folder. Insert your SuperCollider \"Platform.userExtensionDir\"."
    exit 1
fi

###############################################################
# To make a generic intel build, use x86-64 as third argument #
###############################################################

#cd to the Julia source in deps
cd $JULIA_PATH

#Delete (if already exists) the Make.user file. A new one will be created with the first printf command later.
rm -f Make.user 

#-a argument
#Note the JULIA_THREADS=0 to build julia without any multithreading support. Refer to NOTES/NOTES.txt for more infos.
#Edit the Make.user accordingly, with the correct build flags for native/generic builds.
printf "JULIA_THREADS=0\n" >> Make.user
printf "MARCH=$BUILD_MARCH\n" >> Make.user
printf "JULIA_CPU_TARGET=$BUILD_MARCH" >> Make.user

#Build julia in deps/julia
make -j $CORES

#cd back to JuliaCollider folder
cd $DIR

#Create build dir, deleting a previous one if it was there.
rm -rf build; mkdir -p build
cd build

#Actually build JuliaCollider
cmake -DSC_PATH=$SC_PATH -DJULIA_BUILD_PATH=$JULIA_BUILD_PATH -DCMAKE_BUILD_TYPE=Release -DBUILD_MARCH=$BUILD_MARCH ..
make 

#Make a JuliaCollider dir, inside of ./build, and put all the built stuff and libs in it
mkdir -p JuliaCollider
mkdir -p JuliaCollider/julia
mkdir -p JuliaCollider/julia/startup

echo "Copying files over..."

rsync -r --links --update "$JULIA_BUILD_PATH/lib" ./JuliaCollider/julia               #copy julia lib from JULIA_PATH to the new Julia folder, inside of a julia/ sub directory, maybe also copy include?
rsync --update "$JULIA_BUILD_PATH/etc/julia/startup.jl" ./JuliaCollider/julia/startup #copy startup.jl
rsync -r -L --update "$JULIA_BUILD_PATH/share/julia/stdlib" ./JuliaCollider/julia     #copy /stdlib. Need to deep copy all the symlinks (-L flag)

#Rename julia/lib into julia/scide_lib. So that (Linux SC problem) SC won't be looking in that folder and attempt to load all the .so files in there.
mv -f ./JuliaCollider/julia/lib ./JuliaCollider/julia/scide_lib

if [[ "$OSTYPE" == "darwin"* ]]; then                     
    cp Julia.scx ./JuliaCollider                                                      #copy compiled Julia.scx
elif [[ "$OSTYPE" == "linux-gnu" ]]; then 
    cp Julia.so ./JuliaCollider                                                       #copy compiled Julia.so file
fi          

rsync --update ../src/Julia.sc ./JuliaCollider                                        #copy .sc class
rsync -r --update ../src/HelpSource ./JuliaCollider                                   #copy .schelp(s)
rsync -r --links --update ../src/Examples ./JuliaCollider                             #copy /Examples 

#Copy the whole build/JuliaCollider folder over to SC's User Extension directory
rsync -r --links --update ./JuliaCollider "$SC_EXTENSIONS_PATH"

echo "Done!"