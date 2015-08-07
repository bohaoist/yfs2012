// the caching lock server implementation

#include "lock_server_cache_rsm.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "lang/verify.h"
#include "handle.h"
#include "tprintf.h"


static void *
revokethread(void *x)
{
  lock_server_cache_rsm *sc = (lock_server_cache_rsm *) x;
  sc->revoker();
  return 0;
}

static void *
retrythread(void *x)
{
  lock_server_cache_rsm *sc = (lock_server_cache_rsm *) x;
  sc->retryer();
  return 0;
}

lock_server_cache_rsm::lock_server_cache_rsm(class rsm *_rsm) 
  : rsm (_rsm)
{
  pthread_t th;
  int r = pthread_create(&th, NULL, &revokethread, (void *) this);
  VERIFY (r == 0);
  r = pthread_create(&th, NULL, &retrythread, (void *) this);
  VERIFY (r == 0);
}

void
lock_server_cache_rsm::revoker()
{

  // This method should be a continuous loop, that sends revoke
  // messages to lock holders whenever another client wants the
  // same lock
	while(true) {
		revoke_retry_entry e;
		revoke_queue.deq(&e);
		
		if (rsm->amiprimary()) {
			int r;
			rpcc *cl = handle(e.id).safebind();
			VERIFY(cl != NULL);
			cl->call(rlock_protocol::revoke, e.lid, e.xid, r);
		}
	}
}


void
lock_server_cache_rsm::retryer()
{

  // This method should be a continuous loop, waiting for locks
  // to be released and then sending retry messages to those who
  // are waiting for it.
	while(true) {
		revoke_retry_entry e;
		retry_queue.deq(&e);

		if (rsm->amiprimary()) {
			int r;
			rpcc *cl = handle(e.id).safebind();
			VERIFY(cl != NULL);
			cl->call(rlock_protocol::retry, e.lid, e.xid, r);
		}
	}
}


int lock_server_cache_rsm::acquire(lock_protocol::lockid_t lid, std::string id, 
             lock_protocol::xid_t xid, int &)
{
	bool revoke = false;
	lock_protocol::status ret = lock_protocol::OK;
	std::map<lock_protocol::lockid_t, lock_entry>::iterator iter;
	pthread_mutex_lock(&server_mutex);
	iter = lockmap.find(lid);
	if (iter == lockmap.end()){ 
		iter = lockmap.insert(std::make_pair(lid, lock_entry())).first;
	}

	lock_entry& le = iter->second;
	client_xid_map::iterator xid_iter= le.highest_xid_from_client.find(id);
	
	if (xid_iter == le.highest_xid_from_client.end() || xid_iter->second < xid) {
		
		le.highest_xid_from_client[id] = xid;
		le.highest_xid_release_reply.erase(id);

		switch(iter->second.state) {
		
			case FREE:
				iter->second.state = LOCKED;
				iter->second.owner = id;
				break;
		
			case LOCKED:
				iter->second.state = LOCKED_AND_WAIT;
				iter->second.waitset.insert(id);
				revoke_queue.enq(revoke_retry_entry(iter->second.owner, lid, le.highest_xid_from_client[iter->second.owner]));
				revoke = true;
				ret = lock_protocol::RETRY;
				break;
		
			case LOCKED_AND_WAIT:
				iter->second.waitset.insert(id);
				ret = lock_protocol::RETRY;
				break;
		
			case RETRYING:
				if (iter->second.waitset.count(id)) {
					iter->second.waitset.erase(id);
					iter->second.owner = id;
					if(iter->second.waitset.size()) {
						iter->second.state = LOCKED_AND_WAIT;
						revoke = true;
						revoke_queue.enq(revoke_retry_entry(iter->second.owner, lid, le.highest_xid_from_client[iter->second.owner]));	
					}
					else
						iter->second.state = LOCKED;
				} else {
					iter->second.waitset.insert(id);
					ret = lock_protocol::RETRY;
				}
				break;
		
			default:
				break;
		}
		le.highest_xid_acquire_reply[id] = ret;
	} else if (xid_iter->second == xid) {
		ret = le.highest_xid_acquire_reply[id];	
	} else {
		//xid_iter->second > xid
		printf("ERROR: received acquire with old xid. Highest seen: %lld, current xid: %lld\n", xid_iter->second, 		xid);		ret = lock_protocol::RPCERR;
	}	
	
	pthread_mutex_unlock(&server_mutex);
	
	return ret;
}

