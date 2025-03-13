#include <iostream>
#include <string>
#include <vector>
#include <assert.h>
#include <cstring>

#include "LocalFileSystem.h"
#include "ufs.h"

using namespace std;


LocalFileSystem::LocalFileSystem(Disk *disk) {
  this->disk = disk;
}

void LocalFileSystem::readSuperBlock(super_t *super) {
  if (super == nullptr) {
        cerr << "Invalid argument for reading super block." << endl;
        return;
    }

	unsigned char byteBuf[UFS_BLOCK_SIZE];
  disk->readBlock(0, byteBuf);
	memcpy(super, byteBuf, sizeof(super_t));
}

void LocalFileSystem::readInodeBitmap(super_t *super, unsigned char *inodeBitmap) {
  // 读inodeBitmap，inode有没有可能占很多个block
  for (int i = 0; i < super->inode_bitmap_len; i++)
  {
    this->disk->readBlock(super->inode_bitmap_addr+i,inodeBitmap + i * UFS_BLOCK_SIZE);
  }
}

void LocalFileSystem::writeInodeBitmap(super_t *super, unsigned char *inodeBitmap) {
  for (int i = 0; i < super->inode_bitmap_len; i++){
    this->disk->writeBlock(super->inode_bitmap_addr+i,inodeBitmap + i * UFS_BLOCK_SIZE);
  }
}

void LocalFileSystem::readDataBitmap(super_t *super, unsigned char *dataBitmap) {
  for (int i = 0; i < super->data_bitmap_len; i++){
    this->disk->readBlock(super->data_bitmap_addr+i,dataBitmap + i * UFS_BLOCK_SIZE);
  }
}

void LocalFileSystem::writeDataBitmap(super_t *super, unsigned char *dataBitmap) {
  for (int i = 0; i < super->data_bitmap_len; i++){
    this->disk->writeBlock(super->data_bitmap_addr+i,dataBitmap + i * UFS_BLOCK_SIZE);
  }
}

void LocalFileSystem::readInodeRegion(super_t *super, inode_t *inodes) {
  // 读所有的inodes
   for (int i = 0; i < super->inode_region_len; i++){
    this->disk->readBlock(super->inode_region_addr+i,(char*)inodes + i*UFS_BLOCK_SIZE);
   }
}


void LocalFileSystem::writeInodeRegion(super_t *super, inode_t *inodes) {
  for (int i = 0; i < super->inode_region_len; i++){
    this->disk->writeBlock(super->inode_region_addr,(char*)inodes+i*UFS_BLOCK_SIZE);
  }
}



bool checkInodeIsValid(super_t *super,int parentInodeNumber){
  // 判断父节点inode是否有效
  if(parentInodeNumber>=super->num_inodes || parentInodeNumber<0 ){
    return -EINVALIDINODE;
  }
  return 0;
}

bool checkInodeIsExist(super_t *super,int parentInodeNumber,unsigned char * inode_bit_map){
  // parentInodeNumber对应第多少位
  int byte_index = parentInodeNumber / 8;
  // 在字节里的第几位
  int bit_index_in_byte = parentInodeNumber % 8;
  if(((inode_bit_map[byte_index] >> (bit_index_in_byte)) & 1)==0){
    return 0; // 不存在
  }
  return 1; // 存在
}


int LocalFileSystem::lookup(int parentInodeNumber, string name) {

  inode_t inode;
  if (stat(parentInodeNumber, &inode)) {
        return -EINVALIDINODE;
  }
  int entries_num = UFS_BLOCK_SIZE / sizeof(dir_ent_t);
  // 找这个里面的内容就可用了inodes[parentInodeNumber]
  if(inode.type==UFS_DIRECTORY){
    for (int i = 0; i < DIRECT_PTRS; i++) // 30个指针
    {
      unsigned int data_block_index = inode.direct[i];
      if (data_block_index == 0) {
        continue;
      }
      dir_ent_t entrieList[entries_num];
      disk->readBlock(data_block_index, entrieList);
      // 读出了整个entrieList，
      for(int j = 0; j < entries_num;j++){
        if(entrieList[i].inum!=-1){
          if(strcmp(entrieList[i].name,name.c_str())==0 && entrieList[i].inum!=-1){
            return entrieList[i].inum;
          }
        }
      }
    }
  }else{
    return -EINVALIDINODE;
  }
  return -ENOTFOUND;
}

