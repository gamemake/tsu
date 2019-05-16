#!/bin/bash -x

if [ -z "$1" ]; then
    echo "os not set"
    exit 1
fi
if [ -z "$2" ]; then
    echo "cpu not set"
    exit 1
fi
if [ -z "$3" ]; then
    echo "config net set"
    exit 1
fi
if [ "$4" == "Rebuild" ]; then
    REBUILD=true
else
    REBUILD=false
fi

BUILD_OS="$1"
BUILD_CPU="$2"
BUILD_CONFIG="$3"

if [ "$BUILD_CONFIG" == "Debug" ]; then
    export GYP_DEFINES="$GYP_DEFINES fastbuild=0"
else
    export GYP_DEFINES="$GYP_DEFINES fastbuild=1"
fi

ARTIFACT=$(cd "$( dirname "${BASH_SOURCE[0]}" )/../.." && pwd )
GN_FILE=$ARTIFACT/scripts/gn_files/$BUILD_OS.$BUILD_CPU.$BUILD_CONFIG.gn
OUT_DIR="out/$BUILD_OS.$BUILD_CPU.$BUILD_CONFIG"

if [ ! -f $GN_FILE ]; then
    echo "GN_FILE NOT FOUND, $GN_FILE"
    exit 1
fi

INC_DIR=$ARTIFACT/Include
LIB_DIR=$BUILD_OS.$BUILD_CPU.$BUILD_CONFIG
LIB_DIR=${LIB_DIR/Mac.x64./Mac\/}
LIB_DIR=${LIB_DIR/Linux.x64./Linux\/}
LIB_DIR=${LIB_DIR/IOS.Arm64./IOS\/}
LIB_DIR=${LIB_DIR/Android.Arm./Android\/Arm\/}
LIB_DIR=${LIB_DIR/Android.Arm64./Android\/Arm64\/}
LIB_DIR=${LIB_DIR/Android.x86./Android\/x86\/}
LIB_DIR=${LIB_DIR/Android.x64./Android\/x64\/}
LIB_DIR=$ARTIFACT/Lib/$LIB_DIR
DLL_DIR=$ARTIFACT../../Binaries/ThirdParty/V8/Mac

pushd v8
mkdir -p $OUT_DIR
cp "$ARTIFACT/scripts/gn_files/$BUILD_OS.$BUILD_CPU.$BUILD_CONFIG.gn" $OUT_DIR/args.gn
gn gen $OUT_DIR

if [ "$REBUILD" == "true" ]; then
    echo “Rebuild”
    #ninja -v -C $OUT_DIR -t clean || ls $OUT_DIR
fi
ninja -v -C $OUT_DIR
popd

mkdir -p $LIB_DIR
rm -rf $LIB_DIR/*
cp v8/$OUT_DIR/obj/*.a $LIB_DIR
cp v8/$OUT_DIR/*.dylib $LIB_DIR
cp v8/$OUT_DIR/*.so $LIB_DIR
rm $LIB_DIR/*for_testing*
rm $LIB_DIR/*wee8*

mkdir -p $DLL_DIR
cp v8/$OUT_DIR/*.dylib $DLL_DIR/

HEADERS_OUT=`find v8/include -name *.h`
for HEADER in $HEADERS_OUT
do
    HEADER_OUT=${HEADER/v8\/include/$INC_DIR}
    HEADER_DIR=`dirname $HEADER_OUT`
    mkdir -p $HEADER_DIR
    cp $HEADER $HEADER_OUT
done
