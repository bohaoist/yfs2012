// RPC stubs for clients to talk to lock_server, and cache the locks
// see lock_client.cache.h for protocol details.

#include "lock_client_cache.h"
#include "rpc.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include "tprintf.h"


lock_client_cache::lock_client_cache(std::string xdst, 
				     class lock_release_user *_lu)
  : lock_client(xdst), lu(_lu)
{
	
  rpcs *rlsrpc = new rpcs(0);
  rlsrpc->reg(rlock_protocol::revoke, this, &lock_client_cache::revoke_handler);
  rlsrpc->reg(rlock_protocol::retry, this, &lock_client_cache::retry_handler);

  const char *hname;
  hname = "127.0.0.1";
  std::ostringstream host;
  host << hname << ":" << rlsrpc->port();
  id = host.str();
  VERIFY(pthread_mutex_init(&client_mutex, NULL) == 0);
}

lock_protocol::status
lock_client_cache::acquire(lock_protocol::lockid_t lid)
{
	int ret = lock_protocol::OK;
	int r; 
	std::map<lock_protocol::lockid_t, lock_entry>::iterator iter;
	pthread_mutex_lock(&client_mutex);
	iter = lockmap.find(lid);
	if(iter == lockmap.end()) {
		iter = lockmap.insert(std::make_pair(lid, lock_entry())).first;
	}

	while (true) { 
		switch(iter->second.state) {
			case NONE:
				iter->second.state = ACQUIRING;
				iter->second.retry = false;
				pthread_mutex_unlock(&client_mutex);
				ret = cl->call(lock_protocol::acquire, lid, id, r);
				pthread_mutex_lock(&client_mutex);
				if (ret == lock_protocol::OK) {
					iter->second.state = LOCKED;
					pthread_mutex_unlock(&client_mutex);
					return ret;
				} else if (ret == lock_protocol::RETRY) {
					if(!iter->second.retry) {
						pthread_cond_wait(&iter->second.retryqueue, &client_mutex);
					}
				}
				break;
			case FREE:
				iter->second.state = LOCKED;
				pthread_mutex_unlock(&client_mutex);
				return lock_protocol::OK;
				break;
			case LOCKED:
				pthread_cond_wait(&iter->second.waitqueue, &client_mutex);
				break;
			case ACQUIRING:
				if(!iter->second.retry) { 
					pthread_cond_wait(&iter->second.waitqueue, &client_mutex);
				} else {
					iter->second.retry = false;
					pthread_mutex_unlock(&client_mutex);
					ret = cl->call(lock_protocol::acquire, lid, id, r);
					pthread_mutex_lock(&client_mutex);
					if (ret == lock_protocol::OK) {
						iter->second.state = LOCKED;
						pthread_mutex_unlock(&client_mutex);
						return ret;
					} else if (ret == lock_protocol::RETRY) {
						if(!iter->second.retry)
							pthread_cond_wait(&iter->second.retryqueue, &client_mutex);
					}
				}
				break;	
			case RELEASING:
				pthread_cond_wait(&iter->second.releasequeue, &client_mutex);
				break;
		}
	}
  return lock_protocol::OK;
}

lock_protocol::status
lock_client_cache::release(lock_protocol::lockid_t lid)
{
	int r;
	lock_protocol::status ret = lock_protocol::OK;
	std::map<lock_protocol::lockid_t, lock_entry>::iterator iter;
	pthread_mutex_lock(&client_mutex);
	iter = lockmap.find(lid);
	if (iter == lockmap.end()) {
		printf("ERROR: can't find lock with lockid = %d\n", lid);
		return lock_protocol::NOENT;
	}
	if (iter->second.revoked) {
		iter->second.state = RELEASING;
		iter->second.revoked = false;
		pthread_mutex_unlock(&client_mutex);
		ret = cl->call(lock_protocol::release, lid, id, r);
		pthread_mutex_lock(&client_mutex);
		iter->second.state = NONE;
		pthread_cond_broadcast(&iter->second.releasequeue);
		pthread_mutex_unlock(&client_mutex);
		return ret;
	} else {
		iter->second.state = FREE;
		pthread_cond_signal(&iter->second.waitqueue);
		pthread_mutex_unlock(&client_mutex);
		return lock_protocol::OK;
	}	
}

rlock_protocol::status
lock_client_cache::revoke_handler(lock_protocol::lockid_t lid, 
                                  int &)
{
	int r;
	int ret = rlock_protocol::OK;
	std::map<lock_protocol::lockid_t, lock_entry>::iterator iter;
	pthread_mutex_lock(&client_mutex);
	iter = lockmap.find(lid);
	if (iter == lockmap.end()) {
		printf("ERROR: can't find lock with lockid = %d\n", lid);
		return lock_protocol::NOENT;
	}
	if (iter->second.state == FREE) {
		iter->second.state = RELEASING;
		pthread_mutex_unlock(&client_mutex);
		ret = cl->call(lock_protocol::release, lid, id, r);
		pthread_mutex_lock(&client_mutex);
		iter->second.state = NONE;
		pthread_cond_broadcast(&iter->second.releasequeue);
		pthread_mutex_unlock(&client_mutex);
	} else {
		iter->second.revoked = true;
		pthread_mutex_unlock(&client_mutex);
	}
  
  return ret;
}

rlock_protocol::status
lock_client_cache::retry_handler(lock_protocol::lockid_t lid, 
                                 int &)
{
	int ret = rlock_protocol::OK;
	std::map<lock_protocol::lockid_t, lock_entry>::iterator iter;
	pthread_mutex_lock(&client_mutex);
	iter = lockmap.find(lid);
	if (iter == lockmap.end()) {
		printf("ERROR: can't find lock with lockid = %d\n", lid);
		return lock_protocol::NOENT;
	}
	iter->second.retry = true;
	pthread_cond_signal(&iter->second.retryqueue);
//	pthread_cond_signal(&iter->second.waitqueue);
	pthread_mutex_unlock(&client_mutex);
	return ret;
}



