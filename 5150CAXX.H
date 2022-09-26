/* Tape data buffers - far used defaultly or near if there's not enough memory */
unsigned char* pNearTapeBuffer = NULL;
unsigned char far* pFarTapeBuffer = NULL;
unsigned int nTapeBufferSegment = 0;
unsigned int nMaximumTapeDataSize = 0xffff; /* 64 K */

/* To detect if we're on an IBM PC or Jr */
unsigned char far* pEquipmentByte = (unsigned char far*)MK_FP(0xf000, 0xfffe);

/* Used if tweaking recording speed.
   Bit1 usually requires to be within 25% deviation (750-1250Hz)
   for the BIOS "leader finding" code to work properly.
*/
unsigned char far* pFarBiosBuffer = NULL; /* 8K BIOS buffer, used to shadow INT 15h */
unsigned int far* pINT15PatchSeg = (unsigned int far*)MK_FP(0, 0x56); /* INT 15h vector segment */
unsigned int far* pINT15PatchOfs = (unsigned int far*)MK_FP(0, 0x54); /* INT 15h vector offset */
unsigned int nINT15SegOrig = 0xF000; /* On IBM PC/jr, INT 15h points to F000:F859 by default */
unsigned int nINT15OfsOrig = 0xF859; /* This can be overriden by DOS! */

unsigned int nBit0FrequencyHz = 2000; /* Bit 0 is a 0.5 ms (2000Hz) pulse by default */
unsigned int nBit1FrequencyHz = 1000; /* Bit 1 is a 1 ms (1000Hz) pulse by default */
const unsigned long nPITFrequencyHz = 1193180; /* PIT runs on 1.19318MHz off a divider */

/* Files related */
FILE* pFile = NULL;
const char* pFileName = NULL;
unsigned char cFileNameOnly[9] = {0};
unsigned char cFileExtOnly[5] = {0};

/* Options parsed from the command line */
unsigned char nWaitForPressPlay = 0;
unsigned char cOperation = 0;
unsigned char nCreateBASICRecord = 0;
unsigned long nWantedBytes = 0;
unsigned int nSpecifiedBytes = 0;
unsigned char nTargetIsMemorySegment = 0;

/* 244-byte BASIC stub */
unsigned char sBASICStub[244] =
{
	0xF5, 0x0C, 0x01, 0x00, 0x92, 0x2C, 0x1C, 0x00, 0x20, 0x3A, 0x20, 0x97,
	0x20, 0x53, 0x45, 0x47, 0xE7, 0x0C, 0xAD, 0xDE, 0x3A, 0x20, 0x82, 0x20,
	0x49, 0xE7, 0x11, 0x20, 0xCC, 0x20, 0x0F, 0x1F, 0x3A, 0x20, 0x87, 0x20,
	0x4A, 0x3A, 0x20, 0x98, 0x20, 0x49, 0x2C, 0x4A, 0x3A, 0x20, 0x83, 0x20,
	0x49, 0x3A, 0x20, 0x49, 0xE7, 0x11, 0x3A, 0x20, 0xB3, 0x20, 0x49, 0x00,
	0x32, 0x0D, 0x02, 0x00, 0x84, 0x20, 0x26, 0x48, 0x42, 0x39, 0x2C, 0x26,
	0x48, 0x44, 0x45, 0x2C, 0x26, 0x48, 0x41, 0x44, 0x2C, 0x26, 0x48, 0x42,
	0x38, 0x2C, 0x26, 0x48, 0x44, 0x45, 0x2C, 0x26, 0x48, 0x41, 0x44, 0x2C,
	0x26, 0x48, 0x42, 0x42, 0x2C, 0x26, 0x48, 0x44, 0x45, 0x2C, 0x26, 0x48,
	0x41, 0x44, 0x2C, 0x26, 0x48, 0x38, 0x45, 0x2C, 0x26, 0x48, 0x44, 0x38,
	0x00, 0x74, 0x0D, 0x03, 0x00, 0x84, 0x20, 0x26, 0x48, 0x38, 0x45, 0x2C,
	0x26, 0x48, 0x43, 0x30, 0x2C, 0x26, 0x48, 0x38, 0x39, 0x2C, 0x26, 0x48,
	0x43, 0x32, 0x2C, 0x26, 0x48, 0x38, 0x33, 0x2C, 0x26, 0x48, 0x45, 0x41,
	0x2C, 0x26, 0x48, 0x33, 0x30, 0x2C, 0x26, 0x48, 0x38, 0x45, 0x2C, 0x26,
	0x48, 0x44, 0x32, 0x2C, 0x26, 0x48, 0x42, 0x43, 0x2C, 0x26, 0x48, 0x30,
	0x30, 0x2C, 0x26, 0x48, 0x30, 0x32, 0x00, 0xA7, 0x0D, 0x04, 0x00, 0x84,
	0x20, 0x26, 0x48, 0x35, 0x30, 0x2C, 0x26, 0x48, 0x35, 0x33, 0x2C, 0x26,
	0x48, 0x42, 0x34, 0x2C, 0x26, 0x48, 0x30, 0x32, 0x2C, 0x26, 0x48, 0x46,
	0x41, 0x2C, 0x26, 0x48, 0x43, 0x44, 0x2C, 0x26, 0x48, 0x31, 0x35, 0x2C,
	0x26, 0x48, 0x46, 0x42, 0x2C, 0x26, 0x48, 0x43, 0x42, 0x00, 0xAD, 0x0D,
	0x05, 0x00, 0x81, 0x00
};


