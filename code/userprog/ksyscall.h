/**************************************************************
 *
 * userprog/ksyscall.h
 *
 * Kernel interface for systemcalls 
 *
 * by Marcus Voelp  (c) Universitaet Karlsruhe
 *
 **************************************************************/

#ifndef __USERPROG_KSYSCALL_H__
#define __USERPROG_KSYSCALL_H__

#include "kernel.h"

#include "synchconsole.h"

void SysHalt()
{
	kernel->interrupt->Halt();
}

int SysAdd(int op1, int op2)
{
	return op1 + op2;
}

#ifdef FILESYS_STUB
int SysCreate(char *filename)
{
	// return value
	// 1: success
	// 0: failed
	return kernel->interrupt->CreateFile(filename);
}
#endif

//以下是MP1的東西
int SysCreate(char *filename, int size)
{
	// return 1: success 0: failed
	return kernel->fileSystem->Create(filename, size);
}
OpenFileId SysOpen(char *name)
{
	if (kernel->fileSystem->Open(name) != NULL)
	{
		return 1; // successfully open
	}
	else
	{
		return 0; // fail
	}
	//因為我們一次只會開一個file，並且會用FileSystem中的curFile這個指標記住，因此fileID沒有實際用處，就回傳0, 1表示有無成功即可
}

int SysWrite(char *buffer, int size, OpenFileId fileID)
{
	return kernel->fileSystem->curFile->Write(buffer, size);
}

int SysRead(char *buffer, int size, OpenFileId fileID)
{
	return kernel->fileSystem->curFile->Read(buffer, size);
}

int SysClose(OpenFileId id)
{
	delete kernel->fileSystem->curFile;
	kernel->fileSystem->curFile = NULL;
	return 1;
}

#endif /* ! __USERPROG_KSYSCALL_H__ */