int getOffsetOfBitmapByInodeNumber(int inodeNumber){
  return inodeNumber % (UFS_BLOCK_SIZE * 8);
}
int getBitmapBlockIndexByInodeNumber(int inodeNumber){
  return inodeNumber / (UFS_BLOCK_SIZE * 8);
}
int getInodeBlockIndexByInodeNumber(int inodeNumber){
  return inodeNumber / (UFS_BLOCK_SIZE / sizeof(inode_t));
}
int getInodeBlockOffsetByInodeNumber(int inodeNumber){
  return inodeNumber % (UFS_BLOCK_SIZE / sizeof(inode_t));
}

int LocalFileSystem::stat(int inodeNumber, inode_t *inode) {
  union {
		inode_t inodeBuf[UFS_BLOCK_SIZE / sizeof(inode_t)];
		unsigned char byteBuf[UFS_BLOCK_SIZE];
	};
  super_t super;
  this->readSuperBlock(&super);
  // 首先检查编号
  if(checkInodeIsValid(&super,inodeNumber) != 0){
    return -EINVALIDINODE;
  }
  // 查看bitmap
  int bitmapBlockIndex = getBitmapBlockIndexByInodeNumber(inodeNumber);
  disk->readBlock(super.inode_bitmap_addr + bitmapBlockIndex, byteBuf);
  int offsetOfBitmap = getOffsetOfBitmapByInodeNumber(inodeNumber);

  // TODO:这块是否正确
  if (0 == (byteBuf[(offsetOfBitmap / 8)] & (1 << (offsetOfBitmap & 7)))) {
		return -EINVALIDINODE;
	}
  // 读取inode
  disk->readBlock(super.inode_region_addr + getInodeBlockIndexByInodeNumber(inodeNumber), inodeBuf);
  memcpy(inode, &inodeBuf[getInodeBlockOffsetByInodeNumber(inodeNumber)], sizeof(inode_t));
  return 0;
}

int LocalFileSystem::read(int inodeNumber, void *buffer, int size) {
  if (!buffer || (size <= 0)) {
		return -EINVALIDSIZE;
	}
  inode_t inode;
  if(this->stat(inodeNumber,&inode)==-EINVALIDINODE){
    return -EINVALIDINODE;
  }

  // if(inode.type==UFS_DIRECTORY){
  //   return -EINVALIDINODE;
  // }
  // 读数据，我们只能读一个block
  // 先把数据读到block，再从block（4K）里边复制到buffer（2K)
  size = min(size,inode.size);

  int remaining  = size;
  int bytesRead  = 0;
  unsigned char temp_buffer[UFS_BLOCK_SIZE];
  for (int i = 0; i < DIRECT_PTRS; i++) // 遍历inode指向的所有块
  {
    unsigned int data_block_index = inode.direct[i];

    if (data_block_index == 0){
      continue;
    }

    this->disk->readBlock(data_block_index,temp_buffer);

    int copy_size = std::min(UFS_BLOCK_SIZE, remaining); // 确保不会溢出 buffer
    memcpy(static_cast<unsigned char*>(buffer) + bytesRead, temp_buffer, copy_size);
    bytesRead += copy_size;
    remaining -= copy_size;

    if (remaining <= 0) {
        break; // 读取足够数据后停止
    }
  }
  return bytesRead;
}

// 创建一个test.txt
// 1. 创建一个inode，存文件名称，文件类型，调用的是create函数，创建元数据
// 2. 通过这个inode编号，找到inode，往inode指向的区域写数据，调用的是write函数