/* Relay goes click... */
unsigned char TapeMotorOn()
{
  union REGS regs;
  regs.x.ax = 0;
  int86(0x15, &regs, &regs);
  
  return regs.h.ah;
}

/* ... and clack */
void TapeMotorOff()
{
  union REGS regs;
  regs.x.ax = 0x100;
  int86(0x15, &regs, &regs);
}

/* Read from tape to memory */
unsigned char ReadTape(unsigned int nBytesToRead, unsigned int* nBytesRead)
{
  union REGS regs;
  struct SREGS sregs;

  regs.x.ax = 0x200;
  regs.x.cx = nBytesToRead;
  sregs.es = nTapeBufferSegment;
  regs.x.bx = 0;
  
  /* Disable hardware IRQ ISRs during tape operation */
  asm cli 
  int86x(0x15, &regs, &regs, &sregs);
  asm sti
  
  /* Number of successful bytes read is in DX */
  *nBytesRead = regs.x.dx;
  
  return regs.h.ah;
}

/* Write from memory to tape */
unsigned char WriteTape(unsigned int nBytesToWrite, unsigned int* nBytesWritten)
{
  union REGS regs;
  struct SREGS sregs;

  regs.x.ax = 0x300;
  regs.x.cx = nBytesToWrite;
  sregs.es = nTapeBufferSegment;
  regs.x.bx = 0;
  
  /* Disable hardware IRQ ISRs during tape operation */
  asm cli  
  int86x(0x15, &regs, &regs, &sregs);
  asm sti
  
  /* Offset of the last byte written in BX */
  *nBytesWritten = regs.x.bx;
  
  return regs.h.ah;
}

/* Write from memory to tape and include a ROM-BASIC loader stub */
unsigned char WriteTapeWithBasicStub(unsigned int nBytesToWrite, unsigned int* nBytesWritten,
                                     unsigned int nSegment, unsigned int nOffset)
{
  union REGS regs;
  struct SREGS sregs;
  unsigned char cBuf[17] = {0};
    
  /* Prepare the BASIC header */
  cBuf[0] = 0xA5;
  memset(&cBuf[1], 0x20, 8); /* name padded with spaces */
  memcpy(&cBuf[1], cFileNameOnly, strlen(cFileNameOnly));  
  cBuf[9] = 0x80; /* tokenized BASIC */
  cBuf[0xA] = sizeof(sBASICStub); /* 244 */
  cBuf[0xC] = 0x60; /* BASIC loader will be at 0060:081E, same as IBM Advanced Diagnostics */
  cBuf[0xE] = 0x1E;
  cBuf[0xF] = 0x08;
  
  /* Write the BASIC header */
  regs.x.ax = 0x300;
  regs.x.cx = sizeof(cBuf);
  sregs.es = FP_SEG(cBuf);
  regs.x.bx = FP_OFF(cBuf);
  asm cli  
  int86x(0x15, &regs, &regs, &sregs);
  asm sti
  
  /* Prepare the BASIC loader */
  memset(cBuf, 0, 16);
  
  /* Binary loading location: nSegment:nOffset. nOffset equals 0x100 if .COM extension, otherwise 0. */
  /* 2 bytes: set DEF SEG for a machine code stub loader, to load the rest of the tape, at nSegment-0x10 */
  /* Stack segment is set inside to nSegment-0x30:0x200, i.e. right below said stub. */
  {
    unsigned int* setDefSeg = (unsigned int*)&(sBASICStub[0x12]);
    *setDefSeg = nSegment - 0x10;
  }
  
  /* Ugly tokenized BASIC PEEK-POKE machine code follows. First, DATA &H(low byte), then &H(high byte) */  
  sprintf(cBuf, "&H%02X", (unsigned char)nBytesToWrite);          /* MOV CX,nBytesToWrite. (CX for INT15h, AH=02) */
  memcpy(&(sBASICStub[0x47]), cBuf, 4);
  sprintf(cBuf, "&H%02X", (unsigned char)(nBytesToWrite >> 8));
  memcpy(&(sBASICStub[0x4C]), cBuf, 4);
  sprintf(cBuf, "&H%02X", (unsigned char)nSegment);               /* MOV AX,nSegment. (Binary loading segment.) */
  memcpy(&(sBASICStub[0x56]), cBuf, 4);
  sprintf(cBuf, "&H%02X", (unsigned char)(nSegment >> 8));
  memcpy(&(sBASICStub[0x5B]), cBuf, 4);
  sprintf(cBuf, "&H%02X", (unsigned char)nOffset);                /* MOV BX,nOffset. (Binary loading offset.) */
  memcpy(&(sBASICStub[0x65]), cBuf, 4);
  sprintf(cBuf, "&H%02X", (unsigned char)(nOffset >> 8));
  memcpy(&(sBASICStub[0x6A]), cBuf, 4);
                   
  /* Write the BASIC stub */
  regs.x.ax = 0x300;
  regs.x.cx = sizeof(sBASICStub);
  sregs.es = FP_SEG(sBASICStub);
  regs.x.bx = FP_OFF(sBASICStub);
  asm cli  
  int86x(0x15, &regs, &regs, &sregs);
  asm sti
    
  /* And finally write the program binary */  
  return WriteTape(nBytesToWrite, nBytesWritten);  
}

/* Dump tape buffer to screen */
void DumpMemory(unsigned int nLength)
{
  unsigned int nOffset = 0;
  
  for (; nOffset < nLength; nOffset++)
  {
    unsigned char nByte = *((unsigned char far*)(MK_FP(nTapeBufferSegment, nOffset)));
    
    /* Skip ASCII BELL (control code 7) to stop the machine from honking */
    if (nByte == 7)
    {
      nByte = 0;
    }
    
    putchar(nByte);
  }
}

/* PUSHA, CALL FAR tape buffer, POPA... 8088 and inline assembly "friendly" :) */
void CallMemory()
{
  /* Store flags, DS, ES, BP, SI, DI, AX to DX (no PUSHA on 8088) */
  asm pushf
  asm push ds
  asm push es
  asm push bp
  asm push si
  asm push di
  asm push ax
  asm push bx
  asm push cx
  asm push dx
    
  /* Set DS and ES to be the same as CS of tape buffer (will be CS=DS=ES) */
  _BX = nTapeBufferSegment;
  _DS = _BX;
  _ES = _BX;
  
  /* BX = tape buffer offset (always 0!) */
  _BX = 0;
    
  /* Get current instruction pointer address... (by doing a near CALL +0) */
  asm db 0e8h, 0, 0
  
  /* AX = IP from stack */
  asm pop ax
  /* 1 byte */
  
  /* Push return address to stack (CS:IP+9 bytes to the end of function - hack) */
  asm push cs
  asm add ax,9
  asm push ax
  /* 5 bytes */
  
  /* Push segment and offset (0) of tape buffer */
  asm push ds
  asm push bx
  /* 2 bytes */
  
  /* Far call */
  asm retf
  /* 1 byte */
  
  /* It shall return here... or should, at least :) */
dummy_return_label:

  /* Restore registers */
  asm pop dx
  asm pop cx
  asm pop bx
  asm pop ax
  asm pop di
  asm pop si
  asm pop bp
  asm pop es
  asm pop ds
  asm popf
}

/* Terminate with exit code, do cleanup beforehand */
void Quit(int nStatus)
{
  if (pFile)
  {
    fclose(pFile);
    
    /* If there was a fatal error and we wrote a temporary file to disk, erase it */
    if ((cOperation == 'R') && (nStatus == EXIT_FAILURE))
    {
      remove(pFileName);
    }
  }

  /* Deallocate memory used with disk I/O */
  if (pNearTapeBuffer)
  {
    free(pNearTapeBuffer);
  }
  else if (pFarTapeBuffer)
  {
    farfree(pFarTapeBuffer);
  }
  
  /* Did we tweak the recording speed? */
  if (pFarBiosBuffer)
  {
    /* Restore original INT 15h vector */
    *pINT15PatchSeg = nINT15SegOrig;
    *pINT15PatchOfs = nINT15OfsOrig;
    
    farfree(pFarBiosBuffer);
  }
    
  /* Make sure the tape motor is off before passing control */
  TapeMotorOff();
  exit(nStatus);
}

/* Clear the screen, set 80x25 video mode and print the first line */
void PrintSplash()
{
  asm mov ax,3
  asm int 10h

  printf("5150CAXX - DOS cassette tape interface for the IBM PC, (c) J. Bogin\n");
}

/* If executed with /A, wait for confirmation from the user (press PLAY or RECORD+PLAY) */
void WaitForKeypress()
{
  if (nWaitForPressPlay)
  {
    printf("Press %sPLAY on tape and then press a key to continue...",
           (cOperation == 'W') ? "RECORD+" : "");
  
    asm xor ax,ax
    asm int 16h
  }
}

void DelLine()
{
  int index = 0;
  printf("\r");
  
  for (; index < 79; index++)
  {
    putchar(' ');
  }
  
  printf("\r");
}

