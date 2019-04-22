// 5150CAXX (c) J. Bogin
// Compile using BUILD.BAT

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "5150caxx.h"

int main(int argc, char* argv[])
{
  PrintSplash();
  CheckCurrentSetup();
  PrintMaximumSizeLimit();
  ParseCommandLine(argc, argv);
  DoOperation();
  
  return 0;
}