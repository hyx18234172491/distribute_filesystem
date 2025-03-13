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

struct DirEntLess {
	inline bool operator()(const dir_ent_t& a, const dir_ent_t& b) const {
		return std::string(a.name).compare(b.name) < 0;
	}
};

static bool isValidName(int inum, const char* name) {
	return ((inum >= 0) && name && name[0]);
}

void lsDirectory(int inodeNumber, const string& path, LocalFileSystem* fileSystem) {
	inode_t inode;
	if (fileSystem->stat(inodeNumber, &inode))
		return;

	string path2 = path;
	if (path2 == "/") {
		path2.clear();
	}

	// cout << "Directory " << (path2.empty() ? "/" : path2 + "/") << endl;

	int const numOfEntries = (inode.size) / sizeof(dir_ent_t);
	if (numOfEntries <= 0)
		return;

	std::vector<dir_ent_t> vecEntries(numOfEntries);

	if (fileSystem->read(inodeNumber, &vecEntries[0], vecEntries.size()*sizeof(dir_ent_t)) < 0)
		return;

	std::sort(vecEntries.begin(), vecEntries.end(), DirEntLess());

	std::vector<dir_ent_t>::iterator itr;
	for (itr = vecEntries.begin(); itr != vecEntries.end(); ++itr) {
		const dir_ent_t& entry = *itr;
		if (isValidName(entry.inum, entry.name)) {
			cout << entry.inum << "\t" << entry.name << endl;
		}
	}
	cout << endl;

	//DFS for sub-directory
	for (itr = vecEntries.begin(); 
		itr != vecEntries.end(); 
		++itr) {
		const dir_ent_t& entry = *itr;
		if (isValidName(entry.inum, entry.name) && strcmp(entry.name, ".") && strcmp(entry.name, "..")) {
			inode_t subInode;
			if (fileSystem->stat(entry.inum, &subInode))
				continue;
			if (subInode.type == UFS_DIRECTORY) {
				string childPath = path2 + "/" + entry.name;
				lsDirectory(entry.inum, childPath, fileSystem);
			}
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
  
  return 0;
}
