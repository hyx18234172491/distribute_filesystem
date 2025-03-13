#include <iostream>
#include <string>
#include <algorithm>
#include <cstring>

#include "LocalFileSystem.h"
#include "Disk.h"
#include "ufs.h"

using namespace std;


int main(int argc, char *argv[]) {
  if (argc != 3) {
    cerr << argv[0] << ": diskImageFile inodeNumber" << endl;
    return 1;
  }

  Disk *disk = new Disk(argv[1], UFS_BLOCK_SIZE);
  LocalFileSystem *fileSystem = new LocalFileSystem(disk);
  int inodeNumber = stoi(argv[2]);

  inode_t inode;
  if (fileSystem->stat(inodeNumber, &inode) != 0) {
        cerr << "Error: inode invalid: " << inodeNumber << endl;
        return 1;
  }

	cout << "File blocks" << endl;
	int cntOfBlocks = (inode.size + UFS_BLOCK_SIZE - 1) / UFS_BLOCK_SIZE;
	for (int i = 0; i < cntOfBlocks; i++) {
		cout << inode.direct[i] << endl;
	}
	cout << endl;

  char *szBuf = (char*)std::malloc(inode.size + 1);
  szBuf[inode.size] = '\0';

	int ret = fileSystem->read(inodeNumber, szBuf, inode.size);
  if (ret < 0) {
        cerr << "Error: read file data." << endl;
        free(szBuf);
        return 1;
  }
	szBuf[ret] = '\0';

	cout << "File data" << endl;
  cout << szBuf;
  free(szBuf);

  delete disk;
  delete fileSystem;

  return 0;
}
