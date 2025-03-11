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
  // 读超级block
  this->disk->readBlock(0,super);
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



bool checkInodeIsValid(super_t *super,int parentInodeNumber,unsigned char * inode_bit_map){
  // 判断父节点inode是否有效
  if(parentInodeNumber>=super->num_inodes){
    return EINVALIDINODE;
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
  super_t super;
  this->readSuperBlock(&super);

  // 读bitmap看看是否有效
  unsigned char inode_bit_map[super.inode_bitmap_len * UFS_BLOCK_SIZE];
  this->readInodeBitmap(&super,inode_bit_map);
  if(checkInodeIsValid(&super,parentInodeNumber,inode_bit_map)==EINVALIDINODE){
    return EINVALIDINODE;
  }
  
  // 3. 找到inode的table，然后找到parentInodeNumber对应的inode
  inode_t inodes[super.num_inodes]; // inodes table
  this->readInodeRegion(&super,inodes);
  
  int entries_num = UFS_BLOCK_SIZE / sizeof(dir_ent_t);
  // 找这个里面的内容就可用了inodes[parentInodeNumber]
  if(inodes[parentInodeNumber].type==UFS_DIRECTORY){
    // 通过direct[i]找到对应的数据域
    for (int i = 0; i < DIRECT_PTRS; i++)
    {
      unsigned int data_block_index = inodes[parentInodeNumber].direct[i];
      if (data_block_index == 0) continue;
      // 一个block可以放多少entries
      if(checkInodeIsValid(&super,data_block_index,inode_bit_map)==EINVALIDINODE){
          continue;
      }
      dir_ent_t entrieList[entries_num];
      disk->readBlock(data_block_index, entrieList);
      // 读出了整个entrieList，
      for(int j = 0; j<entries_num;j++){
        if(strcmp(entrieList[i].name,name.c_str())==0){
          return entrieList[i].inum;
        }
      }
    }
  }else{
    return EINVALIDINODE;
  }
  return 0;
}

// 什么情况下会去调用stat这个函数，
// stat函数的作用，就是根据inodeNumber找到一个inode

// 找一个空的inode

int LocalFileSystem::stat(int inodeNumber, inode_t *inode) {
  super_t super;
  this->readSuperBlock(&super);
  // TODO:读inode是否有效，和bitmap有关系吗？
  unsigned char inode_bit_map[super.inode_bitmap_len * UFS_BLOCK_SIZE];
  this->readInodeBitmap(&super,inode_bit_map);
  
  if(checkInodeIsValid(&super,inodeNumber,inode_bit_map)==EINVALIDINODE){
    return EINVALIDINODE;
  }
  // 首先读到inode表
  inode_t inodes[super.num_inodes]; // inodes table
  this->readInodeRegion(&super,inodes); // inodes table
  // 从inodes表中找到
  *inode = inodes[inodeNumber];
  return 0;
}

int LocalFileSystem::read(int inodeNumber, void *buffer, int size) {
  super_t super;
  this->readSuperBlock(&super);

  // 读inode是否有效
  unsigned char inode_bit_map[super.inode_bitmap_len * UFS_BLOCK_SIZE];
  this->readInodeBitmap(&super,inode_bit_map);

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
    if (data_block_index == 0) continue;
    // 一个block可以放多少entries
    if(checkInodeIsValid(&super,data_block_index,inode_bit_map)==EINVALIDINODE){
        continue;
    }
    // 有效
    this->disk->readBlock(super.data_region_addr+data_block_index,temp_buffer);

    if(left-UFS_BLOCK_SIZE>=0){
      memcpy(buffer + bytesRead,temp_buffer,UFS_BLOCK_SIZE);
      bytesRead+=UFS_BLOCK_SIZE;
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
  super_t super;
  this->readSuperBlock(&super);
  
  // 读bitmap看看是否有效
  unsigned char inode_bit_map[super.inode_bitmap_len * UFS_BLOCK_SIZE];
  this->readInodeBitmap(&super,inode_bit_map);
  if(checkInodeIsValid(&super,parentInodeNumber,inode_bit_map)==EINVALIDINODE){
    return EINVALIDINODE;
  }
  if(checkInodeIsExist(&super,parentInodeNumber,inode_bit_map)==0){
    return EINVALIDINODE;
  }
  // Inode有效
  // 3. 找到inode的table，然后找到parentInodeNumber对应的inode
  inode_t inodes[super.num_inodes]; // inodes table

  // TODO:
  if(inodes[parentInodeNumber].type==UFS_DIRECTORY){
    // 通过direct[i]找到对应的数据域
    for (int i = 0; i < DIRECT_PTRS; i++) // 遍历30个节点，
    {
      unsigned int data_block_index = inodes[parentInodeNumber].direct[i];
      if (data_block_index == 0) continue;
      // 一个block可以放多少entries
      if(checkInodeIsValid(&super,data_block_index,inode_bit_map)==EINVALIDINODE){
          continue;
      }
      dir_ent_t entrieList[entries_num];
      disk->readBlock(data_block_index, entrieList);
      // 读出了整个entrieList，
      for(int j = 0; j<entries_num;j++){
        if(strcmp(entrieList[i].name,name.c_str())==0){
          return entrieList[i].inum;
        }
      }
    }
  }else{
    return EINVALIDINODE;
  }

  return 0;
}


int getFreeDataBlockIndex(super_t *super, unsigned char *data_bit_map){
  for(int i=0;i<super->data_bitmap_len * UFS_BLOCK_SIZE;i++){
    // 遍历每一个找到空闲块
    for(int j=0;j<8;j++){
      if(((data_bit_map[i]>>(j))&1)==0){
        return i*8 + j;
      }
    }
  }
  return -1;
}

int LocalFileSystem::write(int inodeNumber, const void *buffer, int size) {
  if(size>=MAX_FILE_SIZE || size ==0){
    return EINVALIDSIZE;
  }
  super_t super;
  this->readSuperBlock(&super);
  
  // 读bitmap看看是否有效
  unsigned char inode_bit_map[super.inode_bitmap_len * UFS_BLOCK_SIZE];
  this->readInodeBitmap(&super,inode_bit_map);
  if(checkInodeIsValid(&super,inodeNumber,inode_bit_map)==EINVALIDINODE){
    return EINVALIDINODE;
  }
  // 读inodes表，读到inode
  inode_t inodes[super.num_inodes]; // inodes table
  this->readInodeRegion(&super,inodes);
  inode_t inode = inodes[inodeNumber];

  if((checkInodeIsExist(&super,inodeNumber,inode_bit_map))==1){ // 等于1表示inode存在
    if(inode.type==UFS_DIRECTORY){
      return EINVALIDTYPE;
    }
  }
  // 从中找出一个空闲块
  int block_num = size / UFS_BLOCK_SIZE;
  if( size % UFS_BLOCK_SIZE != 0){
    block_num++;
  }
  // TODO:还需要写超过一个块的
  // 计算需要写几个块
  // 
  if(block_num>=DIRECT_PTRS){
    return EINVALIDSIZE;
  }
  // 通过data_regin的bitmap看看有哪些空闲块，然后放进去
  unsigned char data_bit_map[super.data_bitmap_len * UFS_BLOCK_SIZE];
  this->readDataBitmap(&super,data_bit_map);
  // 走到下面一定可以放得下
  this->disk->beginTransaction();
  for(int i=0;i < block_num; i++){
    if(inode.direct[i] != 0){ // override,because databitMap is 1, not to update,directly override
      this->disk->writeBlock(super.data_region_addr + inode.direct[i],(char *)buffer + i * UFS_BLOCK_SIZE);
    }else{
      int free_data_block_index = getFreeDataBlockIndex(&super,data_bit_map);
      if(free_data_block_index!=-1){
        // set data bit map
        int ii = free_data_block_index / 8;
        int jj = free_data_block_index % 8;
        data_bit_map[ii] = data_bit_map[ii] | 1 << jj; // 100
        this->disk->writeBlock(super.data_region_addr + free_data_block_index,(char *)buffer + i * UFS_BLOCK_SIZE);
      }else{
        this->disk->rollback();
        return EINVALIDSIZE;
      }
    }
  }
  this->writeDataBitmap(&super,data_bit_map); // write complete,writeBitmap
  this->disk->commit();
}

int LocalFileSystem::unlink(int parentInodeNumber, string name) {
  if(name=="."||name==".."){
    return EUNLINKNOTALLOWED;
  }
  if(name==""){
    return EINVALIDNAME;
  }
  super_t super;
  this->readSuperBlock(&super);

  // 读bitmap看看是否有效
  unsigned char inode_bit_map[super.inode_bitmap_len * UFS_BLOCK_SIZE];
  this->readInodeBitmap(&super,inode_bit_map);
  if(checkInodeIsValid(&super,parentInodeNumber,inode_bit_map)==EINVALIDINODE){
    return EINVALIDINODE;
  }
  if(checkInodeIsExist(&super,parentInodeNumber,inode_bit_map)==0){
    return EINVALIDINODE;
  }

  // 找到inode的信息
  inode_t inodes[super.num_inodes]; // inodes table
  this->readInodeRegion(&super,inodes);

  int entries_num = UFS_BLOCK_SIZE / sizeof(dir_ent_t);
  // 找这个里面的内容就可用了inodes[parentInodeNumber]
  if(inodes[parentInodeNumber].type==UFS_DIRECTORY){
    // 通过direct[i]找到对应的数据域
    for (int i = 0; i < DIRECT_PTRS; i++)
    {
      unsigned int data_block_index = inodes[parentInodeNumber].direct[i];
      if (data_block_index == 0) continue;

      if(checkInodeIsValid(&super,data_block_index,inode_bit_map)==EINVALIDINODE){
          continue;
      }
      if(checkInodeIsExist(&super,data_block_index,inode_bit_map)==0){
        continue;
      }
      dir_ent_t entrieList[entries_num];
      disk->readBlock(data_block_index, entrieList);
      // 读出了整个entrieList，
      for(int j = 0; j < entries_num; j++){
        if(strcmp(entrieList[i].name,name.c_str())==0){
          // 这里名字相等了，需要unlink
          // 是不是需要判断，这个name对应的文件是否是目录，如果是目录，需要判断是否非空
          inode_t name_inode;
          this->stat(entrieList[i].inum,&name_inode); // TODO:如果无效就unlink

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
        }
      }
    }
  }else{
    return EINVALIDINODE;
  }
  return 0;
}