int LocalFileSystem::create(int parentInodeNumber, int type, string name) { 
  union {
		unsigned char byteBuf[UFS_BLOCK_SIZE];
		inode_t inodeBuf[(UFS_BLOCK_SIZE / sizeof(inode_t))];
		dir_ent_t entryBuf[(UFS_BLOCK_SIZE / sizeof(dir_ent_t))];
	};
  if(name.empty() || name.size()>=DIR_ENT_NAME_SIZE){
    return -EINVALIDNAME;
  }
  if (!name.compare(".") || !name.compare("..")) {
		return -EINVALIDNAME;
	}
  inode_t parent_inode;
  if(this->stat(parentInodeNumber,&parent_inode)==EINVALIDINODE){
    return -EINVALIDINODE;
  }

  if (parent_inode.type != UFS_DIRECTORY) {
		return -EINVALIDINODE;
	}

  // 首先查找有没有
  int inode_index = lookup(parentInodeNumber, name);
  if(inode_index>=0){
    inode_t inode;
    if (stat(inode_index, &inode)!=0){
      return -EINVALIDINODE;
    }
    if (inode.type != type){
      return -EINVALIDTYPE;
    }
		return inode_index;
  }else if(-ENOTFOUND != inode_index){
    return inode_index;
  }
  
  super_t super;
  readSuperBlock(&super);

	disk->beginTransaction();

	int indexOfData = 0;
	int bQuit = 0;
	if (UFS_DIRECTORY == type) {
		for (int i = 0; 
			(!bQuit && (i < super.data_bitmap_len)); 
			i++) {
			disk->readBlock((super.data_bitmap_addr + i), byteBuf);
			for (int j = 0; 
				(!bQuit && (j < UFS_BLOCK_SIZE)); 
				j++) {
				for (int k = 0; 
					k < 8; 
					k++, indexOfData++) {
					unsigned int const msk = (1 << k);
					if (!(byteBuf[j] & msk)) {
						byteBuf[j] |= msk;
						bQuit = 1;
						disk->writeBlock((super.data_bitmap_addr + i), byteBuf);
						break;
					}
				}
			}
		}

		if (!bQuit || (super.num_data <= indexOfData)) {
			disk->rollback();
			return -ENOTENOUGHSPACE;
		}
	}

	int inumNew = 0;
	bQuit = 0;
	for (int i = 0; 
		(!bQuit && (i < super.inode_bitmap_len)); 
		i++) {
		disk->readBlock((super.inode_bitmap_addr + i), byteBuf);
		for (int j = 0; 
			(!bQuit && (j < UFS_BLOCK_SIZE)); 
			j++) {
			for (int k = 0; 
				k < 8; 
				k++, inumNew++) {
				unsigned int const msk = (1 << k);
				if (!(byteBuf[j] & msk)) {
					byteBuf[j] |= msk;
					bQuit = 1;
					disk->writeBlock((super.inode_bitmap_addr + i), byteBuf);
					break;
				}
			}
		}
	}

	if (!bQuit || (super.num_inodes <= inumNew)) {
		disk->rollback();
		return -ENOTENOUGHSPACE;
	}

	if (UFS_DIRECTORY == type) {
		memset(byteBuf, 0, UFS_BLOCK_SIZE);
		entryBuf[0].name[0] = '.';
		entryBuf[0].name[1] = 0;
		entryBuf[0].inum = inumNew;
		entryBuf[1].name[0] = '.';
		entryBuf[1].name[1] = '.';
		entryBuf[1].name[2] = 0;
		entryBuf[1].inum = parentInodeNumber;
		for (int i = 2; 
			(i < (int)(sizeof(entryBuf)/sizeof(dir_ent_t))); 
			i++) {
			entryBuf[i].inum = -1;
		}
		disk->writeBlock((super.data_region_addr + indexOfData), byteBuf);
	}

	int cntOfBlock = (parent_inode.size + (UFS_BLOCK_SIZE - 1)) / UFS_BLOCK_SIZE;
	int indexOfEntry = 0;
	bQuit = 0;

	for (int i = 0; 
		(!bQuit && (i < cntOfBlock)); 
		i++) {
		disk->readBlock((parent_inode.direct[i]), byteBuf);
		for (int j = 0; 
			(j < (int)(sizeof(entryBuf)/sizeof(dir_ent_t))); 
			j++, indexOfEntry++) {
			if (parent_inode.size <= (int)(sizeof(dir_ent_t) * indexOfEntry)) {
				entryBuf[j].inum = -1;
			}
			if (-1 == entryBuf[j].inum) {
				bQuit = 1;
				strcpy(entryBuf[j].name, name.c_str());
				entryBuf[j].inum = inumNew;
				disk->writeBlock((parent_inode.direct[i]), byteBuf);
				break;
			}
		}
	}

	if (!bQuit) {
		if (DIRECT_PTRS <= cntOfBlock) {
			disk->rollback();
			return -ENOTENOUGHSPACE;
		}

		int indexDataNew = 0;
		for (int i = 0; 
			(!bQuit && (i < super.data_bitmap_len)); 
			i++) {
			disk->readBlock((super.data_bitmap_addr + i), byteBuf);
			for (int j = 0; 
				(!bQuit && (j < UFS_BLOCK_SIZE)); 
				j++) {
				for (int k = 0; 
					k < 8; 
					k++, indexDataNew++) {
					unsigned int const msk = (1 << k);
					if (!(byteBuf[j] & msk)) {
						byteBuf[j] |= msk;
						bQuit = 1;
						disk->writeBlock((super.data_bitmap_addr + i), byteBuf);
						break;
					}
				}
			}
		}

		if (!bQuit || (super.num_data <= indexDataNew)) {
			disk->rollback();
			return -ENOTENOUGHSPACE;
		}

		memset(byteBuf, 0, UFS_BLOCK_SIZE);
		strcpy(entryBuf[0].name, name.c_str());
		entryBuf[0].inum = inumNew;
		for (int i = 1; 
			i < (int)(sizeof(entryBuf) / sizeof(dir_ent_t)); 
			i++) {
			entryBuf[i].inum = -1;
		}
		disk->writeBlock((super.data_region_addr + indexDataNew), byteBuf);
		parent_inode.size = (1 + indexOfEntry) * sizeof(dir_ent_t);
		int indexOfBlock = (parentInodeNumber / (UFS_BLOCK_SIZE / sizeof(inode_t)));
		int offsetOfBlock = (parentInodeNumber % (UFS_BLOCK_SIZE / sizeof(inode_t)));
		disk->readBlock((super.inode_region_addr + indexOfBlock), byteBuf);
		inodeBuf[offsetOfBlock].size = parent_inode.size;
		inodeBuf[offsetOfBlock].direct[cntOfBlock++] = (super.data_region_addr + indexDataNew);
		disk->writeBlock((super.inode_region_addr + indexOfBlock), byteBuf);
	}
	else if (parent_inode.size < (int)((1 + indexOfEntry) * sizeof(dir_ent_t))) {
		parent_inode.size = (1 + indexOfEntry) * sizeof(dir_ent_t);
		int const indexOfBlock = (parentInodeNumber / (UFS_BLOCK_SIZE / sizeof(inode_t)));
		int const offsetOfBlock = (parentInodeNumber % (UFS_BLOCK_SIZE / sizeof(inode_t)));
		disk->readBlock((super.inode_region_addr + indexOfBlock), byteBuf);
		inodeBuf[offsetOfBlock].size = parent_inode.size;
		disk->writeBlock((super.inode_region_addr + indexOfBlock), byteBuf);
	}

	int const indexOfBlock = (inumNew / (UFS_BLOCK_SIZE / sizeof(inode_t)));
	int const offsetOfBlock = (inumNew % (UFS_BLOCK_SIZE / sizeof(inode_t)));
	disk->readBlock((super.inode_region_addr + indexOfBlock), byteBuf);
	inodeBuf[offsetOfBlock].type = type;

	if (UFS_DIRECTORY == type) {
		inodeBuf[offsetOfBlock].size = (sizeof(dir_ent_t) << 1);
		inodeBuf[offsetOfBlock].direct[0] = (super.data_region_addr + indexOfData);
	}
	else {
		inodeBuf[offsetOfBlock].size = 0;
	}
	disk->writeBlock((super.inode_region_addr + indexOfBlock), byteBuf);

	disk->commit();
  return inumNew;
}