/* Determine maximum free memory */
void PrintMaximumSizeLimit()
{  
  unsigned char nAllocateNearBuffer = 0;
  unsigned long nAddress = 0;
   
  /* Allocate 64K RAM (+ some reserve for the zero offset alignment)... */
  unsigned long nAllocSize = (unsigned long)nMaximumTapeDataSize + 16;
  pFarTapeBuffer = farmalloc(nAllocSize);
  
  /* Not enough memory? */
  if (!pFarTapeBuffer)
  {
    /* Get free memory (minus some reserve for the standard library) for both heaps */
    const unsigned int nNearFree = coreleft();
    const unsigned long nFarFree = farcoreleft();
    unsigned long nFree = 0;
    const unsigned int nReserve = 3*1024;
    
    /* Which one is greater? */       
    if ((unsigned long)nNearFree < nFarFree)
    {
      nFree = nFarFree;
    }
    else
    {
      nFree = nNearFree;
      nAllocateNearBuffer = 1;
    }
    
    /* Subtract the reserve */
    if (nFree <= nReserve)
    {
_nomem:
      printf("\nNot enough memory to run the application.\n");
      Quit(EXIT_FAILURE);
    }
    
    /* Try again, with reduced maximum size */
    nAllocSize = nFree - nReserve;
    
    /* near or far... */    
    if (!nAllocateNearBuffer)
    {
      pFarTapeBuffer = farmalloc(nAllocSize);
      if (!pFarTapeBuffer)
      {
        asm jmp _nomem
      }  
    }
    else
    {
      pNearTapeBuffer = malloc((unsigned int)nAllocSize);
      if (!pNearTapeBuffer)
      {
        asm jmp _nomem
      }
    }    
  }
    
  /* Success. Determine address of heap buffer in memory */
  if (!nAllocateNearBuffer)
  {
    nAddress = (FP_SEG(pFarTapeBuffer) * (unsigned long)0x10) + FP_OFF(pFarTapeBuffer);
  }
  else
  {
    nAddress = (_DS * (unsigned long)0x10) + (unsigned int)(pNearTapeBuffer);
  }  
  
  /* Not aligned to offset 0? (most of the time) */
  if (nAddress % 0x10)
  {
    /* Align to 0 and subtract the offset difference from the maximum available size */
    unsigned long nAlignedAddress;
    nTapeBufferSegment = (unsigned int)((nAddress / 0x10) + 1);
    nAlignedAddress = nTapeBufferSegment * (unsigned long)0x10;
    
    nAllocSize -= labs(nAlignedAddress - nAddress);
  }
       
  /* Aligned perfectly, use as is */
  else
  {
    nTapeBufferSegment = (unsigned int)(nAddress / 0x10); /* set segment */
  }
  
  /* Round to 64K and set maximum data size */
  if (nAllocSize > 0xffff)
  {
    nAllocSize = 0xffff;
  }
  nMaximumTapeDataSize = (unsigned int)nAllocSize;
  
  /* Clear the buffer */
  _fmemset(MK_FP(nTapeBufferSegment, 0), 0, nMaximumTapeDataSize);
  
  printf("\nMaximum data size: ");
  if (nMaximumTapeDataSize == 0xffff)
  {
    printf("64K\n");
  }
  
  else
  {
    printf("%u bytes (64K, if given existing RAM segment)\n", nMaximumTapeDataSize);
  }
}

/* Will fail to run on a non IBM-5150 or PCjr, or without a proper 4.77MHz 8088 */
void CheckCurrentSetup()
{
  /* Error messages */
  const char sPCRequired[] = "\nAn original IBM 5150 or PCjr is required to run this application.\n";
  const char s8088Required[] = "\nA genuine Intel 8088 CPU is required to run this application.\n";
    
  /* Must be an IBM PC or PCjr */
  if ((*pEquipmentByte != 0xff) && (*pEquipmentByte != 0xfd))
  {
    printf(sPCRequired);
    Quit(EXIT_FAILURE);
  }
  
  /* Detect 8088 */
  asm push sp
  asm pop ax
  
  /* The 8088 pushes the incremented value of SP on stack */
  if (_SP == _AX)
  {
    printf(s8088Required);
    Quit(EXIT_FAILURE);
  }
  
  /* Rule out NECs */
  _AX = 0x100;
  asm aad 2
  
  /* AX shall be 2 on an Intel (NECs ignore the immediate and return 10) */
  if (_AX != 2)
  {
    printf(s8088Required);
    Quit(EXIT_FAILURE);
  }
  
  /* We are on an IBM, increase text performance a little */
  directvideo = 1;
}

