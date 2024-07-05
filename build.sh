#!/bin/bash

TOPDIR=`readlink -f \`dirname $0\``
DEP_DIR=`readlink -f $TOPDIR/deps`
CMAKE_COMMAND="${DEP_DIR}/usr/local/oceanbase/devtools/bin/cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=1"
KERNEL_RELEASE=`grep -Po 'release [0-9]{1}' /etc/issue 2>/dev/null`

build() {
  build_dir="build_"$1; shift
  cd $TOPDIR/deps/ && bash dep_create.sh
  mkdir -p $TOPDIR/$build_dir
  cd $TOPDIR/$build_dir && ${CMAKE_COMMAND} ${TOPDIR} "$@"
}

case "X$1" in
    Xclean)
        find . -maxdepth 1 -type d -name 'build_*' | xargs rm -rf
        ;;
    Xrelease)
        build "$@" -DCMAKE_BUILD_TYPE=RelWithDebInfo
        ;;
    Xdebug)
        build "$@" -DCMAKE_BUILD_TYPE=Debug
        ;;
    *)
        echo "do nothing"
        ;;
esac