int getAndSetFreeInodeNumIndex(super_t *super, unsigned char *inodeBitMap){
  for(int i=0;i<super->inode_bitmap_len * UFS_BLOCK_SIZE;i++){
    // 遍历每一个找到inode节点
    for(int j=0;j<8;j++){
      if(((inodeBitMap[i]>>(j))&1)==0){
        inodeBitMap[i] |= (1<<j);
        return i*8 + j;
      }
    }
  }
  return -1;
}

int getAndSetFreeDataBlockIndex(super_t *super, unsigned char *data_bit_map){
  for(int i=0;i<super->data_bitmap_len * UFS_BLOCK_SIZE;i++){
    // 遍历每一个找到空闲块
    for(int j=0;j<8;j++){
      if(((data_bit_map[i]>>(j))&1)==0){
        data_bit_map[i] |= (1<<j);
        return i*8 + j;
      }
    }
  }
  return -1;
}

int LocalFileSystem::write(int inum2, const void *byteBuf2, int size) {
	union {
		unsigned char byteBuf[UFS_BLOCK_SIZE];
		inode_t inodeBuf[(UFS_BLOCK_SIZE / sizeof(inode_t))];
		dir_ent_t entryBuf[(UFS_BLOCK_SIZE / sizeof(dir_ent_t))];
	};

  if (byteBuf2 == nullptr || size <= 0) {
      std::cerr << "Invalid byteBuf2 or size." << std::endl;
      return -EINVALIDSIZE;
  }
	
	int cntOfBlock = (size + UFS_BLOCK_SIZE - 1) / UFS_BLOCK_SIZE;
	if (DIRECT_PTRS < cntOfBlock) {
		return -ENOTENOUGHSPACE;
	}

    inode_t inode;
    if (stat(inum2, &inode)) {
        return -EINVALIDINODE;
    }

	if (inode.type != UFS_REGULAR_FILE) {
		return -EWRITETODIR;
	}

	super_t super;
	readSuperBlock(&super);

	disk->beginTransaction();

	int numBlockNow = (inode.size + UFS_BLOCK_SIZE - 1) / UFS_BLOCK_SIZE;
	int bQuit = 0;
	if (numBlockNow < cntOfBlock) {
		for (int x = numBlockNow; 
			x < cntOfBlock; 
			x++) {
			int indexOfData = 0;
			bQuit = 0;
			for (int i = 0; 
				(!bQuit && (i < super.data_bitmap_len)); 
				i++) {
				disk->readBlock((super.data_bitmap_addr + i), byteBuf);
				for (int j = 0; 
					(!bQuit && (j < UFS_BLOCK_SIZE)); 
					j++) {
					for (int k = 0; 
						k < 8; k++, 
						indexOfData++) {
						unsigned int const msk = (1 << k);
						if (!(byteBuf[j] & msk)) {
							byteBuf[j] |= msk;
							bQuit = 1;
							disk->writeBlock((super.data_bitmap_addr + i), byteBuf);
							break;
						}
					}
				}
			}

			if (!bQuit || (super.num_data <= indexOfData)) {
				disk->rollback();
				return -ENOTENOUGHSPACE;
			}
			inode.direct[x] = (super.data_region_addr + indexOfData);
		}
	}
	else if (cntOfBlock < numBlockNow) {
		for (int x = cntOfBlock; 
			x < numBlockNow; 
			x++) {
			int const indexOfBlock = ((inode.direct[x] - super.data_region_addr) / (UFS_BLOCK_SIZE * 8));
			int const offsetOfBlock = ((inode.direct[x] - super.data_region_addr) % (UFS_BLOCK_SIZE * 8));
			disk->readBlock((super.data_bitmap_addr + indexOfBlock), byteBuf);
			byteBuf[(offsetOfBlock / 8)] &= ~(1 << (offsetOfBlock & 7));
			disk->writeBlock((super.data_bitmap_addr + indexOfBlock), byteBuf);
			inode.direct[x] = -1;
		}
	}

	inode.size = size;
	int inumNew = (inum2 / (UFS_BLOCK_SIZE / sizeof(inode_t)));
	disk->readBlock((super.inode_region_addr + inumNew), byteBuf);
	memcpy(&inodeBuf[(inum2 % (UFS_BLOCK_SIZE / sizeof(inode_t)))], &inode, sizeof(inode_t));
	disk->writeBlock((super.inode_region_addr + inumNew), byteBuf);

	int uncopyBytes = size;
	unsigned char* ptrBuf = (unsigned char*)(byteBuf2);

	for (int x = 0; 
		(uncopyBytes && (x < cntOfBlock)); 
		x++) {
		if (UFS_BLOCK_SIZE <= uncopyBytes) {
			disk->writeBlock((inode.direct[x]), ptrBuf);
			uncopyBytes -= UFS_BLOCK_SIZE;
			ptrBuf += UFS_BLOCK_SIZE;
		}
		else {
			memcpy(byteBuf, ptrBuf, uncopyBytes);
			disk->writeBlock((inode.direct[x]), byteBuf);
			uncopyBytes = 0;
			break;
		}
	}

	disk->commit();
  return size;
}


