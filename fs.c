/* @file fs.c
 * @brief The following code includes the implementations that fufill the project specifications of P5 
 * through completing the fsRead and fsWrite functions. 
 * @author Anthony Vu
 * @date 03/12/2023
 */

// ============================================================================
// fs.c - user FileSytem API
// ============================================================================

#include "bfs.h"
#include "fs.h"

// ============================================================================
// Close the file currently open on file descriptor 'fd'.
// ============================================================================
i32 fsClose(i32 fd) { 
  i32 inum = bfsFdToInum(fd);
  bfsDerefOFT(inum);
  return 0; 
}



// ============================================================================
// Create the file called 'fname'.  Overwrite, if it already exsists.
// On success, return its file descriptor.  On failure, EFNF
// ============================================================================
i32 fsCreate(str fname) {
  i32 inum = bfsCreateFile(fname);
  if (inum == EFNF) return EFNF;
  return bfsInumToFd(inum);
}



// ============================================================================
// Format the BFS disk by initializing the SuperBlock, Inodes, Directory and 
// Freelist.  On succes, return 0.  On failure, abort
// ============================================================================
i32 fsFormat() {
  FILE* fp = fopen(BFSDISK, "w+b");
  if (fp == NULL) FATAL(EDISKCREATE);

  i32 ret = bfsInitSuper(fp);               // initialize Super block
  if (ret != 0) { fclose(fp); FATAL(ret); }

  ret = bfsInitInodes(fp);                  // initialize Inodes block
  if (ret != 0) { fclose(fp); FATAL(ret); }

  ret = bfsInitDir(fp);                     // initialize Dir block
  if (ret != 0) { fclose(fp); FATAL(ret); }

  ret = bfsInitFreeList();                  // initialize Freelist
  if (ret != 0) { fclose(fp); FATAL(ret); }

  fclose(fp);
  return 0;
}


// ============================================================================
// Mount the BFS disk.  It must already exist
// ============================================================================
i32 fsMount() {
  FILE* fp = fopen(BFSDISK, "rb");
  if (fp == NULL) FATAL(ENODISK);           // BFSDISK not found
  fclose(fp);
  return 0;
}



// ============================================================================
// Open the existing file called 'fname'.  On success, return its file 
// descriptor.  On failure, return EFNF
// ============================================================================
i32 fsOpen(str fname) {
  i32 inum = bfsLookupFile(fname);        // lookup 'fname' in Directory
  if (inum == EFNF) return EFNF;
  return bfsInumToFd(inum);
}



// ============================================================================
// Read 'numb' bytes of data from the cursor in the file currently fsOpen'd on
// File Descriptor 'fd' into 'buf'.  On success, return actual number of bytes
// read (may be less than 'numb' if we hit EOF).  On failure, abort
// ============================================================================
i32 fsRead(i32 fd, i32 numb, void* buf) {
i32 inum = bfsFdToInum(fd);
i32 fbn = bfsTell(fd) / BYTESPERBLOCK;
i8 otherBuffer[BYTESPERDISK];

if (numb <= BYTESPERBLOCK) {
  bfsRead(inum, fbn, otherBuffer);
  memcpy(buf, otherBuffer, numb);
} else {
  i8 buffer[BYTESPERBLOCK];
  for (int i = 0; i < (numb / BYTESPERBLOCK); i++) {
    bfsRead(inum, fbn + i, buffer);
    for (int j = 0; j < BYTESPERBLOCK; j++) {
      otherBuffer[(BYTESPERBLOCK * i) + j] = buffer[j];
    }
  }
  
  if (numb % BYTESPERBLOCK != 0) {
    bfsRead(inum, fbn + (numb / BYTESPERBLOCK), buffer);
    for (int i = 0; i < (numb % BYTESPERBLOCK); i++) {
      otherBuffer[(BYTESPERBLOCK * (numb / BYTESPERBLOCK)) + i] = buffer[i];
    }
  }
  
  memcpy(buf, otherBuffer, numb);
  
  int write = 0;
  bfsRead(inum, fbn + (numb / BYTESPERBLOCK) - 1, buffer);
  for (int i = 0; i < BYTESPERBLOCK; i++) {
    if(buffer[i] == 0) {
      write++;
    }
  }
  numb = numb - write;
}

fsSeek(fd, numb, SEEK_CUR);
return numb;
}


