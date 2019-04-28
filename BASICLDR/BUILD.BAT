@echo off
if not exist cassette.bin goto end
fasm stub.asm
if not exist stub.com goto end
if exist program.com del program.com
copy /b stub.com+cassette.bin program.com
del stub.com
:end