
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
set LIB_DIR=%ARTIFACT%Lib\%LIB_DIR%\

set DLL_DIR=%TARGET_OS%.%TARGET_CPU%
set DLL_DIR=%DLL_DIR:Win.x86=Win32%
set DLL_DIR=%DLL_DIR:Win.x64=Win64%
set DLL_DIR=%ARTIFACT%..\..\..\Binaries\ThirdParty\V8\%DLL_DIR%\

echo "INC_DIR=%INC_DIR%"
echo "LIB_DIR=%LIB_DIR%"
echo "DLL_DIR=%DLL_DIR%"

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
del /s /q %LIB_DIR%*.dll
del /s /q %LIB_DIR%*.pdb
del /s /q %LIB_DIR%*.lib
copy %OUT_DIR%v8.dll* %LIB_DIR%
copy %OUT_DIR%v8_lib*.dll %LIB_DIR%
copy %OUT_DIR%v8_lib*dll.lib %LIB_DIR%
copy %OUT_DIR%v8_lib*dll.pdb %LIB_DIR%
copy %OUT_DIR%obj\v8_lib*.lib %LIB_DIR%
copy %OUT_DIR%obj\v8_monolith.lib %LIB_DIR%
REM for /R %LIB_DIR% %%I in ("*.*") do (
REM     call %~dp0build-ren.bat %%I
REM )

if not exist %DLL_DIR% (
	md %DLL_DIR%
)
copy %LIB_DIR%*.dll %DLL_DIR%
copy %LIB_DIR%*.pdb %DLL_DIR%

popd
