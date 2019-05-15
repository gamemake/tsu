
call %~dp0build-env.bat

if "%1" == "" (
    echo "os not set"
    exit 1
)
if "%2" == "" (
    echo "cpu not set"
    exit 1
)
if "%3" == "" (
    echo "config not set"
    exit 1
)
if "%4" == "Rebuild" (
    set REBUILD=true
) else (
    set REBUILD=false
)

set TARGET_OS=%1
set TARGET_CPU=%2
set TARGET_CONFIG=%3

if "%TARGET_CONFIG%" == "Debug" (
    set "GYP_DEFINES=fastbuild=0"
) else (
    set "GYP_DEFINES=fastbuild=1"
)

set OUT_DIR=out\%TARGET_OS%.%TARGET_CPU%.%TARGET_CONFIG%\
set ARTIFACT=%~dp0..\..\
set INC_DIR=%ARTIFACT%Include\
set LIB_DIR=%TARGET_OS%.%TARGET_CPU%\%TARGET_CONFIG%
set LIB_DIR=%LIB_DIR:Win.x86=Win32%
set LIB_DIR=%LIB_DIR:Win.x64=Win64%
set LIB_DIR=%ARTIFACT%Lib\%LIB_DIR:Win.x64=Win64%\

pushd v8

if not exist %OUT_DIR% (
	md %OUT_DIR%
)
copy %~dp0..\gn_files\%TARGET_OS%.%TARGET_CPU%.%TARGET_CONFIG%.gn %OUT_DIR%args.gn
call gn gen %OUT_DIR%

if "%REBUILD%" == "true" (
    call ninja -v -C %OUT_DIR% -t clean
)
call ninja -v -C %OUT_DIR%

if not exist %INC_DIR% (
	md %INC_DIR%
)
xcopy /s /y include\* %INC_DIR%
for /R %INC_DIR% %%I in (*.*) do (
	if "%%~xI" == ".h" (
		echo "%%I"
	) else (
		del "%%I"
	)
)

if not exist %LIB_DIR% (
	md %LIB_DIR%
) else (
    del /q %LIB_DIR%*.*
)
copy %OUT_DIR%v8.dll* %LIB_DIR%
copy %OUT_DIR%v8_lib*dll.lib %LIB_DIR%
copy %OUT_DIR%v8_lib*dll.pdb %LIB_DIR%
copy %OUT_DIR%obj\v8_lib*.lib %LIB_DIR%
copy %OUT_DIR%obj\v8_monolith.lib %LIB_DIR%
for /R %LIB_DIR% %%I in ("*.*") do (
    call %~dp0build-ren.bat %%I
)

popd
