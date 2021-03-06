FILE* pFile = NULL;

unsigned int nBufferSegment = 0;
unsigned int nMaximumDataSize = 0xffff;

const char* pFileName = NULL;
unsigned int nWaitForPressPlay = 0;
unsigned char cOperation = 0;
unsigned int nSpecifiedBytes = 0;

// Relay goes click...
unsigned char TapeMotorOn()
{
  union REGS regs;
  regs.x.ax = 0;
  int86(0x15, &regs, &regs);
  
  return regs.h.ah;
}

// ... and clack
void TapeMotorOff()
{
  union REGS regs;
  regs.x.ax = 0x100;
  int86(0x15, &regs, &regs);
}

// Read from tape to memory
unsigned char ReadTape(unsigned int nBytesToRead, unsigned int* nBytesRead)
{
  union REGS regs;
  struct SREGS sregs;

  regs.x.ax = 0x200;
  regs.x.cx = nBytesToRead;
  sregs.es = nBufferSegment;
  regs.x.bx = 0;
  
  // Disable hardware IRQ ISRs during tape operation
  _asm cli  
  int86x(0x15, &regs, &regs, &sregs);
  _asm sti
  
  // Number of successful bytes read is in DX
  *nBytesRead = regs.x.dx;
  
  return regs.h.ah;
}

// Write from memory to tape
unsigned char WriteTape(unsigned int nBytesToWrite, unsigned int* nBytesWritten)
{
  union REGS regs;
  struct SREGS sregs;

  regs.x.ax = 0x300;
  regs.x.cx = nBytesToWrite;
  sregs.es = nBufferSegment;
  regs.x.bx = 0;
  
  // Disable hardware IRQ ISRs during tape operation  
  _asm cli  
  int86x(0x15, &regs, &regs, &sregs);
  _asm sti
  
  // Offset of the last byte written in BX
  *nBytesWritten = regs.x.bx;
  
  return regs.h.ah;
}

// Dump tape buffer to screen
void DumpMemory(unsigned int nLength)
{
  unsigned int nOffset = 0;
  
  for (; nOffset < nLength; nOffset++)
  {
    unsigned char nByte = *((unsigned char far*)(MK_FP(nBufferSegment, nOffset)));
    
    // Skip ASCII BELL (control code 7) to stop the machine from honking
    if (nByte == 7)
    {
      nByte = 0;
    }
    
    putchar(nByte);
  }
}

// PUSHA, CALL FAR tape buffer, POPA... 8088 and inline assembly "friendly" :)
void CallMemory()
{
 // Store flags, DS, ES, BP, SI, DI, AX to DX (no PUSHA on 8088)
  _asm pushf
  _asm push ds
  _asm push es
  _asm push bp
  _asm push si
  _asm push di
  _asm push ax
  _asm push bx
  _asm push cx
  _asm push dx
    
  // Set DS and ES to be the same as CS of tape buffer (will be CS=DS=ES)
  _BX = nBufferSegment;
  _DS = _BX;
  _ES = _BX;
  
  // BX = tape buffer offset (always 0!)
  _BX = 0;
    
  // Get current instruction pointer address... (by doing a near CALL +0)
  _asm db 0e8h, 0, 0
  
  // AX = IP from stack
  _asm pop ax
  // 1 byte
  
  // Push return address to stack (CS:IP+9 bytes to the end of function - hack)
  _asm push cs
  _asm add ax,9
  _asm push ax
  // 5 bytes
  
  // Push segment and offset (0) of tape buffer
  _asm push ds
  _asm push bx
  // 2 bytes
  
  // Far call
  _asm retf
  // 1 byte
  
  // It shall return here... or should, at least :)  
dummy_return_label:

  // Restore registers
  _asm pop dx
  _asm pop cx
  _asm pop bx
  _asm pop ax
  _asm pop di
  _asm pop si
  _asm pop bp
  _asm pop es
  _asm pop ds
  _asm popf
}

// Terminate with exit code, do cleanup beforehand
void Quit(int nStatus)
{
  if (pFile)
  {
    fclose(pFile);
    
    // If there was a fatal error and we wrote a temporary file to disk, erase it    
    if ((cOperation == 'R') && (nStatus == EXIT_FAILURE))
    {
      remove(pFileName);
    }
  }
  
  // Deallocate memory and make sure the tape motor is off
  if (nBufferSegment)
  {
    freemem(nBufferSegment);
    TapeMotorOff();
  }
  
  exit(nStatus);
}

