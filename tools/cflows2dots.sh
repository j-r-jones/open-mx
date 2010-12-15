#!/bin/sh

cflow2dot=/opt/cflow2vcg/bin/cflow2dot
[ "$CFLOW2DOT2" ] && cflow2dot="$CFLOW2DOT"

for f in $* ; do
    filepath=$(readlink -f $f)
    filedir=$(dirname $filepath)

    targetf=
    case $f in
        *.cflow)    targetf=$filedir/$(basename $f .cflow).dot ;;
        *)          targetf=$filepath.dot
    esac

    $cflow2dot <$f >$targetf
done
