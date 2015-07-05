#ifndef lock_server_cache_h
#define lock_server_cache_h

#include <string>

#include <map>
#include <set>
#include "lock_protocol.h"
#include "rpc.h"
#include "lock_server.h"


class lock_server_cache {
	enum lock_state {FREE, LOCKED, LOCKED_AND_WAIT, RETRYING};
	struct lock_entry {
		std::string owner;
		std::set<std::string> waitset;
		bool revoked;
		lock_state state;
		lock_entry():revoked(false),state(FREE) {}
	};
 private:
  int nacquire;
  std::map<lock_protocol::lockid_t, lock_entry> lockmap;
  pthread_mutex_t server_mutex;
 public:
  lock_server_cache();
  lock_protocol::status stat(lock_protocol::lockid_t, int &);
  int acquire(lock_protocol::lockid_t, std::string id, int &);
  int release(lock_protocol::lockid_t, std::string id, int &);
};

#endif
