#include <iostream>
#include <string>
#include <algorithm>
#include <cstring>

#include "LocalFileSystem.h"
#include "Disk.h"
#include "ufs.h"

using namespace std;


int main(int argc, char *argv[]) {
  if (argc != 2) {
    cerr << argv[0] << ": diskImageFile" << endl;
    return 1;
  }
  Disk *disk = new Disk(argv[1], UFS_BLOCK_SIZE);
  LocalFileSystem *fileSystem = new LocalFileSystem(disk);
  union {
		char byteBuf[UFS_BLOCK_SIZE];
		super_t super;
	};
  disk->readBlock(0, &super);

	cout << "Super" << endl;
	cout << "inode_region_addr " << super.inode_region_addr << endl;
	cout << "inode_region_len " << super.inode_region_len << endl;
	cout << "num_inodes " << super.num_inodes << endl;
  cout << "data_region_addr " << super.data_region_addr << endl;
	cout << "data_region_len " << super.data_region_len << endl;
  cout << "num_data " << super.num_data << endl;

  unsigned char *bmp = new unsigned char[super.inode_bitmap_len * UFS_BLOCK_SIZE];
  fileSystem->readInodeBitmap(&super,bmp);

  cout <<endl << "Inode bitmap" << endl;
  for (unsigned int i = 0;i < (unsigned int)(super.num_inodes / 8);i++) {
      cout << (unsigned) bmp[i] << " ";
  }
  cout << endl << endl;

  fileSystem->readDataBitmap(&super,bmp);
  cout << "Data bitmap" << endl;
  for (unsigned int i = 0;i < (unsigned int)(super.num_data / 8); i++) {
      cout << (unsigned int)bmp[i] << " ";
  }
  cout << endl;

  delete[] bmp;
  delete disk;
  delete fileSystem;
  return 0;
}
