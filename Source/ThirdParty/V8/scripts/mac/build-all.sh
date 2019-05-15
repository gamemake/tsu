#!/bin/bash -x

source ./build-fetch.sh

source ./build.sh Mac x64 Release
source ./build.sh Mac x64 Shipping
source ./build.sh IOS Arm64 Debug
source ./build.sh IOS Arm64 Release
