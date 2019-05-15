#!/bin/bash -x

source ./build-fetch.sh

#source ./build.sh Linux x64 Release
#source ./build.sh Linux x64 Shipping
source ./build.sh Android Arm Debug
source ./build.sh Android Arm Release
source ./build.sh Android Arm64 Debug
source ./build.sh Android Arm64 Release
source ./build.sh Android x86 Debug
source ./build.sh Android x86 Release
