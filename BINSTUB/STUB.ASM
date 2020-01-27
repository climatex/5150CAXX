; Compile with FASM

use16
org 100h

xor dx,dx
mov ax,program_start
mov bx,10h
div bx

mov bx,cs
add bx,ax
mov ds,bx
mov es,bx

push cs
push word return
push ds
push word 0
retf

return:
mov ax,cs
mov ds,ax
mov es,ax
int 20h

times 48d-($-$$) db 90h
program_start:
