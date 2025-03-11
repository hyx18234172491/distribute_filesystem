#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sstream>
#include <iostream>
#include <map>
#include <string>
#include <algorithm>

#include "DistributedFileSystemService.h"
#include "ClientError.h"
#include "ufs.h"
#include "WwwFormEncodedDict.h"

using namespace std;

DistributedFileSystemService::DistributedFileSystemService(string diskFile) : HttpService("/ds3/") {
  this->fileSystem = new LocalFileSystem(new Disk(diskFile, UFS_BLOCK_SIZE));
}

void DistributedFileSystemService::get(HTTPRequest *request, HTTPResponse *response) {
  // 首先得到请求参数，从请求参数中得到文件名信息

  // 调用文件系统api，查询该文件，涉及局部文件系统调用
  // 执行3个局部文件系统的调用，


  // 将文件内容，set到response的body中
  response->setBody("");
}

void DistributedFileSystemService::put(HTTPRequest *request, HTTPResponse *response) {

  // 局部文件系统调用
  // 1. 创建文件
  // 2. 写入文件
  // 3. 关闭文件
  // 假如第3步出错了，也就是说这个创建的文件不能存在，就是倒回去


  response->setBody("");
}

void DistributedFileSystemService::del(HTTPRequest *request, HTTPResponse *response) {
  response->setBody("");
}