/* Printed on incorrect or no command line arguments */
void PrintUsage()
{
  /* /F argument (tweak baudrate) not allowed on PCjr due to 64k ROM BIOS */
  unsigned char nAllowFArgument = (*pEquipmentByte != 0xfd);
  unsigned char cTabs[] = "\t\t     ";
  unsigned char cFArgument[] = " [/F bit0 bit1] ";
  
  if (!nAllowFArgument)
  {
    strcpy(cTabs, " ");
    strcpy(cFArgument, " ");
  }
  
  /* Print out possible input arguments... */  
  printf("\n"
       "5150CAXX [/A]%s/R  target [bytes]\n"
       "\t [/A]%s/D  [bytes]\n"
       "\t [/A]%s/X  [bytes]\n"
       "\t [/A]%s/W  source [bytes]\n"
       "\t [/A]%s/WB source [bytes]\n"
       "where:\n"
       " /R  - reads number of [bytes] from tape and saves them to target,\n"	       
       " /D  - reads number of [bytes] from tape and dumps them to screen as raw ASCII,\n"
       " /X  - reads number of [bytes] from tape and executes them as code at org 0,\n"
       " /W  - records source up to [bytes] length to tape,\n"
       " /WB - records source up to [bytes] to tape as ROM-BASIC launchable binary,\n"
       " /A  - optionally asks to press play on tape before the selected operation",
       cTabs, cTabs, cTabs, cFArgument, cFArgument);
       
  if (nAllowFArgument)
  {
    printf(",\n /F  - sets bit0/bit1 freq. in Hz; tweaks recording speed. Default: 2000 1000.\n\n");
  }
  else
  {
    printf(".\n\n");
  }
  
  /* ... and the rest */   
  printf("Seek your tape to the desired point from which to begin the operation, first.\n"
         "The source and target can be a file name or a hex memory segment (eg. 0xF600).\n"	     
         "Passing a segment to /R can cause a crash if the memory is already occupied.\n"
         "/X will crash the system if illegal instructions are read from the tape.\n"
         "Using [bytes] is optional. In any case, the max size above cannot be exceeded.\n"); 
}

/* Tweak the recording speed by "shadowing" ROM-BIOS and patching the timers */
void SetRecordingSpeed()
{
  /* Stuff we'll use */ 
  unsigned int nSegment = 0;
  unsigned long nAddress = 0;  
  unsigned int far* pPatchBit0Div = NULL;
  unsigned int far* pPatchBit1Div = NULL;
  
  /* Won't work on a PCjr, because we'd have to shadow its 64K BIOS in addition to having our 64K tape data buffer */
  /* And, we are using a "small" memory model here */
  if (*pEquipmentByte == 0xfd)
  {
    printf("\nCannot shadow PCjr BIOS to set custom speeds. Using default.\n");
    return;
  }
  
  /* Allocate an 8K buffer for BIOS + 16-byte overhead for offset alignment at 0xE000 */
  pFarBiosBuffer = farmalloc(8192 + 16);
  if (pFarBiosBuffer)
  {
    nAddress = (FP_SEG(pFarBiosBuffer) * (unsigned long)0x10) + FP_OFF(pFarBiosBuffer);
  }
  
  /* Allocation failed, or we won't be able to align the starting offset for some weird reason */
  if (nAddress < 0xe000)
  {
    if (pFarBiosBuffer)
    {
      farfree(pFarBiosBuffer);
      pFarBiosBuffer = NULL;
    }
    
    /* Continue, just warn */
    printf("\nNot enough memory to shadow PC BIOS to set custom speeds. Using default.\n");
    return;
  }
  
  /* Shadowed BIOS code needs to start at offset 0xE000 exactly - perform alignment */
  nSegment = (nAddress - 0xE000) / 0x10; /* 16-byte leeway, ah well */
  
  /* Copy the ROM BIOS into our RAM so we can do patching */
  _fmemcpy(MK_FP(nSegment, 0xE000), MK_FP(0xF000, 0xE000), 8192);
  
  /* Look where to patch:  the bit0 frequency divisor is 592, for bit1 it is 1184.
     PC BIOS assumes a 1184000 dividend instead of the more proper 1193180, which is closer to the real PIT freq.
     Actual frequency in Hz = PIT freq. (1193180) Hz / divisor
     The lower the frequency, the longer the time period for a bit, and thus, the recording speed is slower.
  */  
  
  /* IBM 5150 - 2 possible memory locations, depending on BIOS */
  /* bit0 freq divisor is at address 0xFA2D or 0xFA34 */
  pPatchBit0Div = (unsigned int far*)MK_FP(nSegment, 0xFA2D);
  if (*pPatchBit0Div != 592)
  {
    pPatchBit0Div = (unsigned int far*)MK_FP(nSegment, 0xFA34);
  }
  
  /* bit1 freq divisor address at 0xFA28 or 0xFA2F */
  pPatchBit1Div = (unsigned int far*)MK_FP(nSegment, 0xFA28);
  if (*pPatchBit1Div != 1184)
  {
    pPatchBit1Div = (unsigned int far*)MK_FP(nSegment, 0xFA2F);
  }
  
  /* Sanity check */ 
  if ((*pPatchBit0Div != 592) || (*pPatchBit1Div != 1184))
  {
    printf("\nNon-standard BIOS detected, cannot set custom speeds.\n");
    farfree(pFarBiosBuffer);
    pFarBiosBuffer = NULL;
    return;
  }
  
  /* Now patch the 2 divisors according to the chosen frequencies. */
  printf("\nUsing custom recording speed: bit0 frequency %u Hz, bit1 frequency %u Hz.\n",
         nBit0FrequencyHz, nBit1FrequencyHz);
         
  *pPatchBit0Div = nPITFrequencyHz / nBit0FrequencyHz;
  *pPatchBit1Div = nPITFrequencyHz / nBit1FrequencyHz;  
    
  /* Store the old INT 15h pointer and set the new vector to use our shadow-patch. And we're done! */
  nINT15SegOrig = *pINT15PatchSeg;
  nINT15OfsOrig = *pINT15PatchOfs;
  *pINT15PatchSeg = nSegment;
  *pINT15PatchOfs = 0xF859; /* orig. IBM BIOS offset */
}

