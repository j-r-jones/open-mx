#!/bin/sh

for f in $* ; do
    filepath=$(readlink -f $f)
    filedir=$(dirname $filepath)

    targetf=$filedir/$(basename $f .dot).png

    dot -Tpng -o$targetf $f
done
