@ECHO OFF

rem Build the p8 platform library for Windows

SETLOCAL

SET MYDIR=%~dp0
SET BUILDTYPE=Release
SET VSVERSION=12
SET INSTALLPATH=%MYDIR%..\build

rmdir %MYDIR%..\build /s /q

for %%T in (amd64 x86) do (
  call %MYDIR%\build-lib.cmd %%T %BUILDTYPE% %VSVERSION% %INSTALLPATH%
)

rmdir %MYDIR%..\build\cmake /s /q
