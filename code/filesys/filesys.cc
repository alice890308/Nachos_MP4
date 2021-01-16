// filesys.cc
//	Routines to manage the overall operation of the file system.
//	Implements routines to map from textual file names to files.
//
//	Each file in the file system has:
//	   A file header, stored in a sector on disk
//		(the size of the file header data structure is arranged
//		to be precisely the size of 1 disk sector)
//	   A number of data blocks
//	   An entry in the file system directory
//
// 	The file system consists of several data structures:
//	   A bitmap of free disk sectors (cf. bitmap.h)
//	   A directory of file names and file headers
//
//      Both the bitmap and the directory are represented as normal
//	files.  Their file headers are located in specific sectors
//	(sector 0 and sector 1), so that the file system can find them
//	on bootup.
//
//	The file system assumes that the bitmap and directory files are
//	kept "open" continuously while Nachos is running.
//
//	For those operations (such as Create, Remove) that modify the
//	directory and/or bitmap, if the operation succeeds, the changes
//	are written immediately back to disk (the two files are kept
//	open during all this time).  If the operation fails, and we have
//	modified part of the directory and/or bitmap, we simply discard
//	the changed version, without writing it back to disk.
//
// 	Our implementation at this point has the following restrictions:
//
//	   there is no synchronization for concurrent accesses
//	   files have a fixed size, set when the file is created
//	   files cannot be bigger than about 3KB in size
//	   there is no hierarchical directory structure, and only a limited
//	     number of files can be added to the system
//	   there is no attempt to make the system robust to failures
//	    (if Nachos exits in the middle of an operation that modifies
//	    the file system, it may corrupt the disk)
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation
// of liability and disclaimer of warranty provisions.
#ifndef FILESYS_STUB

#include "copyright.h"
#include "debug.h"
#include "disk.h"
#include "pbitmap.h"
#include "directory.h"
#include "filehdr.h"
#include "filesys.h"

// Sectors containing the file headers for the bitmap of free sectors,
// and the directory of files.  These file headers are placed in well-known
// sectors, so that they can be located on boot-up.
#define FreeMapSector 0
#define DirectorySector 1

// Initial file sizes for the bitmap and directory; until the file system
// supports extensible files, the directory size sets the maximum number
// of files that can be loaded onto the disk.

// bit 變成 byte
#define FreeMapFileSize (NumSectors / BitsInByte)
// file 最多只能放 10 個
#define NumDirEntries 10
// 所有 file entry 的資料
#define DirectoryFileSize (sizeof(DirectoryEntry) * NumDirEntries)

//----------------------------------------------------------------------
// FileSystem::FileSystem
// 	Initialize the file system.  If format = TRUE, the disk has
//	nothing on it, and we need to initialize the disk to contain
//	an empty directory, and a bitmap of free sectors (with almost but
//	not all of the sectors marked as free).
//
//	If format = FALSE, we just have to open the files
//	representing the bitmap and the directory.
//
//	"format" -- should we initialize the disk?
//----------------------------------------------------------------------

