// lock protocol

#ifndef lock_protocol_h
#define lock_protocol_h

#include "rpc.h"

class lock_protocol {
 public:
  enum xxstatus { OK, RETRY, RPCERR, NOENT, IOERR };
  typedef int status;
  typedef unsigned long long lockid_t;
  enum rpc_numbers {
    acquire = 0x7001,
    release,
    stat
  };
};

class lock {
 public: 
    enum lock_status {FREE, LOCKED};
	lock_protocol::lockid_t lid;
	int status;
	pthread_cond_t lcond;
														      
	lock(lock_protocol::lockid_t);
	lock(lock_protocol::lockid_t, int);
	~lock(){};
};
#endif 
