#!/bin/bash
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null && pwd )" #path to folder where script is
cd $DIR
mkdir -p build
cd build
cmake -DJULIA_PATH=/Applications/julia-1.0.1-no-threads -DSC_PATH=~/SuperCollider -DCMAKE_BUILD_TYPE=Release ..
make 
cp Julia.scx ~/Library/Application\ Support/SuperCollider/Extensions
cp ../Julia.sc ~/Library/Application\ Support/SuperCollider/Extensions