FileSystem::FileSystem(bool format)
{
    DEBUG(dbgFile, "Initializing the file system.");
    if (format)
    {
        PersistentBitmap *freeMap = new PersistentBitmap(NumSectors);
        Directory *directory = new Directory(NumDirEntries);
        FileHeader *mapHdr = new FileHeader;
        FileHeader *dirHdr = new FileHeader;

        DEBUG(dbgFile, "Formatting the file system.");

        // First, allocate space for FileHeaders for the directory and bitmap
        // (make sure no one else grabs these!)
        freeMap->Mark(FreeMapSector);
        freeMap->Mark(DirectorySector);

        // Second, allocate space for the data blocks containing the contents
        // of the directory and bitmap files.  There better be enough space!

        // allocate 空間給 directory 跟 bitmap 的 file
        ASSERT(mapHdr->Allocate(freeMap, FreeMapFileSize));
        ASSERT(dirHdr->Allocate(freeMap, DirectoryFileSize));

        // Flush the bitmap and directory FileHeaders back to disk
        // We need to do this before we can "Open" the file, since open
        // reads the file header off of disk (and currently the disk has garbage
        // on it!).

        DEBUG(dbgFile, "Writing headers back to disk.");
        mapHdr->WriteBack(FreeMapSector);
        dirHdr->WriteBack(DirectorySector);

        // OK to open the bitmap and directory files now
        // The file system operations assume these two files are left open
        // while Nachos is running.

        freeMapFile = new OpenFile(FreeMapSector);
        directoryFile = new OpenFile(DirectorySector);

        // Once we have the files "open", we can write the initial version
        // of each file back to disk.  The directory at this point is completely
        // empty; but the bitmap has been changed to reflect the fact that
        // sectors on the disk have been allocated for the file headers and
        // to hold the file data for the directory and bitmap.

        DEBUG(dbgFile, "Writing bitmap and directory back to disk.");
        freeMap->WriteBack(freeMapFile); // flush changes to disk
        directory->WriteBack(directoryFile);

        if (debug->IsEnabled('f'))
        {
            freeMap->Print();
            directory->Print();
        }
        delete freeMap;
        delete directory;
        delete mapHdr;
        delete dirHdr;
    }
    else
    {
        // if we are not formatting the disk, just open the files representing
        // the bitmap and directory; these are left open while Nachos is running
        freeMapFile = new OpenFile(FreeMapSector);
        directoryFile = new OpenFile(DirectorySector);
    }
}

//----------------------------------------------------------------------
// MP4 mod tag
// FileSystem::~FileSystem
//----------------------------------------------------------------------
FileSystem::~FileSystem()
{
    delete freeMapFile;
    delete directoryFile;
}

//----------------------------------------------------------------------
// FileSystem::Create
// 	Create a file in the Nachos file system (similar to UNIX create).
//	Since we can't increase the size of files dynamically, we have
//	to give Create the initial size of the file.
//
//	The steps to create a file are:
//	  Make sure the file doesn't already exist
//        Allocate a sector for the file header
// 	  Allocate space on disk for the data blocks for the file
//	  Add the name to the directory
//	  Store the new file header on disk
//	  Flush the changes to the bitmap and the directory back to disk
//
//	Return TRUE if everything goes ok, otherwise, return FALSE.
//
// 	Create fails if:
//   		file is already in directory
//	 	no free space for file header
//	 	no free entry for file in directory
//	 	no free space for data blocks for the file
//
// 	Note that this implementation assumes there is no concurrent access
//	to the file system!
//
//	"name" -- name of file to be created
//	"initialSize" -- size of file to be created
//----------------------------------------------------------------------

int FileSystem::Create(char *name, int initialSize)
{
    Directory *directory;
    PersistentBitmap *freeMap;
    OpenFile *dirFile = directoryFile;
    FileHeader *hdr;
    pair<int, int> temp;
    int sector, isDir;
    char *fileName;

    DEBUG(dbgFile, "Creating file " << name << " size " << initialSize);
    DEBUG(alice, "Creating file " << name << " size " << initialSize);

    directory = new Directory(NumDirEntries);
    directory->FetchFrom(directoryFile);

    fileName = strtok(name, "/");
    while(fileName != NULL) {
        temp = directory->Find(fileName);
        sector = temp.first;
        isDir = temp.second;
        if (sector != -1 && isDir == 1) { // 這層dir存在，要繼續往下走
            DEBUG(alice, fileName << " is dir, keep going");
            dirFile = new OpenFile(sector);
            directory->FetchFrom(dirFile);
        }
        else { // 這層dir不存在，所以就是要create的file
            //ASSERT (isDir == -1); // 這個檔案若已經存在，則會assertion fail
            DEBUG(alice, fileName << " is not exist, create this!");
            break;
        }
        fileName = strtok(NULL, "/");
    }
    freeMap = new PersistentBitmap(freeMapFile, NumSectors);
    sector = freeMap->FindAndSet(); // find a sector to hold the file header
    ASSERT(sector >= 0);
    ASSERT(directory->Add(fileName, sector, false));
    DEBUG(alice, "success add to directory");
    hdr = new FileHeader;
    ASSERT(hdr->Allocate(freeMap, initialSize));
    DEBUG(alice, "success allocate space");
    
    // everthing worked, flush all changes back to disk
    hdr->WriteBack(sector);
    directory->WriteBack(dirFile); // directoryFile是root directory，dirFil才是這一層的directory
    freeMap->WriteBack(freeMapFile);
    DEBUG(alice, "write back finish, create file success");
    delete hdr;
    delete freeMap;
    delete directory;
    return 1;
}

