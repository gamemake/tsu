#!/bin/bash -x

PWD=$(cd $(dirname $0); pwd)

source $PWD/build-fetch.sh

source $PWD/build.sh Mac x64 Release
source $PWD/build.sh Mac x64 Shipping
source $PWD/build.sh IOS Arm64 Debug
source $PWD/build.sh IOS Arm64 Release
