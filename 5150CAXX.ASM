.8086                               ; 8086/88 instruction set only
.model tiny                         ; DOS .COM, CS=DS=ES
.code

public _CheckPC                     ; List of exported functions
public _Detect8088
public _TapeMotorOn
public _TapeMotorOff
public _ReadTape
public _WriteTape
public _CallMemory
public _DumpMemory

_CheckPC proc                       ; Detect IBM PC (5150) or PCjr
  push si
  push ds
  mov ax,0F000h                     ; Read a byte from f000:fffe
  mov ds,ax
  mov si,0FFFEh
  lodsb
  pop ds
  pop si
  cmp al,0FFh                       ; IBM 5150
  je ibm5150
  cmp al,0FDh                       ; IBM PCjr
  je ibmPcJr
  xor ax,ax                         ; Nope, something else - accumulator returns 0
  ret
ibm5150:
ibmPcJr:
  mov ax,1                          ; Found, return 1
  ret
_CheckPC endp

_Detect8088 proc                    ; Must be an Intel 8088
  push sp                           ; 8088 pushes the decremented value of SP on stack
  pop ax
  cmp ax,sp                         ; All the others push the original value of SP
  jnz is8088                        ; before the actual push
  xor ax,ax
  ret                               ; Fail
is8088:                             ; NEC (Vxx) test
  mov ax,0100h
  aad 2                             ; AX shall be 2, NECs return AX=0ah (ignore the immediate)
  cmp ax,2
  je isIntel
  xor ax,ax                         ; Fail
  ret
isIntel:
  mov ax,1                          ; Success, continue with execution
  ret
_Detect8088 endp

_TapeMotorOn proc                   ; Turn tape drive on
  xor ax,ax
  int 15h
  mov al,ah                         ; Return code is originally in the higher 8-bits
  xor ah,ah                         ; Since we're returning unsigned char, we want the lower part
  ret
_TapeMotorOn endp

_TapeMotorOff proc                  ; Turn tape motor off (returns void)
  push ax
  mov ax,0100h
  int 15h
  pop ax
  ret
_TapeMotorOff endp

_ReadTape proc                     ; Read tape - NEAR call: we have IP on stack (1st argument pBuffer is at stack+02)
  push bp                          ; BP will be used to address it here, so store original 16bit BP, +04 ...
  push bx
  push cx
  push dx
  push di                          ; All registers we overwrite here have been stored to stack
  mov bp,sp                        ; Now the 1st argument (buffer pointer), size == 16 bits, appears at stack+0Ch
  mov ax,0200h                     ; INT 15h: Read tape
  mov bx,word ptr [bp+0Ch]         ; ES:BX: pBuffer (same segment)
  mov cx,word ptr [bp+0Eh]         ; nBytesToRead 
  int 15h
  mov al,ah                        ; Pass return code from AH to AL
  xor ah,ah
  mov di,word ptr [bp+10h]         ; pBytesRead
  mov word ptr[es:di], dx          ; Get where it is pointing to and store the value of DX (bytes read successfully)
  pop di                           ; Restore all overwritten registers from stack and return with value in AL
  pop dx
  pop cx
  pop bx
  pop bp
  ret
_ReadTape endp

_WriteTape proc                    ; Write tape - NEAR call, so IP on stack (1st argument pBuffer is at stack+02)
  push bp
  push bx
  push cx
  push di                          ; Four 16bit words on stack + instruction pointer, so 1st argument appears at stack+0Ah
  mov bp,sp
  mov ax,0300h                     ; INT 15h: Write tape
  mov bx,word ptr [bp+0Ah]         ; pBuffer
  mov cx,word ptr [bp+0Ch]         ; nBytesToWrite
  int 15h
  mov al,ah                        ; Return unsigned char in AL
  xor ah,ah
  sub bx,word ptr [bp+0Ah]         ; Subtract the auto-incremented buffer pointer minus the beginning offset
  mov di,word ptr [bp+0Eh]         ; so that we know how many bytes has BIOS written on tape
  mov word ptr[es:di], bx          ; bp+0Eh: pBytesWritten, the value is in BX
  pop di
  pop cx
  pop bx
  pop bp
  ret
_WriteTape endp

_CallMemory proc                   ; NEAR call to the buffer we allocated for ourselves
  push bp
  mov bp,sp
  mov ax,offset return
  push ax                          ; Stack contains IP of ... us at "return" label, CS stays the same
  mov ax,word ptr [bp+04]              
  push ax                          ; pOffset
  ret                              ; Actually does "call pOffset" (pops offset from stack)
  
return:                            ; We get here if the code we got from the tape calls RET (and didn't screw up the stack)
  pop bp                           ; The base pointer we saved in our original function call
  ret                              ; Actual return
_CallMemory endp

_DumpMemory proc                   ; Raw dump of the memory buffer to screen
  push bp
  push ax
  push cx
  push si
  mov bp,sp
  mov si,word ptr [bp+0Ah]         ; pOffset
  mov cx,word ptr [bp+0Ch]         ; nLength
printer:                           ; Print using BIOS INT 10h
  mov ah,0Eh
  lodsb                            ; AL = byte[DS:SI]
  cmp al,7                         ; If it is ASCII 0x7 (BELL), treat it as ASCII 0
  je replace_bell                  ; so the machine does not keep on honking all the time... 
continue:                          ; ... that is, if we dwelve into nonprintable characters
  push bp                          ; I had an ancient (non-IBM) machine that destroyed these two on INT 10h !
  push si
  int 10h
  pop si
  pop bp
  loop printer
  pop si  
  pop cx
  pop ax
  pop bp
  ret
replace_bell:
  xor al,al                        ; 7 equals 0
  jmp continue
_DumpMemory endp

END