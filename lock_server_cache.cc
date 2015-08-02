// the caching lock server implementation

#include "lock_server_cache.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <set>
#include <arpa/inet.h>
#include "lang/verify.h"
#include "handle.h"
#include "tprintf.h"


lock_server_cache::lock_server_cache()
{
	VERIFY(pthread_mutex_init(&server_mutex, NULL) == 0);
}


int lock_server_cache::acquire(lock_protocol::lockid_t lid, std::string id, 
                               int &)
{
	bool revoke = false;
	lock_protocol::status ret = lock_protocol::OK;
	std::map<lock_protocol::lockid_t, lock_entry>::iterator iter;
	pthread_mutex_lock(&server_mutex);
	iter = lockmap.find(lid);
	if (iter == lockmap.end()){ 
		iter = lockmap.insert(std::make_pair(lid, lock_entry())).first;
	}
	switch(iter->second.state) {
		
		case FREE:
			iter->second.state = LOCKED;
			iter->second.owner = id;
			break;
		
		case LOCKED:
			iter->second.state = LOCKED_AND_WAIT;
			iter->second.waitset.insert(id);
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
	pthread_mutex_unlock(&server_mutex);
	if(revoke) {
		int r;
		handle(iter->second.owner).safebind()->call(rlock_protocol::revoke, lid, r);
	}
	return ret;
	/*
	if (iter->second.owner.empty()) {
		iter->owner = id;
		pthread_mutex_unlock(&server_mutex);
		return ret;
	} else {
		iter->second.waitset.insert(id);
		ret = lock_protocol::RETRY;
		if (!iter->second.revoked) {
			iter->second.revoked = true;
			pthread_mutex_unlock(&server_mutex);
			handle(iter->second.id).safebind()->call(rlock_protocol::revoke, lid, r);
			return ret;
		}
		pthread_mutex_unlock(&server_mutex);
		return ret;
	} */
}

int 
lock_server_cache::release(lock_protocol::lockid_t lid, std::string id, 
         int &r)
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
			break;
		
		case RETRYING:
			ret = lock_protocol::IOERR;
			printf("ERROR: can't releaer a lock with state RETRYING\n");
			break;
		
		default:
			break;	
	}
	pthread_mutex_unlock(&server_mutex);
	if(retry) {
		handle(client_need_retry).safebind()->call(rlock_protocol::retry, lid, r);	
	}
	return ret;
	/*iter->second.owner = std::string();
	iter->second.state = FREE;
	iter->revoked = false;
	if (!iter->second.waitset.empty()) {
		std::string next = *iter->second.waitset.begin();
		pthread_mutex_unlock(&server_mutex);
		handle(next).safebind->call(rlock::protocol::retry, lid, r);
		if(iter->second.waitset.size() > 1) 
			handle(next).safebind->call(rlock::protocol::revoke, lid, r);
		iter->second.waitset.erase(next);
		return ret;
	}
	pthread_mutex_unlock(&server_mutex);*/
	return ret;
}

lock_protocol::status
lock_server_cache::stat(lock_protocol::lockid_t lid, int &r)
{
  tprintf("stat request\n");
  r = nacquire;
  return lock_protocol::OK;
}

