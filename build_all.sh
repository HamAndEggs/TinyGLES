#!/bin/bash

PROJECTS=(
    "./examples/2D/"
    "./examples/3D/"
    "./examples/FreeTypeFont/"
    "./examples/NinePatch/"
    "./examples/PixelFont/"
    "./examples/Sprites/"
    "./examples/Texture/"
    "./examples/TextureUpdating/"
)

for t in ${PROJECTS[@]}; do
    cd "$t"
    appbuild -c debug -r
    appbuild -c release -r
    appbuild -c x11 -r
    cd -
done

