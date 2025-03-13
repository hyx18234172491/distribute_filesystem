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
#include <fstream>
#include <sstream>

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

  std::ifstream file(srcFile);
  if (!file) {
      cerr << "Could not write to dst_file"<<endl;
      return 1;
  }

  std::stringstream buffer;
  buffer << file.rdbuf();
  std::string fileContent = buffer.str();

  if(fileSystem->write(dstInode,fileContent.c_str(),fileContent.size())<0){
    cerr << "Could not write to dst_file"<<endl;
    delete disk;
    delete fileSystem;
    return 1;
  }
  delete disk;
  delete fileSystem;
  return 0;
}
