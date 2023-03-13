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
  i8 buffer[BYTESPERDISK];

  if (numb <= BYTESPERBLOCK) {
    bfsRead(inum, fbn, buffer); // read a block from the file into the buffer
    memcpy(buf, buffer, numb); // copy the read data from the buffer to buf
  } else {
    for (int i = 0; i < (numb / BYTESPERBLOCK); i++) { //read each block starting from the current cursor position
      bfsRead(inum, fbn + i, buffer + (BYTESPERBLOCK * i));
    }
    if (numb % BYTESPERBLOCK != 0) { // if there are remaining bytes read the last block seperately
      bfsRead(inum, fbn + (numb / BYTESPERBLOCK), buffer + (BYTESPERBLOCK * (numb / BYTESPERBLOCK)));
    }
    memcpy(buf, buffer, numb);
    
    //adjusts number of bytes that are read in the last block 
    int w = 0; 
    bfsRead(inum, fbn + (numb / BYTESPERBLOCK) - 1, buffer);
    for (int i = 0; i < BYTESPERBLOCK; i++) {
      if (buffer[i] == 0) {
        w++;
      }
    }
    numb = numb - w;
  }

  fsSeek(fd, numb, SEEK_CUR); //move file cursor foward by number of bytes read
  return numb; //return number of bytes read
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

// allocate new file block and initialize the buffer
if (dbn < 0) {
    bfsAllocBlock(inum, fbn);
    dbn = bfsFbnToDbn(inum, fbn);
    memset(buffer, 0, BYTESPERBLOCK);
} else {
    bfsRead(inum, fbn, buffer); //read the file data into the buffer
}

// If the number of bytes to be written is less than or equal to the size of a file block,
// copy the data from the input buffer into the current file block's buffer
if (numb <= BYTESPERBLOCK) {
    for (int i = 0; i < numb; i++) {
        buffer[(fsTell(fd) % BYTESPERBLOCK) + i] = otherBuffer[i];
    }
    bioWrite(dbn, &buffer); // write the data from currrent file block buffer to disk
} else {
    // If the number of bytes to be written is larger than a file block, write the first
    // block's worth of data to disk and then continue with the remaining data
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
        memcpy(buffer, otherBuffer + numb - remainder, BYTESPERBLOCK); //copy the next file block into the buffer
        bioWrite(dbn, buffer);
        remainder = remainder - BYTESPERBLOCK;
    }

    // if there is data remaining write the data to a new file block
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