int LocalFileSystem::unlink(int parentInodeNumber, string name) {
  union {
    unsigned char byteBuf[UFS_BLOCK_SIZE];
    inode_t inodeBuf[(UFS_BLOCK_SIZE / sizeof(inode_t))];
    dir_ent_t entryBuf[(UFS_BLOCK_SIZE / sizeof(dir_ent_t))];
  };
  if(name=="."||name==".."){
    return -EUNLINKNOTALLOWED;
  }
  if(name==""){
    return -EINVALIDNAME;
  }
  inode_t pinum;
  if (stat(parentInodeNumber, &pinum)) {
      return -EINVALIDINODE;
  }

  if ((UFS_DIRECTORY != pinum.type) || (pinum.size < (int)(sizeof(dir_ent_t) * 2))) { // 至少两个目录项
		return -EINVALIDINODE;
	}
  // 目录的inode size存储的所有目录项的大小
  super_t super;
	readSuperBlock(&super);
  disk->beginTransaction();

	int inumNew = -1;
	int indexOfEntry = 0;
	int bQuit = 0;
	int cntOfBlock = ((pinum.size + UFS_BLOCK_SIZE - 1) / UFS_BLOCK_SIZE);

	unsigned char* pEnts = new unsigned char[((UFS_BLOCK_SIZE * cntOfBlock))];
	int countOfEntries = (pinum.size / sizeof(dir_ent_t));

	for (int i = 0; (i < cntOfBlock); i++) {
		disk->readBlock((pinum.direct[i]), (pEnts + (i * UFS_BLOCK_SIZE)));
	}

	dir_ent_t* const pEntry = (dir_ent_t*)(pEnts);
	for (indexOfEntry = 0; 
		indexOfEntry < countOfEntries; 
		indexOfEntry++) {
		if ((-1 != pEntry[indexOfEntry].inum) 
			&& !name.compare(pEntry[indexOfEntry].name)) {
			inumNew = pEntry[indexOfEntry].inum;
			break;
		}
	}

	if ((countOfEntries <= indexOfEntry) 
		|| (inumNew < 0)) {
		delete[]pEnts;
		disk->rollback();
		return -EINVALIDNAME;
	}

	inode_t inode2;
	if (stat(inumNew, &inode2)) {
		disk->rollback();
		return -EINVALIDINODE;
	}

	if (UFS_DIRECTORY == inode2.type) {
		int cnt_sblk = (inode2.size + UFS_BLOCK_SIZE - 1) / UFS_BLOCK_SIZE;
		bQuit = 0;
		int idx_sentry = 0;

		for (int i = 0; 
			(!bQuit && (i < cnt_sblk)); 
			i++) {
			disk->readBlock((inode2.direct[i]), byteBuf);
			for (int j = 0; 
				j < (int)(sizeof(entryBuf) / sizeof(dir_ent_t)); 
				j++, idx_sentry++) {
				if (inode2.size <= (int)(sizeof(dir_ent_t) * idx_sentry)) {
					break;
				}
				if ((-1 != entryBuf[j].inum) 
					&& strcmp(entryBuf[j].name, ".")
					&& strcmp(entryBuf[j].name, "..")) {
					bQuit = 1;
					break;
				}
			}
		}

		if (bQuit) {
			disk->rollback();
			return -EDIRNOTEMPTY;
		}
	}

	countOfEntries--;
	pinum.size = (sizeof(dir_ent_t) * countOfEntries);
	if (indexOfEntry != countOfEntries) {
		memmove((pEntry + indexOfEntry), 
			(pEntry + indexOfEntry + 1), 
			sizeof(dir_ent_t) * (countOfEntries - indexOfEntry));
	}
	pEntry[countOfEntries].inum = -1;
	int const cntOfBlockBefore = cntOfBlock;
	cntOfBlock = ((pinum.size + UFS_BLOCK_SIZE - 1) / UFS_BLOCK_SIZE);
	for (int i = 0; 
		(i < cntOfBlock); 
		i++) {
		disk->writeBlock((pinum.direct[i]), (pEnts + (i * UFS_BLOCK_SIZE)));
	}
	delete[]pEnts;
	pEnts = 0;

	if (cntOfBlockBefore != cntOfBlock) {
		int const indexOfBlock = ((pinum.direct[cntOfBlock] - super.data_region_addr) / (UFS_BLOCK_SIZE * 8));
		int const offsetOfBlock = ((pinum.direct[cntOfBlock] - super.data_region_addr) % (UFS_BLOCK_SIZE * 8));
		disk->readBlock((super.data_bitmap_addr + indexOfBlock), byteBuf);
		byteBuf[(offsetOfBlock / 8)] &= ~(1 << (offsetOfBlock & 7));
		disk->writeBlock((super.data_bitmap_addr + indexOfBlock), byteBuf);
		pinum.direct[cntOfBlock] = -1;
	}

	int const indexOfTmp = (parentInodeNumber / (UFS_BLOCK_SIZE / sizeof(inode_t)));
	disk->readBlock((super.inode_region_addr + indexOfTmp), byteBuf);
	memcpy(&inodeBuf[(parentInodeNumber % (UFS_BLOCK_SIZE / sizeof(inode_t)))], &pinum, sizeof(inode_t));
	disk->writeBlock((super.inode_region_addr + indexOfTmp), byteBuf);

	cntOfBlock = (inode2.size + UFS_BLOCK_SIZE - 1) / UFS_BLOCK_SIZE;
	for (int i = 0; 
		i < cntOfBlock; 
		i++) {
		int const indexOfBlock = ((inode2.direct[i] - super.data_region_addr)/ (UFS_BLOCK_SIZE * 8));
		int const offsetOfBlock = ((inode2.direct[i] - super.data_region_addr) % (UFS_BLOCK_SIZE * 8));
		disk->readBlock((super.data_bitmap_addr + indexOfBlock), byteBuf);
		byteBuf[(offsetOfBlock / 8)] &= ~(1 << (offsetOfBlock & 7));
		disk->writeBlock((super.data_bitmap_addr + indexOfBlock), byteBuf);
	}
	inode2.size = 0;

	int const indexOfBitmap = (inumNew / (UFS_BLOCK_SIZE * 8));
	int const offsetOfBitmap = (inumNew % (UFS_BLOCK_SIZE * 8));
	disk->readBlock((super.inode_bitmap_addr + indexOfBitmap), byteBuf);
	byteBuf[(offsetOfBitmap / 8)] &= ~(1 << (offsetOfBitmap & 7));
	disk->writeBlock((super.inode_bitmap_addr + indexOfBitmap), byteBuf);
	
	disk->commit();
  return 0;
}

