@echo off

REM *** Assumes DOS Turbo C compiler in C:\TC ***
REM *** Place Turbo Assembler (TASM) into %TC%\BIN if it's not there ***

set TC=C:\TC

if exist 5150caxx.com del 5150caxx.com
del *.obj >nul

%TC%\BIN\TASM 5150CAXX.ASM
ren 5150CAXX.OBJ ASM.OBJ
%TC%\BIN\TCC -I%TC%\INCLUDE -c -f- -1- -mt -K 5150CAXX.C
%TC%\BIN\TLINK /L%TC%\LIB /x /t C0FT 5150CAXX.OBJ ASM.OBJ,5150CAXX.COM,,CS
del *.obj >nul