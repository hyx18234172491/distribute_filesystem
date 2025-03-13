#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>

#include "StringUtils.h"
#include "LocalFileSystem.h"
#include "Disk.h"
#include "ufs.h"

using namespace std;

/*
  Use this function with std::sort for directory entries
bool compareByName(const dir_ent_t& a, const dir_ent_t& b) {
    return std::strcmp(a.name, b.name) < 0;
}
*/

int flag = 0;

struct DirEntLess {
	inline bool operator()(const dir_ent_t& a, const dir_ent_t& b) const {
		return std::string(a.name).compare(b.name) < 0;
	}
};

static bool isValidName(int inum, const char* name) {
	return ((inum >= 0) && name && name[0]);
}

std::pair<std::string, std::string> splitFirstSegment(const std::string& path) {
    if (path.empty() || path[0] != '/') return {"", ""}; // 确保是绝对路径

    size_t firstSlash = 1; // 跳过第一个斜杠 '/'
    size_t secondSlash = path.find('/', firstSlash);

    if (secondSlash == std::string::npos) {
        // 只有一个目录，例如 "/a"
        return {path.substr(1), ""};
    } else {
        // 例如 "/a/b/c"，得到 "a" 和 "/b/c"
        return {path.substr(1, secondSlash - 1), path.substr(secondSlash)};
    }
}

void lsDirectory(int inodeNumber, const string path, LocalFileSystem* fileSystem) {
	inode_t inode;
	if (fileSystem->stat(inodeNumber, &inode)){
    // cout << "1" <<endl;
    flag = 1;
    return;
  }

  pair<std::string, std::string> result = splitFirstSegment(path);
  int const numOfEntries = (inode.size) / sizeof(dir_ent_t);
  if (numOfEntries <= 0){
    // cout << "2" <<endl;
    return;
  }
  std::vector<dir_ent_t> vecEntries(numOfEntries);
  if (fileSystem->read(inodeNumber, &vecEntries[0], vecEntries.size()*sizeof(dir_ent_t)) < 0){
    // cout << "3"<<endl;
    return;
  }

  std::sort(vecEntries.begin(), vecEntries.end(), DirEntLess());
  std::vector<dir_ent_t>::iterator itr;
  // if(result.second==""&&result.first.find(".")){
  //   cout << inodeNumber <<"\t" << result.first <<endl;
  //   return ;
  // }

  if(result.first=="" && result.second==""){
    // cout << "4"<<endl;
    for (itr = vecEntries.begin(); itr != vecEntries.end(); ++itr) {
      const dir_ent_t& entry = *itr;
      if (isValidName(entry.inum, entry.name)) {
        cout << entry.inum << "\t" << entry.name << endl;
      }
    }
    return ;
  }
	
 if(result.first!=""){
   int find = 0;
    //DFS for sub-directory
    for (itr = vecEntries.begin(); 
      itr != vecEntries.end(); 
      ++itr) {
      const dir_ent_t& entry = *itr;
      if (isValidName(entry.inum, entry.name) && strcmp(entry.name, ".") && strcmp(entry.name, "..") && strcmp(entry.name,result.first.c_str())==0) {
        inode_t subInode;
        if (fileSystem->stat(entry.inum, &subInode)){
          continue;
        }
        if (subInode.type == UFS_DIRECTORY) {
          find = 1;
          string childPath = result.second;
          lsDirectory(entry.inum, childPath, fileSystem);
        }
      }
    }
    if(find==0){
      flag = 1;
    }
 }
}


int main(int argc, char *argv[]) {
  if (argc != 3) {
    cerr << argv[0] << ": diskImageFile directory" << endl;
    cerr << "For example:" << endl;
    cerr << "    $ " << argv[0] << " tests/disk_images/a.img /a/b" << endl;
    return 1;
  }

  Disk *disk = new Disk(argv[1], UFS_BLOCK_SIZE);
  LocalFileSystem *fileSystem = new LocalFileSystem(disk);
  string directory = string(argv[2]);
  lsDirectory(0, directory, fileSystem);

  delete disk;
  delete fileSystem;

  if(flag==1){
    cerr << "Directory not found" << endl;
    return 1;
  }
  
  return 0;
}
