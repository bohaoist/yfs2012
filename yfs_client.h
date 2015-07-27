#ifndef yfs_client_h
#define yfs_client_h

#include <string>
//#include "yfs_protocol.h"
#include "extent_client.h"
#include <vector>

#include "lock_protocol.h"
#include "lock_client.h"
#include "lock_client_cache.h"

class yfs_client {
  extent_client *ec;
  lock_client *lc;
 public:

  typedef unsigned long long inum;
  enum xxstatus { OK, RPCERR, NOENT, IOERR, EXIST };
  typedef int status;

  struct fileinfo {
    unsigned long long size;
    unsigned long atime;
    unsigned long mtime;
    unsigned long ctime;
  };
  struct dirinfo {
    unsigned long atime;
    unsigned long mtime;
    unsigned long ctime;
  };
  struct dirent {
    std::string name;
    yfs_client::inum inum;
  };

 private:
  static std::string filename(inum);
  static inum n2i(std::string);
 public:

  yfs_client(std::string, std::string);

  bool isfile(inum);
  bool isdir(inum);

  int getfile(inum, fileinfo &);
  int getdir(inum, dirinfo &);
  int setattr(inum, struct stat *);
  int read(inum, off_t, size_t, std::string &);
  int write(inum, off_t, size_t, const char *);
  inum random_inum(bool);
  int create(inum, const char *, inum &);
  int lookup(inum, const char *, inum &, bool *);
  int readdir(inum, std::list<dirent> &);
  int mkdir(inum, const char *, mode_t, inum &);
  int unlink(inum, const char *);

};

class ScopedLockClient {
	private:
		lock_client *lc_;
		lock_protocol::lockid_t lid;
	public:
		ScopedLockClient(lock_client *lc, lock_protocol::lockid_t lid):
			lc_(lc),lid(lid) {
				lc_->acquire(lid);
			}
		~ScopedLockClient() {
			lc_->release(lid);
		}
};
class lock_user : public lock_release_user {
	public:
		lock_user(extent_client_cache *e) : ec(e) {}; 
		void dorelease(lock_protocol::lockid_t lid) {
			ec->flush(lid);
		}
    private:
       extent_client_cache *ec;
};
#endif 