int 
lock_server_cache_rsm::release(lock_protocol::lockid_t lid, std::string id, 
         lock_protocol::xid_t xid, int &r)
{
	bool retry = false;
	std::string client_need_retry;
	lock_protocol::status ret = lock_protocol::OK;
	std::map<lock_protocol::lockid_t, lock_entry>::iterator iter;
	pthread_mutex_lock(&server_mutex);
	iter = lockmap.find(lid);
	if (iter == lockmap.end()) {
		printf("ERROR: can't find lock with lockid = %d\n", lid);
		return lock_protocol::NOENT;
	} 
	
	lock_entry &le = iter->second;
	
	client_xid_map::iterator xid_iter = le.highest_xid_from_client.find(id);
	VERIFY(xid <= xid_iter->second); 
	if (xid_iter != le.highest_xid_from_client.end() && xid == xid_iter->second) {
		
		client_reply_map::iterator reply_iter = le.highest_xid_release_reply.find(id);
		
		if (reply_iter == le.highest_xid_release_reply.end()) {
			
			if (iter->second.owner != id) {
				ret = lock_protocol::IOERR;
         	 		le.highest_xid_release_reply.insert(make_pair(id, ret));
          			printf("ERROR: received release from client not holding the lock.\n");			
			}	
			switch(iter->second.state) {
				case FREE:
					printf("ERROR: can't release a lock with state FREE\n");
					ret = lock_protocol::IOERR;
					break;
		
				case LOCKED:
					iter->second.state = FREE;
					iter->second.owner = "";
					break;
			
				case LOCKED_AND_WAIT:
					iter->second.state = RETRYING;
					iter->second.owner = "";
					client_need_retry = *iter->second.waitset.begin();
					retry = true;
					retry_queue.enq(revoke_retry_entry(client_need_retry, lid, le.highest_xid_from_client[client_need_retry]));
					break;
				
				case RETRYING:
					ret = lock_protocol::IOERR;
					printf("ERROR: can't releaer a lock with state RETRYING\n");
					break;
			
				default:
					break;	
			}
			le.highest_xid_release_reply.insert(make_pair(id, ret));
		} else {
			ret = reply_iter->second;		
		}
	} else if (xid < xid_iter->second) {
		printf("ERROR: received release with incorrect xid. xid: %lld, highest acquire xid: %lld\n", xid, xid_iter->second);
      		ret = lock_protocol::RPCERR;
	} else {
		//(xid_iter == le.highest_xid_from_client.end()
		printf("ERROR: received release for lock with no recorded xid for this client.\n");
      		ret = lock_protocol::RPCERR;
	}
	pthread_mutex_unlock(&server_mutex);
			
	return ret;
}

std::string
lock_server_cache_rsm::marshal_state()
{
	
  std::ostringstream ost;
  std::string r;
  
  ScopedLock ml(&server_mutex);
  marshall rep;
  unsigned int size = lockmap.size();
  rep << size;
  std::map<lock_protocol::lockid_t, lock_entry>::iterator iter;
  for (iter = lockmap.begin(); iter != lockmap.end(); iter++) {
  	rep << iter->first;
	rep << iter->second.owner;
	rep << iter->second.revoked;
	unsigned int state = iter->second.state;
	rep << state;
	std::set<std::string>::iterator iter_waitset;
	unsigned int wait_size = iter->second.waitset.size();
	rep << wait_size;
	for (iter_waitset = iter->second.waitset.begin(); iter_waitset != iter->second.waitset.end(); 
					iter_waitset++) {
		rep << *iter_waitset;	
	}
	
	client_xid_map::iterator iter_client_xid_map;
	unsigned int xid_size = iter->second.highest_xid_from_client.size();
	rep << xid_size;
	for (iter_client_xid_map = iter->second.highest_xid_from_client.begin(); 
			iter_client_xid_map != iter->second.highest_xid_from_client.end(); 
			iter_client_xid_map++) {
		rep << iter_client_xid_map->first;
		rep << iter_client_xid_map->second;
	}
	client_reply_map::iterator iter_client_reply_map;
	unsigned int reply_size = iter->second.highest_xid_acquire_reply.size();
	rep << reply_size;
	for (iter_client_reply_map = iter->second.highest_xid_acquire_reply.begin(); 
			iter_client_reply_map != iter->second.highest_xid_acquire_reply.end(); 
			iter_client_reply_map++)
	{
		rep << iter_client_reply_map->first;
		rep << iter_client_reply_map->second;
	}
	
	reply_size = iter->second.highest_xid_release_reply.size();
	rep << reply_size;
	for (iter_client_reply_map = iter->second.highest_xid_release_reply.begin();
	   		iter_client_reply_map != iter->second.highest_xid_release_reply.end(); 
			iter_client_reply_map++) {
		rep << iter_client_reply_map->first;
		rep << iter_client_reply_map->second;
	}
  }
  r = rep.str();
  return r;
}

void
lock_server_cache_rsm::unmarshal_state(std::string state)
{
	/*
	ScopedLock ml(&server_mutex);
	unmarshall rep (state);
	unsigned int lockmap_size;
	rep >> lockmap_size;
	for (unsigned int i = 0; i < lockmap_size; i++)
	{
		lock_protocol::lockid_t lid;
		rep >> lid;
		lock_entry *entry = new lock_entry();
		rep >> entry->owner;
		rep >> entry->revoked;
		unsigned int lock_status;
		rep >> lock_status;
		entry->state = (lock_state)lock_status;
		unsigned int waitset_size;
		rep >> waitset_size;
		std::string waitid;
		for (unsigned int k = 0; k < waitset_size; k++) {
			rep >> waitid;
			entry->waitset.insert(waitid);		
		}
		unsigned int xid_size;
		rep >> xid_size;
		std::string client_id;
		lock_protocol::xid_t xid;
		for (unsigned int m = 0; m < xid_size; m++) {
			rep >> client_id;
			rep >> xid;
		   	entry->highest_xid_from_client[client_id] = xid;
		}
		unsigned int reply_size;
		rep >> reply_size;
		int ret;
		for (unsigned int n = 0; n < reply_size; n++) {
			rep >> client_id;
			rep >> ret;
			entry->highest_xid_acquire_reply[client_id] = ret; 
		}
		rep >> reply_size;
		for (unsigned int j = 0; j < reply_size; j++) {
			rep >> client_id;
			rep >> ret;
			entry->highest_xid_release_reply[client_id] = ret;
		}
		lockmap[lid] = *entry;
	}*/
}

lock_protocol::status
lock_server_cache_rsm::stat(lock_protocol::lockid_t lid, int &r)
{
  printf("stat request\n");
  r = nacquire;
  return lock_protocol::OK;
}

