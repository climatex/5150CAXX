@echo off

REM *** Assumes DOS Turbo C compiler in C:\TC ***
REM *** Place Turbo Assembler (TASM) into %TC%\BIN if it's not there ***

set TC=C:\TC

if exist 5150caxx.exe del 5150caxx.exe
del *.obj >nul

%TC%\BIN\TCC -I%TC%\INCLUDE -c -f- -1- -G -ms -K 5150CAXX.C
%TC%\BIN\TLINK /L%TC%\LIB /x C0FS 5150CAXX.OBJ,5150CAXX.EXE,,CS
del *.obj >nul