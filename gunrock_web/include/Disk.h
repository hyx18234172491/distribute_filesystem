#ifndef _DISK_H_
#define _DISK_H_

#include <string>
#include <deque>

struct UndoRecord {
  int blockNumber;
  unsigned char *blockData;
};

class Disk {
 public:
  Disk(std::string imageFile, int blockSize);
  void readBlock(int blockNumber, void *buffer);
  void writeBlock(int blockNumber, void *buffer);
  int numberOfBlocks();

  void beginTransaction();  // 开始事务
  void commit();  // 提交事务
  void rollback();  // 回滚事务

  // 一个事务里边的所有程序代码，要么全部执行成功，要么全部不执行
  // 如果成功，就commit
  // 如果不成功，就执行回滚
  
 private:
  std::string imageFile;
  int blockSize;
  int imageFileSize;
  bool isInTransaction;
  std::deque<struct UndoRecord> undoLog;
};

#endif
