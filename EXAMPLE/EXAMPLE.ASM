; 5150CAXX, /X command line option binary example
; Assemble with FASM or similar

use16           ; 16-bit binary
org 0           ; We don't know where we will be loaded, so offset nothing

call get_ip     ; Get instruction pointer by doing a NEAR call
get_ip:         ; IP of this address pushed on stack
pop di          ; Get it to DI

jmp start       ; Skip "execution" of the string

db 'Hello world!',13,10,'$'

start:
add di,3        ;skip "pop di" (1 byte) and "jmp short start" (2 bytes), so that it points to the string
xor ax,ax       ;Set 40x25
int 10h
mov ah,9        ;Print the string
mov dx,di       ;offset in DI
int 21h
xor ax,ax       ;Wait for a keypress
int 16h
mov ax,0003     ;Set 80x25 again
int 10h
retf            ;FAR return to parent that called it (5150CAXX in this case)
