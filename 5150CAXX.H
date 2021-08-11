FILE* pFile = NULL;

unsigned char* pHeapBuffer = NULL;
unsigned int nBufferSegment = 0;
unsigned int nMaximumDataSize = 0xffff;

const char* pFileName = NULL;
unsigned int nWaitForPressPlay = 0;
unsigned char cOperation = 0;
unsigned long nWantedBytes = 0;
unsigned int nSpecifiedBytes = 0;
unsigned char nTargetIsMemorySegment = 0;

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
  sregs.es = nBufferSegment;
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
  sregs.es = nBufferSegment;
  regs.x.bx = 0;
  
  /* Disable hardware IRQ ISRs during tape operation */
  asm cli  
  int86x(0x15, &regs, &regs, &sregs);
  asm sti
  
  /* Offset of the last byte written in BX */
  *nBytesWritten = regs.x.bx;
  
  return regs.h.ah;
}

/* Dump tape buffer to screen */
void DumpMemory(unsigned int nLength)
{
  unsigned int nOffset = 0;
  
  for (; nOffset < nLength; nOffset++)
  {
    unsigned char nByte = *((unsigned char far*)(MK_FP(nBufferSegment, nOffset)));
    
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
  _BX = nBufferSegment;
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

  /* Deallocate memory used with disk I/O: was it on heap? */
  if (pHeapBuffer)
  {
    free(pHeapBuffer);
  }
  
  /* No, it was a DOS memory block (default) */
  else if (!nTargetIsMemorySegment && nBufferSegment)
  {
    freemem(nBufferSegment);
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
  /* Allocate 64K max (4096 16-byte paragraphs) and return segment:offset always 0 */
  int nStatus = allocmem(4096, &nBufferSegment);
  
  /* Not enough memory? */
  if (nStatus != -1)
  {
    /* Allocate the maximum paragraph count (times 16) */
    if (nStatus > 0)
    {
      nMaximumDataSize = nStatus * 16;
      allocmem(nStatus, &nBufferSegment);
    }
    
    /* Zero paragraphs available. Try malloc on heap... */
    else
    {
      /* Reserve some 3K on heap for the standard library (fopen etc) */
      const unsigned int nReserve = 3*1024;
      
      unsigned long nAddress;
      unsigned int nAllocSize = coreleft();
      
      /* Check how many bytes left on heap and if within reserve */
      if (nAllocSize < nReserve)
      {
        asm jmp _nomem;
      }
      
      nAllocSize -= nReserve;
      pHeapBuffer = malloc(nAllocSize);
      if (!pHeapBuffer)
      {
_nomem:
        printf("\nNot enough memory to run the application.\n");
        Quit(EXIT_FAILURE);
      }
      
      /* Determine address of heap buffer in memory */
      nAddress = (_DS * (unsigned long)0x10) + (unsigned int)(pHeapBuffer);
      
      /* Not aligned to offset 0? (most of the time) */
      if (nAddress % 0x10)
      {
        /* Align to 0 and subtract the offset difference from the maximum available size */
        unsigned long nAlignedAddress;
        nBufferSegment = (unsigned int)((nAddress / 0x10) + 1);
        nAlignedAddress = nBufferSegment * (unsigned long)0x10;
        
        nMaximumDataSize = nAllocSize - labs(nAlignedAddress - nAddress);
      }
           
      /* Aligned perfectly, use as is */
      else
      {
        nBufferSegment = (unsigned int)(nAddress / 0x10); /* set segment */
        nMaximumDataSize = nAllocSize;
      }
    }
  }
  
  _fmemset(MK_FP(nBufferSegment, 0), 0, nMaximumDataSize);
  
  printf("\nMaximum data size: ");
  if (nMaximumDataSize == 0xffff)
  {
    printf("64K\n");
  }
  
  else
  {
    printf("%u bytes (64K, if given existing RAM segment)\n", nMaximumDataSize);
  }
}

/* Will fail to run on a non IBM-5150 or PCjr, or without a proper 4.77MHz 8088 */
void CheckCurrentSetup()
{
  /* Error messages */
  const char sPCRequired[] = "\nAn original IBM 5150 or PCjr is required to run this application.\n";
  const char s8088Required[] = "\nA genuine Intel 8088 CPU is required to run this application.\n";
  
  /* Get PC equipment byte */
  unsigned char nEquipmentByte = *((unsigned char far*)MK_FP(0xf000, 0xfffe));
  
  /* Must be an IBM PC or PCjr */
  if ((nEquipmentByte != 0xff) && (nEquipmentByte != 0xfd))
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
}

/* Printed on incorrect or no command line arguments */
void PrintUsage()
{
  printf("\nUsage:\n"
         "5150CAXX [/A] /R target [bytes]\n"
	     "         [/A] /W source [bytes]\n"
	     "         [/A] /D [bytes]\n"
	     "         [/A] /X [bytes]\n\n"
	     "where:\n"
	     " /R - reads number of [bytes] from tape and saves them to target,\n"
	     " /W - reads source up to [bytes] length, and records it to tape,\n"
	     " /D - reads number of [bytes] from tape and dumps them to screen as raw ASCII,\n"
	     " /X - reads number of [bytes] from tape and executes them as code at org 0,\n"
	     " /A - (optional) asks to press play on tape before the selected operation.\n\n"
	     "Seek your tape to the desired point from which to begin the operation, first.\n"
         "The source and target can be a file name or a hex memory segment (eg. 0xF600).\n"	     
	     "Using segments instead of file names (/R and /W only) might cause a crash\n"
         "if writing into nonexistent (unallocated) or already occupied locations.\n"
	     "/X will crash the system if illegal instructions are read from the tape.\n"
         "Using [bytes] is optional. In any case, the max size above cannot be exceeded.\n");
}

void ParseCommandLine(int argc, char* argv[])
{
  int indexArgs = 0;
  int indexSec = 0;
  
  /* Display usage information */
  if ( (argc < 2) || (argc > 5) )
  {
    PrintUsage();
    Quit(EXIT_SUCCESS);
  }
  
  for (indexArgs = 1; indexArgs < argc; indexArgs++)
  {
    char* pArgument = argv[indexArgs];
    nWantedBytes = strtoul(pArgument, NULL, 10);
    
    if (stricmp(pArgument, "/A") == 0)
    {
      nWaitForPressPlay = 1;  
    }
    else if ( (indexArgs == argc-1) && (nWantedBytes > 0) )
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
    freemem(nBufferSegment);
    
    /* Set the int 15h buffer segment manually */
    nBufferSegment = (unsigned int)nCmdLine;
    nMaximumDataSize = 0xffff; /* full segments are 64K max. */
    
    /* Correct maximum data size if going past the 1MB boundary */
    nCmdLine *= 0x10;
    if (nCmdLine+nMaximumDataSize > 0xfffff)
    {
      nMaximumDataSize = (unsigned int)(0x100000 - nCmdLine);
      printf("Reducing this to %u bytes, for segment not to overlap the 1MB boundary.\n", nMaximumDataSize);
    }
  }
  
  /* Size to read or write manually specified on commandline. Check if it is 0 < arg < 64K */
  if (nWantedBytes > 0)
  {
    if (nWantedBytes <= nMaximumDataSize)
    {
      nSpecifiedBytes = (unsigned int)nWantedBytes;
    }
    else
    {
      printf("\nSpecified data size of %lu bytes exceeds memory/segment limit\n", nWantedBytes);
      Quit(EXIT_FAILURE);
    } 
  }
  
  /* Still unknown operation */
  if (cOperation == 0)
  {
    PrintUsage();
    Quit(EXIT_SUCCESS);
  }
}

/* Check result of INT 15h, allow continue on non-fatal errors */
void CheckForErrors(unsigned char nResult, const int nRecording)
{  
  DelLine();
  
  switch(nResult)
  {
  case 0:
    printf("Success!");
    break;
  case 1:
    if (nRecording)
    {
      printf("Reached a CRC error while recording.\n");
      Quit(EXIT_FAILURE);
    }
    else
    {
      printf("Reached a CRC error (or end of a valid data stream) here. Stop.");
    }
    break;
  case 2:
    printf("Reached bad tape signal (or end of a valid data stream) here. Stop.");
    break;
  case 4:
    printf("No data on tape, or no valid tape present.\n");
    Quit(EXIT_FAILURE);
    break;
  case 0x80:
    printf("Unsupported command for the tape interface.\n");
    Quit(EXIT_FAILURE);
    break;
  case 0x86:
    printf("No tape in drive, or no tape drive present.\n");
    Quit(EXIT_FAILURE);
    break;
  default:
    printf("Error: ROM-BIOS returned an unspecified error code.\n");
    Quit(EXIT_FAILURE);
    break;
  }
}

/* Main funtionality */
void DoOperation()
{
  unsigned int nSuccessfulBytes = 0;
  unsigned char nMainOperationResult = 0;                                              
  long nFileSize = 0;
    
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
    nSpecifiedBytes = nMaximumDataSize;
  }
  
  /* If we're going to write to the tape, inform about the boundary */
  if (pFile && (cOperation == 'W'))
  {
    fseek(pFile, 0, SEEK_END);
    nFileSize = ftell(pFile);
    rewind(pFile);
    
    if (nFileSize > nSpecifiedBytes)
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
      printf(" to memory segment 0x%04X.\n\n", nBufferSegment);
    }
    break;
  case 'W':
    if (!nTargetIsMemorySegment)
    {
      printf(" from file %s.\n\n", pFileName);
    }
    else
    {
      printf(" from memory segment 0x%04X.\n\n", nBufferSegment);  
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
    
    if (nTargetIsMemorySegment)
    {
      asm jmp _skipread
    }
    
    printf("Reading file...");

    /* In 1K increments (no far fread) */
    while (nOffset != nSpecifiedBytes)
    {
      unsigned char cBuffer[1024] = {0};
      const int nSize = ((nSpecifiedBytes-nOffset) < 1024) ? nSpecifiedBytes-nOffset : 1024;
      size_t nResult = fread(cBuffer, sizeof(unsigned char), nSize, pFile);
      
      if (nResult < nSize)
      {
        DelLine();
        printf("File read error: Could not read %s from the disk.\n", pFileName);
        Quit(EXIT_FAILURE);
      }
      
      _fmemcpy(MK_FP(nBufferSegment, nOffset), cBuffer, nSize);
      nOffset += nSize;
    }
    
    DelLine();
_skipread:
    WaitForKeypress();
    DelLine();
    
    printf("Tape motor on...");   
    CheckForErrors(TapeMotorOn(), 0);
    
    DelLine();
    printf("Recording tape...");
    nMainOperationResult = WriteTape(nSpecifiedBytes, &nSuccessfulBytes);
    TapeMotorOff();
    CheckForErrors(nMainOperationResult, 1);
    
    printf("\n%u out of %u bytes recorded successfully.\n"
           "Be sure you note down this value, and also your current tape position.\n",
	       nSuccessfulBytes, nSpecifiedBytes);
    
    Quit(EXIT_SUCCESS);
  }
  
  /* Read from tape and write to file/memory segment/do an ASCII dump/whatever else is handled here */
  WaitForKeypress();
  DelLine();
  
  printf("Tape motor on...");   
  CheckForErrors(TapeMotorOn(), 0);
    
  DelLine();  
  printf("Loading tape..."); 
  nMainOperationResult = ReadTape(nSpecifiedBytes, &nSuccessfulBytes);
  TapeMotorOff();
  CheckForErrors(nMainOperationResult, 0);
  
  printf("\n%u out of %u bytes read successfully.\n", nSuccessfulBytes, nSpecifiedBytes);
  
  /* Quit with an error */
  if (nSuccessfulBytes == 0)
  {
    Quit(EXIT_FAILURE);
  }
  
  /* Do nothing more if the target is a custom memory segment */
  if (nTargetIsMemorySegment)
  {
    printf("\nTape buffer stored at address 0x%04X:0.\n", nBufferSegment);
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
      unsigned char cBuffer[1024] = {0};
      const int nSize = ((nSuccessfulBytes-nOffset) < 1024) ? nSuccessfulBytes-nOffset : 1024;
      
      _fmemcpy(cBuffer, MK_FP(nBufferSegment, nOffset), nSize);
      
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