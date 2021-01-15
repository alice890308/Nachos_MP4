// main.cc
//	Driver code to initialize, selftest, and run the
//	operating system kernel.
//
// Usage: nachos -d <debugflags> -rs <random seed #>
//              -s -x <nachos file> -ci <consoleIn> -co <consoleOut>
//              -f -cp <unix file> <nachos file>
//              -p <nachos file> -r <nachos file> -l -D
//              -n <network reliability> -m <machine id>
//              -z -K -C -N
//
//    -d causes certain debugging messages to be printed (see debug.h)
//    -rs causes Yield to occur at random (but repeatable) spots
//    -z prints the copyright message
//    -s causes user programs to be executed in single-step mode
//    -x runs a user program
//    -ci specify file for console input (stdin is the default)
//    -co specify file for console output (stdout is the default)
//    -n sets the network reliability
//    -m sets this machine's host id (needed for the network)
//    -K run a simple self test of kernel threads and synchronization
//    -C run an interactive console test
//    -N run a two-machine network test (see Kernel::NetworkTest)
//
//    Filesystem-related flags:
//    -f forces the Nachos disk to be formatted
//    -cp copies a file from UNIX to Nachos
//    -p prints a Nachos file to stdout
//    -r removes a Nachos file from the file system
//    -l lists the contents of the Nachos directory
//    -D prints the contents of the entire file system
//
//  Note: the file system flags are not used if the stub filesystem
//        is being used
//
// Copyright (c) 1992-1996 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation
// of liability and disclaimer of warranty provisions.

#define MAIN
#include "copyright.h"
#undef MAIN

#include "main.h"
#include "filesys.h"
#include "openfile.h"
#include "sysdep.h"

// global variables
Kernel *kernel;
Debug *debug;

//----------------------------------------------------------------------
// Cleanup
//	Delete kernel data structures; called when user hits "ctl-C".
//----------------------------------------------------------------------

static void
Cleanup(int x)
{
    cerr << "\nCleaning up after signal " << x << "\n";
    delete kernel;
}

//-------------------------------------------------------------------
// Constant used by "Copy" and "Print"
//   It is the number of bytes read from the Unix file (for Copy)
//   or the Nachos file (for Print) by each read operation
//-------------------------------------------------------------------
static const int TransferSize = 128;

#ifndef FILESYS_STUB
//----------------------------------------------------------------------
// Copy
//      Copy the contents of the UNIX file "from" to the Nachos file "to"
//----------------------------------------------------------------------

static void Copy(char *from, char *to)
{
    int fd;
    OpenFile *openFile;
    int amountRead, fileLength;
    char *buffer;

    // Open UNIX file
    if ((fd = OpenForReadWrite(from, FALSE)) < 0)
    {
        printf("Copy: couldn't open input file %s\n", from);
        return;
    }

    // Figure out length of UNIX file
    Lseek(fd, 0, 2); //把位置移到file的最尾端
    fileLength = Tell(fd);
    Lseek(fd, 0, 0); // 把位置移到file的最前端

    // Create a Nachos file of the same length
    DEBUG('f', "Copying file " << from << " of size " << fileLength << " to file " << to);
    if (!kernel->fileSystem->Create(to, fileLength))
    { // Create Nachos file
        printf("Copy: couldn't create output file %s\n", to);
        Close(fd);
        return;
    }

    openFile = kernel->fileSystem->Open(to);
    ASSERT(openFile != NULL);

    // Copy the data in TransferSize chunks
    buffer = new char[TransferSize];
    while ((amountRead = ReadPartial(fd, buffer, sizeof(char) * TransferSize)) > 0)
        openFile->Write(buffer, amountRead);
    delete[] buffer;

    // Close the UNIX and the Nachos files
    delete openFile;
    Close(fd);
}

#endif // FILESYS_STUB

//----------------------------------------------------------------------
//