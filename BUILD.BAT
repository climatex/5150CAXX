@echo off

REM *** Assumes DOS Turbo C v3 in C:\TC ***

set TC=C:\TC

if exist 5150caxx.exe del 5150caxx.exe
del *.obj >nul

%TC%\BIN\TCC -I%TC%\INCLUDE -c -f- -1- -ms -K 5150CAXX.C
%TC%\BIN\TLINK /L%TC%\LIB /x C0FS 5150CAXX.OBJ,5150CAXX.EXE,,CS
del *.obj >nul