void FileSystem::CreateDirectory(char *name)
{
    Directory *directory;
    PersistentBitmap *freeMap;
    OpenFile *dirFile = directoryFile;
    FileHeader *newDirHdr = new FileHeader;
    pair<int, int> temp;
    int sector;
    char *dirname;

    directory = new Directory(NumDirEntries);
    directory->FetchFrom(directoryFile);

    dirname = strtok(name, "/");
    while(dirname != NULL) {
        temp = directory->Find(dirname);
        sector = temp.first;
        if (sector != -1) { // 這層dir存在，要繼續往下走
            dirFile = new OpenFile(sector);
            directory->FetchFrom(dirFile);
        }
        else { // 這層dir不存在，所以就是要create的dir
            break;
        }
        dirname = strtok(NULL, "/");
    }
    freeMap = new PersistentBitmap(freeMapFile, NumSectors);
    sector = freeMap->FindAndSet(); // find a sector to hold the dir header
    ASSERT(sector >= 0);
    ASSERT(directory->Add(dirname, sector, true)); // 把新的dir加到現在的directory底下
    ASSERT(newDirHdr->Allocate(freeMap, DirectoryFileSize)); //幫新的dir（data的部分) allocate空間
    newDirHdr->WriteBack(sector); // 把新的sub dir header寫回disk
    OpenFile *newDirFile = new OpenFile(sector); // 打開新的sub dir的檔案
    Directory *newDir = new Directory(NumDirEntries); //爲sub dir創建新的directory structure
    newDir->WriteBack(newDirFile); // 把這個新的directory structure寫進sub dir的檔案中
    directory->WriteBack(dirFile); // 更新舊的（上一層）dir的結構
    freeMap->WriteBack(freeMapFile); // 更新free map

    //把過程中產生的變數刪掉
    delete newDirHdr;
    delete newDirFile;
    delete newDir;
    delete freeMap;
    delete directory;
}

//----------------------------------------------------------------------
// FileSystem::Open
// 	Open a file for reading and writing.
//	To open a file:
//	  Find the location of the file's header, using the directory
//	  Bring the header into memory
//
//	"name" -- the text name of the file to be opened
//----------------------------------------------------------------------

OpenFile * FileSystem::Open(char *name)
{
    Directory *directory = new Directory(NumDirEntries);
    OpenFile *openFile = NULL;
    pair<int, int> temp;
    int sector, isDir;
    char *fileName;

    DEBUG(dbgFile, "Opening file" << name);
    DEBUG(alice, "opening file: " << name);
    directory->FetchFrom(directoryFile);

    fileName = strtok(name, "/");
    while(fileName != NULL) {
        temp = directory->Find(fileName);
        sector = temp.first;
        isDir = temp.second;
        ASSERT(sector != -1);
        if (isDir) { // 這層dir存在，要繼續往下走
            DEBUG(alice, fileName << " is dir, keep going");
            openFile = new OpenFile(sector);
            directory->FetchFrom(openFile);
        }
        else {
            DEBUG(alice, fileName << " is file, open this one!");
            break;
        }
        fileName = strtok(NULL, "/");
    }

    openFile = new OpenFile(sector); // name was found in directory
    DEBUG(alice, "success open file");
    curFile = openFile;
    delete directory;

    return openFile; // return NULL if not found
}

//----------------------------------------------------------------------
// FileSystem::Remove
// 	Delete a file from the file system.  This requires:
//	    Remove it from the directory
//	    Delete the space for its header
//	    Delete the space for its data blocks
//	    Write changes to directory, bitmap back to disk
//
//	Return TRUE if the file was deleted, FALSE if the file wasn't
//	in the file system.
//
//	"name" -- the text name of the file to be removed
//----------------------------------------------------------------------