void ParseCommandLine(int argc, char* argv[])
{
  int indexArgs = 0;
  int indexSec = 0;
   
  /* Display usage information */
  if ( (argc < 2) || (argc > 8) )
  {
    PrintUsage();
    Quit(EXIT_SUCCESS);
  }
  
  /* Ugly parsing follows */  
  for (indexArgs = 1; indexArgs < argc; indexArgs++)
  {
    char* pArgument = argv[indexArgs];
    nWantedBytes = strtoul(pArgument, NULL, 10);
    
    if (stricmp(pArgument, "/A") == 0)
    {
      nWaitForPressPlay = 1;  
    }
    else if ((stricmp(pArgument, "/F") == 0) && (argc > indexArgs+2))
    {
      nBit0FrequencyHz = strtoul(argv[indexArgs+1], NULL, 10);
      nBit1FrequencyHz = strtoul(argv[indexArgs+2], NULL, 10);
      
      /* 600-6000 Hz */
      if ((nBit0FrequencyHz < 600) || 
          (nBit1FrequencyHz < 600) || 
          (nBit0FrequencyHz > 6000) ||
          (nBit1FrequencyHz > 6000))
      {
        printf("\nBit frequencies must be between 600 to 6000 Hz.\n");
        Quit(EXIT_FAILURE);
      }
      
      /* Filler so this does not get mistaken for "wanted bytes" if at the end */
      argv[indexArgs+1][0] = 'A';
      argv[indexArgs+2][0] = 'B';
    }    
    else if ( pFileName && (indexArgs == argc-1) && (nWantedBytes > 0) )
    {
      for (indexSec = 0; indexSec < strlen(pArgument); indexSec++)
      {
        if (isdigit(pArgument[indexSec]) == 0)
        {
          nWantedBytes = 0;
          break;
        }
      }
    }
    else if ( (argc > indexArgs+1) && (stricmp(pArgument, "/R") == 0) )
    {
      cOperation = 'R';
      pFileName = argv[indexArgs+1];
    }
    else if ( (argc > indexArgs+1) && (stricmp(pArgument, "/W") == 0) )
    {
      cOperation = 'W';
      pFileName = argv[indexArgs+1];
    }
    else if ( (argc > indexArgs+1) && (stricmp(pArgument, "/WB") == 0) )
    {
      cOperation = 'W';
      pFileName = argv[indexArgs+1];
      nCreateBASICRecord = 1;
    }
    else if (stricmp(pArgument, "/D") == 0)
    {
      cOperation = 'D';
    }
    else if (stricmp(pArgument, "/X") == 0)
    {
      cOperation = 'X';
    }
  }
  
  /* Is the target not a filename, but a memory segment? */
  nTargetIsMemorySegment = pFileName && (strnicmp(pFileName, "0x", 2) == 0);
  if (nTargetIsMemorySegment)
  {
    unsigned long nCmdLine = strtoul(pFileName, NULL, 16);
    
    /* Sanity check */
    if ((nCmdLine > 0xffff) || ((nCmdLine == 0) && 
                                (strcmp(pFileName, "0") != 0) &&
                                (strcmp(pFileName, "0x0000") != 0)))
    {
      printf("\nInvalid memory segment specified\n");
      Quit(EXIT_FAILURE);
    }

    /* Deallocate memory reserved file usage */
    if (pNearTapeBuffer)
    {
      free(pNearTapeBuffer);
      pNearTapeBuffer = NULL;
    }
    else if (pFarTapeBuffer)
    {
      farfree(pFarTapeBuffer);
      pFarTapeBuffer = NULL;
    }
    
    /* Set the int 15h buffer segment manually */
    nTapeBufferSegment = (unsigned int)nCmdLine;
    nMaximumTapeDataSize = 0xffff; /* full segments are 64K max. */
    
    /* Correct maximum data size if going past the 1MB boundary */
    nCmdLine *= 0x10;
    if (nCmdLine+nMaximumTapeDataSize > 0xfffff)
    {
      nMaximumTapeDataSize = (unsigned int)(0x100000 - nCmdLine);
      printf("Given RAM segment.\nReducing 64K maximum to %u bytes, for it not overlap the 1MB boundary.\n", nMaximumTapeDataSize);
    }
    
    /* Used with /WB */
    sprintf(cFileNameOnly, "0x%04X", nTapeBufferSegment);
  }  
  else if (pFileName)
  {
     /* Obtain file name without ext or path */
    _splitpath(pFileName, NULL, NULL, cFileNameOnly, cFileExtOnly);
    
    /* Determine extension */
    if (nCreateBASICRecord && (strlen(cFileExtOnly) > 0)
                           && (stricmp(cFileExtOnly, ".com") != 0)
                           && (stricmp(cFileExtOnly, ".bin") != 0))
    {
      printf("\nA .COM or other <=64K binary with no DOS function calls is required.\n");
      Quit(EXIT_FAILURE);
    }
  }
  
  /* Size to read or write manually specified on commandline. Check if it is 0 < arg < 64K */
  if (nWantedBytes > 0)
  {
    if (nWantedBytes <= nMaximumTapeDataSize)
    {
      nSpecifiedBytes = (unsigned int)nWantedBytes;
    }
    else
    {
      printf("\nSpecified data size of %lu bytes exceeds memory/segment limit\n", nWantedBytes);
      Quit(EXIT_FAILURE);
    } 
  }
  
  /* Tweak recording speed */
  if ((cOperation == 'W') && ((nBit0FrequencyHz != 2000) || (nBit1FrequencyHz != 1000)))
  {
    SetRecordingSpeed();
  }
  
  /* Still unknown operation */
  if (cOperation == 0)
  {
    PrintUsage();
    Quit(EXIT_SUCCESS);
  }
}

