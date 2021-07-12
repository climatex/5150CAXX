; 5150CAXX, /X command line option binary example
; Assemble with FASM or similar

use16           ; 16-bit binary
org 0           ; 5150CAXX.EXE allocates a new 64K segment with data starting at offset 0

jmp start

message db 'Hello world!',13,10,'$'

start:
xor ax,ax       ;Set 40x25
int 10h
mov ah,9        ;Print the string
lea dx,[message]
int 21h
xor ax,ax       ;Wait for a keypress
int 16h
mov ax,0003     ;Set 80x25 again
int 10h
retf            ;FAR return to parent that called it (5150CAXX in this case)
