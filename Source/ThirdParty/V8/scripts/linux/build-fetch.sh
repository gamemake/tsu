#!/bin/bash -x

#export V8_TARGET_OS_OVERWRITE="'linux', 'android'"
export V8_TARGET_OS_OVERWRITE="'android'"

source "$(dirname $0)/../mac/build-fetch.sh"
