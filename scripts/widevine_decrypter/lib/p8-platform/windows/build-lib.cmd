@ECHO OFF

rem Build the p8 platform library for Windows

SETLOCAL

SET MYDIR=%~dp0
SET BUILDARCH=%1
SET BUILDTYPE=%2
SET VSVERSION=%3
SET INSTALLPATH=%4
IF [%4] == [] GOTO missingparams

SET BUILDTARGET=%INSTALLPATH%\cmake\%BUILDARCH%
SET TARGET=%INSTALLPATH%\%BUILDARCH%

call %MYDIR%..\support\windows\cmake\generate.cmd %BUILDARCH% nmake %MYDIR%..\ %BUILDTARGET% %TARGET% %BUILDTYPE% %VSVERSION% static
call %MYDIR%..\support\windows\cmake\build.cmd %BUILDARCH% %BUILDTARGET% %VSVERSION%
goto exit

:missingparams
echo "build-lib.cmd requires 4 parameters"

:exit