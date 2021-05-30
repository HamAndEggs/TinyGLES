#!/bin/bash

PROJECTS=(
    "./examples/2D/"
    "./examples/FreeTypeFont/"
    "./examples/NinePatch/"
    "./examples/PixelFont/"
    "./examples/Sprites/"
    "./examples/Texture/"
    "./examples/TextureUpdating/"
)

for t in ${PROJECTS[@]}; do
    cd "$t"
    appbuild -c GLES-debug -r
    cd -
done

