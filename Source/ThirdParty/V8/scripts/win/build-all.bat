
call %~dp0build-fetch.bat

call %~dp0build.bat Win x86 Release
call %~dp0build.bat Win x86 Shipping
call %~dp0build.bat Win x64 Release
call %~dp0build.bat Win x64 Shipping
