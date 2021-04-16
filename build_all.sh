#!/bin/bash

PROJECTS=(
    "./examples/2D/"
    "./examples/PixelFont/"
    "./examples/Texture/"
    "./examples/TextureUpdating/"
    "./examples/FreeTypeFont/"
    "./examples/NinePatch/"
)

for t in ${PROJECTS[@]}; do
    cd "$t"
    appbuild -c GLES-debug -r
    cd -
done