// ============================================================================
// Move the cursor for the file currently open on File Descriptor 'fd' to the
// byte-offset 'offset'.  'whence' can be any of:
//
//  SEEK_SET : set cursor to 'offset'
//  SEEK_CUR : add 'offset' to the current cursor
//  SEEK_END : add 'offset' to the size of the file
//
// On success, return 0.  On failure, abort
// ============================================================================
i32 fsSeek(i32 fd, i32 offset, i32 whence) {

  if (offset < 0) FATAL(EBADCURS);
 
  i32 inum = bfsFdToInum(fd);
  i32 ofte = bfsFindOFTE(inum);
  
  switch(whence) {
    case SEEK_SET:
      g_oft[ofte].curs = offset;
      break;
    case SEEK_CUR:
      g_oft[ofte].curs += offset;
      break;
    case SEEK_END: {
        i32 end = fsSize(fd);
        g_oft[ofte].curs = end + offset;
        break;
      }
    default:
        FATAL(EBADWHENCE);
  }
  return 0;
}



// ============================================================================
// Return the cursor position for the file open on File Descriptor 'fd'
// ============================================================================
i32 fsTell(i32 fd) {
  return bfsTell(fd);
}



// ============================================================================
// Retrieve the current file size in bytes.  This depends on the highest offset
// written to the file, or the highest offset set with the fsSeek function.  On
// success, return the file size.  On failure, abort
// ============================================================================
i32 fsSize(i32 fd) {
  i32 inum = bfsFdToInum(fd);
  return bfsGetSize(inum);
}



// ============================================================================
// Write 'numb' bytes of data from 'buf' into the file currently fsOpen'd on
// filedescriptor 'fd'.  The write starts at the current file offset for the
// destination file.  On success, return 0.  On failure, abort
// ============================================================================
i32 fsWrite(i32 fd, i32 numb, void* buf) {
i32 inum = bfsFdToInum(fd);
i32 fbn = bfsTell(fd) / BYTESPERBLOCK;
i32 dbn = bfsFbnToDbn(inum, fbn);

i8 otherBuffer[2048];
memcpy(otherBuffer, buf, 2048);

i8 buffer[BYTESPERBLOCK] = {0};

if (dbn < 0) {
    bfsAllocBlock(inum, fbn);
    dbn = bfsFbnToDbn(inum, fbn);
    memset(buffer, 0, BYTESPERBLOCK);
} else {
    bfsRead(inum, fbn, buffer);
}

if (numb <= BYTESPERBLOCK) {
    for (int i = 0; i < numb; i++) {
        buffer[(fsTell(fd) % BYTESPERBLOCK) + i] = otherBuffer[i];
    }
    bioWrite(dbn, &buffer);
} else {
    int remainder = numb;
    int offset = fsTell(fd) % BYTESPERBLOCK;

    for (int i = offset; i < BYTESPERBLOCK; i++) {
        buffer[i] = otherBuffer[i - offset];
        remainder--;
    }
    bioWrite(dbn, buffer);

    while (remainder > BYTESPERBLOCK) {
        fbn++;
        dbn = bfsFbnToDbn(inum, fbn);
        memcpy(buffer, otherBuffer + numb - remainder, BYTESPERBLOCK);
        bioWrite(dbn, buffer);
        remainder = remainder - BYTESPERBLOCK;
    }

    if (remainder > 0) {
        dbn = bfsFbnToDbn(inum, fbn + 1);
        if (dbn < 0) {
            bfsAllocBlock(inum, fbn + 1);
            dbn = bfsFbnToDbn(inum, fbn + 1);
            memset(buffer, 0, BYTESPERBLOCK);
        } else {
            bfsRead(inum, fbn + 1, buffer);
        }
        memcpy(buffer, otherBuffer + numb - remainder, remainder);
        bioWrite(dbn, buffer);
    }
}

fsSeek(fd, numb, SEEK_CUR);
return 0;
}