#!/bin/bash -x

set -e

V8_REVISION=c99a619ab56cbb75602f1c89ec461b9283c40917
V8_URL=https://chromium.googlesource.com/v8/v8.git

SYNC_REVISION="--revision $V8_REVISION"

gclient config --unmanaged --verbose --name=v8 $V8_URL

if [ -z "$V8_TARGET_OS_OVERWRITE" ]; then
    echo "target_os = ['mac', 'ios']" >> .gclient
else
    echo "target_os = [$V8_TARGET_OS_OVERWRITE]" >> .gclient
fi

gclient sync --verbose $SYNC_REVISION
