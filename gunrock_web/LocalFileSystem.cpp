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
    // 通过direct[i]找到对应的数据域
    for (int i = 0; i < DIRECT_PTRS; i++)
    {
      unsigned int data_block_index = inode.direct[i];
      if (data_block_index == 0) {
        continue;
      }
      dir_ent_t entrieList[entries_num];
      disk->readBlock(data_block_index, entrieList);
      // 读出了整个entrieList，
      for(int j = 0; j<entries_num;j++){
        if(entrieList[i].inum!=-1 && (entrieList[i].name,name.c_str())==0){
          return entrieList[i].inum;
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
		return EINVALIDSIZE;
	}
  inode_t inode;
  if(this->stat(inodeNumber,&inode)==EINVALIDINODE){
    return EINVALIDINODE;
  }
  // 读数据，我们只能读一个block
  // 先把数据读到block，再从block（4K）里边复制到buffer（2K)
  size = min(size,inode.size);

  int remaining  = size;
  int bytesRead  = 0;
  char temp_buffer[UFS_BLOCK_SIZE];
  for (int i = 0; i < DIRECT_PTRS; i++) // 遍历inode指向的所有块
  {
    unsigned int data_block_index = inode.direct[i];

    if (data_block_index == 0){
      continue;
    }

    this->disk->readBlock(data_block_index,temp_buffer);

    if(remaining-UFS_BLOCK_SIZE>=0){
      memcpy(buffer + bytesRead,temp_buffer,UFS_BLOCK_SIZE);
      bytesRead += UFS_BLOCK_SIZE;
    }else{
      memcpy(buffer + bytesRead,temp_buffer,remaining);
      bytesRead+=remaining ;
      break;
    }
  }
  return bytesRead;
}

// 创建一个test.txt
// 1. 创建一个inode，存文件名称，文件类型，调用的是create函数，创建元数据
// 2. 通过这个inode编号，找到inode，往inode指向的区域写数据，调用的是write函数

int LocalFileSystem::create(int parentInodeNumber, int type, string name) { 
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
  // 到这里一定是not found
  super_t super;
  readSuperBlock(&super);
	disk->beginTransaction();
  // 开始创建了
  unsigned char data_bit_map[super.data_bitmap_len * UFS_BLOCK_SIZE];
  this->readDataBitmap(&super,data_bit_map);

  // 遍历，从现在direct[i]已经有指向的，找空闲的entirList，然后放进去

  int entries_num = UFS_BLOCK_SIZE / sizeof(dir_ent_t);
  for (int i = 0; i < DIRECT_PTRS; i++)
  {
    unsigned int data_block_index = parent_inode.direct[i];
    if (data_block_index == 0) continue;
    if(checkInodeIsValid(&super,data_block_index)==EINVALIDINODE){
        continue;
    }
    dir_ent_t entrieList[entries_num];
    this->disk->readBlock(data_block_index, entrieList);
    // 读出了整个entrieList，
    for(int j = 0; j<entries_num;j++){
      if(entrieList[i].name[0]=='\0'){
        // 加进去就可以了
        strcpy(entrieList[i].name,name.c_str());
        // 创建一个新的inode了
        // 需要遍历bitmap找一个空的
        unsigned char inodeBitmap[super.inode_bitmap_len * UFS_BLOCK_SIZE]; // 栈，vector（堆）
        readInodeBitmap(&super,inodeBitmap);
        int new_inode_index = getAndSetFreeInodeNumIndex(&super,inodeBitmap);
        entrieList[i].inum = new_inode_index;
        if(entrieList[i].inum==-1){
          return ENOTENOUGHSPACE;
        }
        
        inode_t inodes[super.num_inodes]; // inodes table
        this->readInodeRegion(&super,inodes); // inodes table
        inode_t &new_inode = inodes[entrieList[i].inum];
        new_inode.type = type;
        for (int k = 0; k < DIRECT_PTRS; k++)
        {
          new_inode.direct[k] = 0;
        }
        
        if(type==UFS_DIRECTORY){
          // 找数据域，并且写两个entrie
          int free_data_block_index = getAndSetFreeDataBlockIndex(&super,data_bit_map);
          dir_ent_t new_dir_entrieList[entries_num];
          strcpy(new_dir_entrieList[0].name,".");
          new_dir_entrieList[0].inum = new_inode_index;
          strcpy(new_dir_entrieList[1].name,"..");
          new_dir_entrieList[1].inum = parentInodeNumber;

          for (int k = 2; k < DIRECT_PTRS; k++)
          {
            new_dir_entrieList[i].name[0]='\0';
            new_dir_entrieList[i].inum = -1;
          }
          this->disk->writeBlock(free_data_block_index,new_dir_entrieList);
          this->writeDataBitmap(&super,data_bit_map);
        }

        // update InodeBitmap, InodeRegion
        this->writeInodeBitmap(&super,inodeBitmap);
        this->writeInodeRegion(&super,inodes);
        this->disk->writeBlock(data_block_index, entrieList);
        return entrieList[i].inum;
      }
    }
  }

  return 0;
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

int LocalFileSystem::write(int inodeNumber, const void *buffer, int size) {
  if(size>=MAX_FILE_SIZE || size == 0 || buffer == nullptr){
    return -EINVALIDSIZE;
  }
  int block_num = size / UFS_BLOCK_SIZE;  // 需要的块数量
  if( size % UFS_BLOCK_SIZE != 0){
    block_num++;
  }
  if(block_num>=DIRECT_PTRS){
    return -ENOTENOUGHSPACE;
  }
  inode_t inode;
  if (stat(inodeNumber, &inode)) {
      return -EINVALIDINODE;
  }

  if(inode.type==UFS_DIRECTORY){
      return EINVALIDTYPE;
  }

  super_t super;
  this->readSuperBlock(&super);
  // 通过data_regin的bitmap看看有哪些空闲块，然后放进去
  unsigned char data_bit_map[super.data_bitmap_len * UFS_BLOCK_SIZE];
  this->readDataBitmap(&super,data_bit_map);
  // 走到下面一定可以放得下
  int i;
  for(i = 0; i < block_num; i++){
    if(inode.direct[i] != 0){ // override,because databitMap is 1, not to update,directly override
      this->disk->writeBlock(super.data_region_addr + inode.direct[i],(char *)buffer + i * UFS_BLOCK_SIZE);
    }else{
      int free_data_block_index = getAndSetFreeDataBlockIndex(&super,data_bit_map);
      if(free_data_block_index!=-1){
        this->disk->writeBlock(super.data_region_addr + free_data_block_index,(char *)buffer + i * UFS_BLOCK_SIZE);
      }else{
        break;
      }
    }
  }
  // 将i后边的所有的都释放掉
  for(; i<DIRECT_PTRS; i++){
    if(inode.direct[i] != 0){
      // 释放
      int byte_index = inode.direct[i] / 8;
      int bit_index = inode.direct[i] % 8;
      data_bit_map[byte_index]  = data_bit_map[byte_index] & ~(1<<bit_index);
    }
  }
  
  this->writeDataBitmap(&super,data_bit_map); // write complete,writeBitmap
  return min(size,i * UFS_BLOCK_SIZE);
}

int LocalFileSystem::unlink(int parentInodeNumber, string name) {
  if(name=="."||name==".."){
    return -EUNLINKNOTALLOWED;
  }
  if(name==""){
    return -EINVALIDNAME;
  }
  inode_t inode;
  if (stat(parentInodeNumber, &inode)) {
      return -EINVALIDINODE;
  }

  int entries_num = UFS_BLOCK_SIZE / sizeof(dir_ent_t);
  if ((UFS_DIRECTORY != inode.type) || (inode.size < (int)(sizeof(dir_ent_t) * 2))) { // 至少两个目录项
		return -EINVALIDINODE;
	}
  // 目录的inode size存储的所有目录项的大小
  super_t super;
	readSuperBlock(&super);
  disk->beginTransaction();

  // 通过direct[i]找到对应的数据域
  int inode_num = -1;
  for (int i = 0; i < DIRECT_PTRS; i++)
  {
    unsigned int data_block_index = inode.direct[i];
    dir_ent_t entrieList[entries_num];
    disk->readBlock(data_block_index, entrieList);
    // 读出了整个entrieList，
    for(int j = 0; j < entries_num; j++){
      if(strcmp(entrieList[i].name,name.c_str())==0){
        inode_num = entrieList[i].inum;
        break;
      }
    }
    if(inode_num!=-1){
      break;
    }
  }
  if(inode_num==-1){
    return -EINVALIDNAME;
  }
  // 是不是需要判断，这个name对应的文件是否是目录，如果是目录，需要判断是否非空
  inode_t name_inode;
  if(this->stat(inode_num,&name_inode)!=0){
    return -EINVALIDINODE;
  }

  
  // TODO:unlink是否需要更新bitmap
  if(name_inode.type==UFS_REGULAR_FILE){
    // 如何从entrieList中删除
    entrieList[i].inum=-1;
    entrieList[i].name[0]='\0';
    disk->writeBlock(data_block_index, entrieList);
  }else{
    // 目录
    // 还需要判断目录是否为空
    if(name_inode.size>0){
      return EDIRNOTEMPTY;
    }else{
        entrieList[i].inum=-1;
        entrieList[i].name[0]='\0';
        disk->writeBlock(data_block_index, entrieList);
    }
  }
  return 0;
}

