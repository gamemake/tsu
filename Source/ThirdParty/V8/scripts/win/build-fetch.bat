
call %~dp0build-env.bat

set V8_REVISION=c99a619ab56cbb75602f1c89ec461b9283c40917
set V8_URL=https://chromium.googlesource.com/v8/v8.git
set SYNC_REVISION=--revision %V8_REVISION%

call gclient config --unmanaged --verbose --name=v8 %V8_URL%
cmd /c echo | set /p="target_os = ['win']" >> .gclient
call gclient sync --verbose %SYNC_REVISION%
