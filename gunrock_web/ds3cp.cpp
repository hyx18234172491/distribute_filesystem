#include <iostream>
#include <string>

#include <fcntl.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

#include "LocalFileSystem.h"
#include "Disk.h"
#include "ufs.h"

using namespace std;

int main(int argc, char *argv[]) {
  if (argc != 4) {
    cerr << argv[0] << ": diskImageFile src_file dst_inode" << endl;
    cerr << "For example:" << endl;
    cerr << "    $ " << argv[0] << " tests/disk_images/a.img dthread.cpp 3" << endl;
    return 1;
  }

  Disk *disk = new Disk(argv[1], UFS_BLOCK_SIZE);
  LocalFileSystem *fileSystem = new LocalFileSystem(disk);
  string srcFile = string(argv[2]);
  int dstInode = stoi(argv[3]);

  inode_t inode;
  int inode_num = fileSystem->lookup(0,srcFile);  // 假设递归lookup
  if(inode_num < 0){
    cout << "Could not write to dst_file"<<endl;
    return 1;
  }
  fileSystem->stat(inode_num,&inode);
  unsigned char *buffer = new unsigned char[inode.size+1];

  fileSystem->read(inode_num,buffer,inode.size);
  if(fileSystem->write(dstInode,buffer,inode.size)!=-1){
    cout << "Could not write to dst_file"<<endl;
    return 1;
  }
  
  return 0;
}