/* Check result of INT 15h, allow continue on non-fatal errors */
void CheckForErrors(unsigned char nResult)
{  
  DelLine();
  
  switch(nResult)
  {
  case 0:
    printf("Success!");
    break;
  case 1:
    printf("Reached a CRC error (or end of a valid data stream) here. Stop.");
    break;
  case 2:
    printf("Reached bad tape signal (or end of a valid data stream) here. Stop.");
    break;
  case 4:
    printf("No data leader on tape, or no valid tape present.\n");
    Quit(EXIT_FAILURE);
  default:
    break;
  }
}

/* Main funtionality */
void DoOperation()
{
  unsigned int nSuccessfulBytes = 0;
  unsigned char nMainOperationResult = 0;                                              
  long nFileSize = 0;
  unsigned char cBuffer[1024] = {0};
    
  /* Will be reading or writing a file ? Open it in the correct mode */
  if (!nTargetIsMemorySegment && ((cOperation == 'R') || (cOperation == 'W')))
  {
    pFile = fopen(pFileName, (cOperation == 'R') ? "wb" : "rb");
    if (!pFile)
    {
      printf("\nCannot open file \"%s\" for %s\n", pFileName, (cOperation == 'R') ? "writing" : "reading");
      Quit(EXIT_FAILURE);
    }
  }
  
  /* No manually specified number of bytes to read/write (or invalid) */
  if (nSpecifiedBytes == 0)
  {
    nSpecifiedBytes = nMaximumTapeDataSize;
  }
  
  /* If we're going to write to the tape, inform about the boundary */
  if (pFile && (cOperation == 'W'))
  {
    fseek(pFile, 0, SEEK_END);
    nFileSize = ftell(pFile);
    rewind(pFile);
    
    if (nFileSize == 0)
    {
      printf("\nThe file is empty\n");
      Quit(EXIT_FAILURE); 
    }    
    else if (nFileSize > nSpecifiedBytes)
    {
      printf("\nReading the first %u bytes of the file only.", nSpecifiedBytes);
    }
    else
    {
      nSpecifiedBytes = (unsigned int)nFileSize;
    }
  }
  
  /* Form the message */
  printf((cOperation == 'W') ? "\nWill record " : "\nWill load ");
  printf("%u bytes ", nSpecifiedBytes);
  printf((cOperation == 'W') ? "to tape" : "from tape");
  
  switch(cOperation)
  {
  case 'R':
    if (!nTargetIsMemorySegment)
    {
      printf(" to file %s.\n\n", pFileName);
    }
    else
    {
      printf(" to memory segment 0x%04X.\n\n", nTapeBufferSegment);
    }
    break;
  case 'W':
    if (!nTargetIsMemorySegment)
    {
      printf(" from file %s.\n\n", pFileName);
    }
    else
    {
      printf(" from memory segment 0x%04X.\n\n", nTapeBufferSegment);  
    }
    break;
  case 'D':
    printf(", and display them as ASCII.\n\n");
    break;
  case 'X':
    printf(", and execute them as machine code.\n\n");
  }
  
  /* Warn about possible extra dummy data at the end of the file */
  if ((cOperation != 'W') && (nWantedBytes == 0))
  {
    printf("Warning: no data length specified. Will read until end-of-tape or data error.\n"
           "There might be some extra data padded at the end of the buffer.\n\n");
  }
  
  /* Read file/memory segment and write to tape */
  if (cOperation == 'W')
  {
    unsigned int nOffset = 0;
    unsigned int nBinaryLoadingOfs = (stricmp(cFileExtOnly, ".com") == 0) ? 0x100 : 0;
    
    if (nTargetIsMemorySegment)
    {
      asm jmp _skipread
    }
    
    printf("Reading file...");

    /* In 1K increments (no far fread) */
    while (nOffset != nSpecifiedBytes)
    {
      const int nSize = ((nSpecifiedBytes-nOffset) < 1024) ? nSpecifiedBytes-nOffset : 1024;
      size_t nResult = fread(cBuffer, sizeof(unsigned char), nSize, pFile);
      
      if (nResult < nSize)
      {
        DelLine();
        printf("File read error: Could not read %s from the disk.\n", pFileName);
        Quit(EXIT_FAILURE);
      }
      
      _fmemcpy(MK_FP(nTapeBufferSegment, nOffset), cBuffer, nSize);
      nOffset += nSize;
    }
    
    DelLine();
_skipread:
    WaitForKeypress();
    DelLine();
    
    printf("Recording tape...");
    TapeMotorOn();    
    
    if (!nCreateBASICRecord)
    {
      nMainOperationResult = WriteTape(nSpecifiedBytes, &nSuccessfulBytes);
    }
    else
    {
      /* Will be read to 0300:0000 (BIN) or 0300:0100 (.COM) */
      nMainOperationResult = WriteTapeWithBasicStub(nSpecifiedBytes, &nSuccessfulBytes, 0x300, nBinaryLoadingOfs);
    }

    TapeMotorOff();
    CheckForErrors(nMainOperationResult);
    
    printf("\n%u out of %u bytes recorded successfully.\n"
           "Be sure you note down this value, and also your current tape position.\n",
	       nSuccessfulBytes, nSpecifiedBytes);
         
    if (nCreateBASICRecord)
    {
      printf("\nTo run in ROM-BASIC, execute following: LOAD \"%s\",R"
             "\nLoading this under DOS Disk BASIC/BASICA.COM will crash the system.\n", cFileNameOnly);
    }
    
    Quit(EXIT_SUCCESS);
  }
  
  /* Read from tape and write to file/memory segment/do an ASCII dump/whatever else is handled here */
  WaitForKeypress();
  DelLine();
  
  printf("Loading tape...");
  TapeMotorOn();  
  
  nMainOperationResult = ReadTape(nSpecifiedBytes, &nSuccessfulBytes);
  TapeMotorOff();
  CheckForErrors(nMainOperationResult);
  
  printf("\n%u out of %u bytes read successfully.\n", nSuccessfulBytes, nSpecifiedBytes);
  
  /* Quit with an error */
  if (nSuccessfulBytes == 0)
  {
    Quit(EXIT_FAILURE);
  }
  
  /* Do nothing more if the target is a custom memory segment */
  if (nTargetIsMemorySegment)
  {
    printf("\nTape buffer stored at address 0x%04X:0.\n", nTapeBufferSegment);
    Quit(EXIT_SUCCESS);
  }
  
  /* Now decide what to do with the buffer */
  switch(cOperation)
  {
  
  case 'R':
  {
    unsigned int nOffset = 0;
    printf("\nWriting file...");
    
    /* in 1K increments from the far memory (no far fwrite) */
    while (nOffset != nSuccessfulBytes)
    {
      const int nSize = ((nSuccessfulBytes-nOffset) < 1024) ? nSuccessfulBytes-nOffset : 1024;
      
      _fmemcpy(cBuffer, MK_FP(nTapeBufferSegment, nOffset), nSize);
      
      if (fwrite(cBuffer, sizeof(unsigned char), nSize, pFile) < nSize)
      {
        DelLine();
        printf("File write error: Could not write %s to the disk.\n", pFileName);
        Quit(EXIT_FAILURE);
      }
      
      nOffset += nSize;
    }

    printf(" done!");
  }
    break;
    
  case 'D':
  {
    printf("\nASCII dump of the memory buffer contents:\n\n");
    DumpMemory(nSuccessfulBytes);
  }
    break;
    
  case 'X':
  {
    printf("\nInvoking CALL FAR to the memory buffer contents...\n\n");
    CallMemory();
  }
  }
 
  printf("\n");
  Quit(EXIT_SUCCESS);  
}