#!/bin/bash

if [ "$1" = "rpm" ]; then
    # A very simplistic RPM build scenario
    if [ -e rh.psd.spec ]; then
        mydir=`dirname $0`
        tmpdir=`mktemp -d`
        cp -r ${mydir} ${tmpdir}/rh.psd-2.0.2
        tar czf ${tmpdir}/rh.psd-2.0.2.tar.gz --exclude=".svn" --exclude=".git" -C ${tmpdir} rh.psd-2.0.2
        rpmbuild -ta ${tmpdir}/rh.psd-2.0.2.tar.gz
        rm -rf $tmpdir
    else
        echo "Missing RPM spec file in" `pwd`
        exit 1
    fi
else
    for impl in cpp cpp_rfnoc ; do
        if [ ! -d "$impl" ]; then
            echo "Directory '$impl' does not exist...continuing"
            continue
        fi
        cd $impl
        if [ -e build.sh ]; then
            if [ $# == 1 ]; then
                if [ $1 == 'clean' ]; then
                    rm -f Makefile
                    rm -f config.*
                    ./build.sh distclean
                else
                    ./build.sh $*
                fi
            else
                ./build.sh $*
            fi
        elif [ -e Makefile ] && [ Makefile.am -ot Makefile ]; then
            make $*
        elif [ -e reconf ]; then
            ./reconf && ./configure && make $*
        else
            echo "No build.sh found for $impl"
        fi
        retval=$?
        if [ $retval != '0' ]; then
            exit $retval
        fi
        cd -
    done
fi
