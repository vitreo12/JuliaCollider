#!/bin/bash
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null && pwd )" #path to folder where script is
cd $DIR
mkdir -p build
cd build
cmake -DSC_PATH=~/SuperCollider -DCMAKE_BUILD_TYPE=Release ..
make 
#make a dir Julia and put all the built stuff with includes and libs
mkdir -p Julia
rsync -r --links --update ../julia Julia/ #copy julia libs and includes..
cp Julia.scx ./Julia
cp ../Julia.sc ./Julia
cp ../Sine_DSP.jl ./Julia
#copy stuff over to SC's User Extension directory
rsync -r --links --update ./Julia ~/Library/Application\ Support/SuperCollider/Extensions