bool FileSystem::Remove(char *name, bool recursive)
{
    Directory *directory;
    PersistentBitmap *freeMap;
    FileHeader *fileHdr;
    OpenFile *openFile, *prevFile; // prevFile紀錄前一個directory的file
    pair<int, int> temp;
    int sector, isDir;
    char *deleteName, *prevName;

    openFile = directoryFile;
    directory = new Directory(NumDirEntries);
    directory->FetchFrom(directoryFile);

    DEBUG(alice, "remove file: " << name);

    deleteName = strtok(name, "/");
    while(deleteName != NULL) {
        temp = directory->Find(deleteName);
        DEBUG(alice, "check here");
        sector = temp.first;
        isDir = temp.second;
        ASSERT(sector != -1);
        DEBUG(alice, "check assertion");
        if (isDir) {
            DEBUG(alice, "in directory: " << deleteName << ", keep going!");
            prevFile = openFile;
            prevName = deleteName;
            openFile = new OpenFile(sector);
            directory->FetchFrom(openFile);
        }
        else {
            DEBUG(alice, "find target file: " << deleteName << ", start delete!");
            break;
        }
        deleteName = strtok(NULL, "/");
    }

    freeMap = new PersistentBitmap(freeMapFile, NumSectors);

    if (recursive) {
        directory->RecursiveRemove(freeMap);
        directory->FetchFrom(prevFile); // 取得要被刪掉的這個資料夾的上一層的directory
        openFile = prevFile; // 取得上一層資料夾的file
        deleteName = prevName; // 取回要被刪掉的資料夾名稱
    }
    fileHdr = new FileHeader;
    fileHdr->FetchFrom(sector); // get the file header
    fileHdr->Deallocate(freeMap); // remove data blocks
    freeMap->Clear(sector);       // remove header block
    directory->Remove(deleteName);

    freeMap->WriteBack(freeMapFile);     // flush to disk
    DEBUG(alice, "writeback");
    directory->WriteBack(openFile); // flush to disk
    
    delete fileHdr;
    delete directory;
    delete freeMap;
    return TRUE;
}

//----------------------------------------------------------------------
// FileSystem::List
// 	List all the files in the file system directory.
//----------------------------------------------------------------------

void FileSystem::List(char *name, bool recursive)
{
    Directory *directory = new Directory(NumDirEntries);
    directory->FetchFrom(directoryFile);
    char *dirName;
    int sector, isDir;
    pair<int, int> temp;
    OpenFile *openFile = directoryFile;

    DEBUG(alice, "list file in directory: " << name);

    dirName = strtok(name, "/");
    while(dirName != NULL) {
        temp = directory->Find(dirName);
        sector = temp.first;
        isDir = temp.second;
        ASSERT(sector != -1);
        if (isDir) { // 這層dir存在，要繼續往下走
            DEBUG(rain, "directory " << dirName << " exist, keepgoing!");
            openFile = new OpenFile(sector);
            directory->FetchFrom(openFile);
        }
        dirName = strtok(NULL, "/");
    }
    if (recursive == false) {
        directory->List();
    }
    else {
        directory->RecursiveList(0);
    }
    
    delete directory;
}

//----------------------------------------------------------------------
// FileSystem::Print
// 	Print everything about the file system:
//	  the contents of the bitmap
//	  the contents of the directory
//	  for each file in the directory,
//	      the contents of the file header
//	      the data in the file
//----------------------------------------------------------------------

void FileSystem::Print()
{
    FileHeader *bitHdr = new FileHeader;
    FileHeader *dirHdr = new FileHeader;
    PersistentBitmap *freeMap = new PersistentBitmap(freeMapFile, NumSectors);
    Directory *directory = new Directory(NumDirEntries);

    printf("Bit map file header:\n");
    bitHdr->FetchFrom(FreeMapSector);
    bitHdr->Print();

    printf("Directory file header:\n");
    dirHdr->FetchFrom(DirectorySector);
    dirHdr->Print();

    freeMap->Print();

    directory->FetchFrom(directoryFile);
    directory->Print();

    delete bitHdr;
    delete dirHdr;
    delete freeMap;
    delete directory;
}

#endif // FILESYS_STUB
