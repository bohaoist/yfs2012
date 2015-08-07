#ifndef lock_server_cache_rsm_h
#define lock_server_cache_rsm_h

#include <string>

#include "lock_protocol.h"
#include "rpc.h"
#include "rsm_state_transfer.h"
#include "rsm.h"
#include <set>

class lock_server_cache_rsm : public rsm_state_transfer {
	typedef std::map<std::string, int> client_reply_map;
	typedef std::map<std::string, lock_protocol::xid_t> client_xid_map;

	enum lock_state {FREE, LOCKED, LOCKED_AND_WAIT, RETRYING};
	struct lock_entry {
		std::string owner;
		bool revoked;
		lock_state state;
		std::set<std::string> waitset;
		client_xid_map highest_xid_from_client;
		client_reply_map highest_xid_acquire_reply;
		client_reply_map highest_xid_release_reply;

		lock_entry():revoked(false),state(FREE) {}
	};  
	struct revoke_retry_entry {
		std::string id; 
		lock_protocol::lockid_t lid;
		lock_protocol::xid_t xid;
		revoke_retry_entry(const std::string& id_ = "", lock_protocol::lockid_t lid_ = 0,
			lock_protocol::xid_t xid_ = 0) : id(id_), lid(lid_), xid(xid_) {}
	};


 private:
  int nacquire;
  class rsm *rsm;


  	std::map<lock_protocol::lockid_t, lock_entry> lockmap;
  	pthread_mutex_t server_mutex;
	fifo<revoke_retry_entry> retry_queue;
	fifo<revoke_retry_entry> revoke_queue;

 public:
  lock_server_cache_rsm(class rsm *rsm = 0);
  lock_protocol::status stat(lock_protocol::lockid_t, int &);
  void revoker();
  void retryer();
  std::string marshal_state();
  void unmarshal_state(std::string state);
  int acquire(lock_protocol::lockid_t, std::string id, 
	      lock_protocol::xid_t, int &);
  int release(lock_protocol::lockid_t, std::string id, lock_protocol::xid_t,
	      int &);
};

#endif
