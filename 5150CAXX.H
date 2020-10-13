// Exported functions from assembly
extern unsigned char CheckPC();
extern unsigned char Detect8088();
extern unsigned char TapeMotorOn();
extern void TapeMotorOff();
extern unsigned char ReadTape(unsigned char* pBuffer, unsigned int nBytesToRead, unsigned int* pBytesRead);
extern unsigned char WriteTape(unsigned char* pBuffer, unsigned int nBytesToWrite, unsigned int* pBytesWritten);
extern void CallMemory(const void* pOffset);
extern void DumpMemory(const void* pOffset, unsigned int nLength);

FILE* pFile = NULL;

unsigned char* pTapeBuffer = NULL;
unsigned int nMaximumDataSize = 0xcccc;

const char* pFileName = NULL;
unsigned int nWaitForPressPlay = 0;
unsigned char cOperation = 0;
unsigned int nSpecifiedBytes = 0;

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
  if (pTapeBuffer)
  {
    free(pTapeBuffer);
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
    printf("Press %sPLAY on tape and then press ENTER to continue...",
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
  void* pTestBuffer = NULL;  
  while( (pTestBuffer == NULL) && (nMaximumDataSize > 0) )
  {
    pTestBuffer = malloc(nMaximumDataSize);
    if (pTestBuffer != NULL)
    {
      free(pTestBuffer);
      break;
    }
    
    nMaximumDataSize--;
  }
  
  // Be sure to leave at least a 1kB overhead for the standard library
  nMaximumDataSize -= 1024;
  nMaximumDataSize = nMaximumDataSize - (nMaximumDataSize % 64);
  
  // Now allocate the memory for global use
  pTapeBuffer = (unsigned char*)calloc(nMaximumDataSize, sizeof(unsigned char));
  
  printf("\nMaximum data size (available memory in current code segment): %u bytes\n", nMaximumDataSize);
}

// Will fail to run on a non IBM-5150 or PCjr, or without a proper 4.77MHz 8088
void CheckCurrentSetup()
{
  if (!CheckPC())
  {
    printf("\nAn original IBM 5150 or PCjr is required to run this application.\n");
    Quit(EXIT_FAILURE);
  }
  
  if (!Detect8088())
  {
    printf("\nA genuine Intel 8088 CPU is required to run this application.\n");
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
	 "If no [bytes] are given, the program will read the maximum data size from tape.\n",
	 nMaximumDataSize);
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
        printf("\nSpecified data size %lu exceeds memory segment boundary\n", nArgument);
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
      printf("Error: Tape CRC error while recording.\n"
             "Try a different tape, or clean the heads.\n");
    }
    else
    {
      printf("Tape end-of-file mark reached while loading, or CRC error.\n"
             "If getting CRC errors often, try a different tape, or clean the heads.\n");
    }
    break;
  case 2:
    printf("Reached bad tape signal here.\n"
           "If problems persist, try a different tape, or clean the heads.\n");
    break;
  case 4:
    printf("Error: No data on tape, or no valid tape present.\n");
    Quit(EXIT_FAILURE);
    break;
  case 0x80:
    printf("Error: Unsupported command for the tape interface.\n");
    Quit(EXIT_FAILURE);
    break;
  case 0x86:
    printf("Error: No tape or no tape drive present.\n");
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
  size_t nFileOperationResult = 0;
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
    printf(", and execute them as code in the current CS.\n\n");
  }
  
  // Warn about possible extra dummy data at the end of the file
  if ((cOperation != 'W') && (nSpecifiedBytes == nMaximumDataSize))
  {
    printf("Warning: no data length specified. Will read until end-of-tape or data error.\n"
           "As soon as this occurs, the file might be zero padded after the end.\n\n");
  }
  
  // Read file and write to tape  
  if (cOperation == 'W')
  {
    printf("Reading file...");
    nFileOperationResult = fread(pTapeBuffer, sizeof(unsigned char), nSpecifiedBytes, pFile);
    
    if (nFileOperationResult < nSpecifiedBytes)
    {
      DelLine();
      printf("File read error: Could not read %s from the disk.\n", pFileName);
      Quit(EXIT_FAILURE);
    }
    
    DelLine();
    WaitForKeypress();
    DelLine();
    
    printf("Tape motor on...");   
    CheckForErrors(TapeMotorOn(), 0);
    
    DelLine();
    printf("Recording tape...");
    nMainOperationResult = WriteTape(pTapeBuffer, nSpecifiedBytes, &nSuccessfulBytes);
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
  nMainOperationResult = ReadTape(pTapeBuffer, nSpecifiedBytes, &nSuccessfulBytes);
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
    printf("\nWriting file...");
    nFileOperationResult = fwrite(pTapeBuffer, sizeof(unsigned char), nSuccessfulBytes, pFile);
    
    if (nFileOperationResult < nSuccessfulBytes)
    {
      DelLine();
      printf("File write error: Could not write %s to the disk.\n", pFileName);
      Quit(EXIT_FAILURE);
    }
    
    printf(" done!");
  }
    break;
    
  case 'D':
  {
    printf("\nASCII dump of the memory buffer contents:\n\n");
    DumpMemory(pTapeBuffer, nSuccessfulBytes);
  }
    break;
    
  case 'X':
  {
    printf("\nInvoking CALL NEAR to the memory buffer contents...\n\n");
    CallMemory(pTapeBuffer);
  }
  }
 
  printf("\n");
  Quit(EXIT_SUCCESS);  
}