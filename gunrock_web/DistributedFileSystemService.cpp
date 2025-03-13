#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sstream>
#include <iostream>
#include <map>
#include <string>
#include <algorithm>
#include <cstring>

#include "DistributedFileSystemService.h"
#include "ClientError.h"
#include "ufs.h"
#include "WwwFormEncodedDict.h"

using namespace std;

DistributedFileSystemService::DistributedFileSystemService(string diskFile) : HttpService("/ds3/") {
  this->fileSystem = new LocalFileSystem(new Disk(diskFile, UFS_BLOCK_SIZE));
}

void DistributedFileSystemService::get(HTTPRequest *request, HTTPResponse *response) {
	char buffer[MAX_FILE_SIZE];
  string path = request->getPath().substr(this->pathPrefix().length());
	int inum = 0;
	while (path.size()) {
		std::string path2;
		size_t t = path.find('/');
		if (std::string::npos != t) {
			path2 = path.substr((t + 1));
			path.erase(t);
		}
		inum = fileSystem->lookup(inum, path);
		if (inum < 0){
      break;
    }
		path = path2;
	}

  if (inum < 0){
    throw ClientError::notFound();
  }

  inode_t inode;
  if (fileSystem->stat(inum, &inode)!=0){
    throw ClientError::notFound();
  }
  if (inode.type == UFS_DIRECTORY) {
      std::stringstream ss;
      int br = fileSystem->read(inum, buffer, sizeof(buffer));
      for (int offset = 0; offset < br; offset += sizeof(dir_ent_t)) {
        const dir_ent_t* entry = (const dir_ent_t*)(buffer + offset);
        if ((entry->inum != -1) && strcmp(entry->name, ".") && strcmp(entry->name, "..")) {
          if (fileSystem->stat(entry->inum, &inode)){
            continue;
          }
          else if (UFS_DIRECTORY == inode.type){
            ss << entry->name << "/" << endl;
          }
          else{
            ss << entry->name << endl;
          }
        }
      }
      response->setBody(ss.str());
  } else {
      int br = fileSystem->read(inum, buffer, inode.size);
      response->setBody(string(buffer, br));
  }
}

void DistributedFileSystemService::put(HTTPRequest *request, HTTPResponse *response) {

  string path = request->getPath().substr(this->pathPrefix().length());
  string content = request->getBody();
	int inum = 0;
	while (path.size()) {
		std::string path2;
		size_t t = path.find('/');
		if (std::string::npos != t) {
			path2 = path.substr((t + 1));
			path.erase(t);
		}
		else {
			break;
		}
		int ret = fileSystem->lookup(inum, path);
		if (-ENOTFOUND == ret) {
			inum = fileSystem->create(inum, UFS_DIRECTORY, path);
			if (inum < 0){
        throw ClientError::badRequest();
      }
		}
		else if (ret < 0) {
			throw ClientError::badRequest();
		}
		else {
			inum = ret;
		}
		path = path2;
	}

	if (path.empty()){
    throw ClientError::badRequest();
  }

	int parentInodeNumber = inum;
	inum = fileSystem->lookup(parentInodeNumber, path);
	if (inum == -ENOTFOUND) {
		inum = fileSystem->create(parentInodeNumber, UFS_REGULAR_FILE, path);
		if (inum < 0){
      throw ClientError::insufficientStorage();
    }
	}
	else if (inum < 0) {
		throw ClientError::badRequest();
	}

	if (fileSystem->write(inum, content.c_str(), content.size()) < 0){
    throw ClientError::insufficientStorage();
  }

	response->setStatus(200);
	response->setBody("File created or updated successfully.");
}

void DistributedFileSystemService::del(HTTPRequest *request, HTTPResponse *response) {
  string path = request->getPath().substr(this->pathPrefix().length());
	if (path.length() && ('/' == path.back())){
    path.pop_back();
  }
	int inum = 0;
	while (path.size()) {
		std::string path2;
		size_t t = path.find('/');
		if (std::string::npos != t) {
			path2 = path.substr((t + 1));
			path.erase(t);
		}else {
			break;
		}
		inum = fileSystem->lookup(inum, path);
		if (-ENOTFOUND == inum){
      throw ClientError::notFound();
    }
		else if (inum < 0){
      throw ClientError::badRequest();
    }
		path = path2;
	}

	if (path.empty()){
    throw ClientError::badRequest();
  }

	int dinum = fileSystem->lookup(inum, path);
	if (dinum < 0){
    throw ClientError::notFound();
  }
	int ret = fileSystem->unlink(inum, path);
	if (-EDIRNOTEMPTY == ret){
    throw ClientError::forbidden();
  }
	else if (-EINVALIDNAME == ret){
    throw ClientError::notFound();
  }
	else if (ret < 0){
    throw ClientError::insufficientStorage();
  }

	response->setStatus(200);
	response->setBody("Resource deleted successfully.");
}