// Clear the screen, set 80x25 video mode and print the first line
void PrintSplash()
{
  _asm {
    mov ax,3
    int 10h
  }

  printf("5150CAXX - DOS cassette tape interface for the IBM PC, (c) J. Bogin\n");
}

// If executed with /A, wait for confirmation from the user (press PLAY or RECORD+PLAY)
void WaitForKeypress()
{
  if (nWaitForPressPlay)
  {
    printf("Press %sPLAY on tape and then press a key to continue...",
           (cOperation == 'W') ? "RECORD+" : "");
  
    _asm {
      xor ax,ax
      int 16h
    }
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

// Determine maximum free memory in our code segment
void PrintMaximumSizeLimit()
{ 
  // Allocate 64K max (4096 16-byte paragraphs) and return segment:offset always 0
  int nStatus = allocmem(4096, &nBufferSegment);
  
  // Not enough memory?
  if (nStatus != -1)
  {
    // Allocate the maximum paragraph count (times 16)
    if (nStatus > 0)
    {
      nMaximumDataSize = nStatus * 16;
      allocmem(nStatus, &nBufferSegment);
    }
    
    // Zero paragraphs available
    else
    {
      printf("\nNot enough memory to run the application.\n");
      Quit(EXIT_FAILURE);
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
    printf("%u bytes\n", nMaximumDataSize);
  }
}

// Will fail to run on a non IBM-5150 or PCjr, or without a proper 4.77MHz 8088
void CheckCurrentSetup()
{
  // Error messages
  const char sPCRequired[] = "\nAn original IBM 5150 or PCjr is required to run this application.\n";
  const char s8088Required[] = "\nA genuine Intel 8088 CPU is required to run this application.\n";
  
  // Get PC equipment byte
  unsigned char nEquipmentByte = *((unsigned char far*)MK_FP(0xf000, 0xfffe));
  
  // Must be an IBM PC or PCjr
  if ((nEquipmentByte != 0xff) && (nEquipmentByte != 0xfd))
  {
    printf(sPCRequired);
    Quit(EXIT_FAILURE);
  }
  
  // Detect 8088
  _asm push sp
  _asm pop ax
  
  // The 8088 pushes the incremented value of SP on stack
  if (_SP == _AX)
  {
    printf(s8088Required);
    Quit(EXIT_FAILURE);
  }
  
  // Rule out NECs
  _AX = 0x100;
  _asm aad 2
  
  // AX shall be 2 on an Intel (NECs ignore the immediate and return 10)
  if (_AX != 2)
  {
    printf(s8088Required);
    Quit(EXIT_FAILURE);
  }
}

// Printed on incorrect or no command line arguments
void PrintUsage()
{
  printf("\nUsage:\n"
         "5150CAXX [/A] /R filename [bytes]\n"
	     "         [/A] /W filename\n"
	     "         [/A] /D [bytes]\n"
	     "         [/A] /X [bytes]\n\n"
	     "where:\n"
	     " /R - reads number of [bytes] from tape and saves them to file on disk,\n"
	     " /W - reads file from disk and records it to tape,\n"
	     " /D - reads number of [bytes] from tape and dumps them to screen as raw ASCII,\n"
	     " /X - reads number of [bytes] from tape and executes them as machine code,\n"
	     " /A - (optional) asks to press play on tape before the selected operation.\n\n"
	     "Connect your tape recorder to the DIN5 socket next to the keyboard connector.\n"
	     "Seek your tape to the desired point from which to begin the operation, first.\n"
	     "The file cannot be bigger than the maximum size in bytes written on-screen.\n"
	     "/X will crash the system if illegal instructions are read from the tape.\n\n"
	     "If no [bytes] are given, the program will read the maximum data size from tape.\n");
}

void ParseCommandLine(int argc, char* argv[])
{
  int indexArgs = 0;
  int indexSec = 0;
  
  // Display usage information  
  if ( (argc < 2) || (argc > 5) )
  {
    PrintUsage();
    Quit(EXIT_SUCCESS);
  }
  
  for (indexArgs = 1; indexArgs < argc; indexArgs++)
  {
    char* pArgument = argv[indexArgs];
    long nArgument = atol(pArgument);
    
    if (stricmp(pArgument, "/A") == 0)
    {
      nWaitForPressPlay = 1;  
    }
    else if ( (indexArgs == argc-1) && (nArgument > 0) )
    {
      // Size to read or write manually specified on commandline. Check if it is 0 < arg < 64K
      for (indexSec = 0; indexSec < strlen(pArgument); indexSec++)
      {
        if (isdigit(pArgument[indexSec]) == 0)
        {
          // Contains letters, ignore it
          nArgument = 0;
          break;
        }
      }
      
      if (nArgument <= nMaximumDataSize)
      {
        nSpecifiedBytes = (unsigned int)nArgument;
      }
      else
      {
        printf("\nSpecified data size of %lu bytes exceeds free memory\n", nArgument);
        Quit(EXIT_FAILURE);
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
  
  // Still unknown operation
  if (cOperation == 0)
  {
    PrintUsage();
    Quit(EXIT_SUCCESS);
  }
}

// Check result of INT 15h, allow continue on non-fatal errors
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

// Main funtionality
void DoOperation()
{
  unsigned int nSuccessfulBytes = 0;
  unsigned char nMainOperationResult = 0;
  long nFileSize = 0;
  
  // Will be reading or writing a file ? Open it in the correct mode
  switch(cOperation)
  {
  case 'R':
  case 'W':
    pFile = fopen(pFileName, (cOperation == 'R') ? "wb" : "rb");
    if (!pFile)
    {
      printf("\nCannot open %s for %s\n", pFileName, (cOperation == 'R') ? "writing" : "reading");
      Quit(EXIT_FAILURE);
    }
  default:
    break;
  }
  
  // No manually specified number of bytes to read/write (or invalid)  
  if (nSpecifiedBytes == 0)
  {
    nSpecifiedBytes = nMaximumDataSize;
  }
  
  // If we're going to write to the tape, inform about the boundary  
  if (cOperation == 'W')
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
  
  // Form the message  
  printf((cOperation == 'W') ? "\nWill record " : "\nWill load ");
  printf("%u bytes ", nSpecifiedBytes);
  printf((cOperation == 'W') ? "to tape" : "from tape");
  
  switch(cOperation)
  {
  case 'R':
    printf(" to file %s.\n\n", pFileName);
    break;
  case 'W':
    printf(" from file %s.\n\n", pFileName);
    break;
  case 'D':
    printf(", and display them as ASCII.\n\n");
    break;
  case 'X':
    printf(", and execute them as machine code.\n\n");
  }
  
  // Warn about possible extra dummy data at the end of the file
  if ((cOperation != 'W') && (nSpecifiedBytes == nMaximumDataSize))
  {
    printf("Warning: no data length specified. Will read until end-of-tape or data error.\n"
           "The file might have some extra data padded at the end.\n\n");
  }
  
  // Read file and write to tape  
  if (cOperation == 'W')
  {
    unsigned int nOffset = 0;
    printf("Reading file...");

    // In 4K increments (no far fread)
    while (nOffset != nSpecifiedBytes)
    {
      unsigned char cBuffer[4096] = {0};
      const int nSize = ((nSpecifiedBytes-nOffset) < 4096) ? nSpecifiedBytes-nOffset : 4096;
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
           "Be sure you note this value, and also your current tape position.\n",
	       nSuccessfulBytes, nSpecifiedBytes);
    
    Quit(EXIT_SUCCESS);
  }
  
  // Read from tape and write to file/do an ASCII dump/whatever else is handled here  
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
  
  if (nSuccessfulBytes == 0)
  {
    Quit(EXIT_FAILURE);
  }
  
  // Now decide what to do with the buffer
  switch(cOperation)
  {
  
  case 'R':
  {
    unsigned int nOffset = 0;
    printf("\nWriting file...");
    
    // in 4K increments from the far memory (no far fwrite)
    while (nOffset != nSuccessfulBytes)
    {
      unsigned char cBuffer[4096] = {0};
      const int nSize = ((nSuccessfulBytes-nOffset) < 4096) ? nSuccessfulBytes-nOffset : 4096;
      
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