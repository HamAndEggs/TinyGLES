#!/bin/bash

PROJECTS=(
    "./examples/2D/"
    "./examples/PixelFont/"
    "./examples/Texture/"
    "./examples/TextureUpdating/"
    "./examples/TrueTypeFont/"
)

for t in ${PROJECTS[@]}; do
    cd "$t"
    appbuild -c GLES-debug -r
    cd -
done

