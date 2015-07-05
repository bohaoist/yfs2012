// the lock server implementation

#include "lock_server.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>

lock::lock(lock_protocol::lockid_t lid):
	    status(lock::FREE)
{
	    lid = lid;
		    pthread_cond_init(&lcond, NULL);
}

lock::lock(lock_protocol::lockid_t lid, int stat):
	    status(stat) 
{

	    lid = lid;
		    pthread_cond_init(&lcond, NULL);
}

lock_server::lock_server():
  nacquire (0)
{
	pthread_mutex_init(&mutex, NULL);
}

lock_protocol::status
lock_server::stat(int clt, lock_protocol::lockid_t lid, int &r)
{
  lock_protocol::status ret = lock_protocol::OK;
  printf("stat request from clt %d\n", clt);
  r = nacquire;
  return ret;
}

lock_protocol::status
lock_server::acquire(int clt, lock_protocol::lockid_t lid, int &r)
{
	lock_protocol::status ret = lock_protocol::OK;
	std::map<lock_protocol::lockid_t, lock* >::iterator iter;
	pthread_mutex_lock(&mutex);
	iter = lockmap.find(lid);
	if(iter != lockmap.end()) {
		while(iter->second->status != lock::FREE) {
			pthread_cond_wait(&(iter->second->lcond), &mutex);
		}
		iter->second->status = lock::LOCKED;
		pthread_mutex_unlock(&mutex);
		return ret;
	} else {
		lock *new_lock = new lock(lid, lock::LOCKED);
	//	lockmap[lid] = new_lock;
		lockmap.insert(std::make_pair(lid, new_lock));
		pthread_mutex_unlock(&mutex);
		return ret;
	}
}

lock_protocol::status
lock_server::release(int clt, lock_protocol::lockid_t lid, int &r)
{
	lock_protocol::status ret = lock_protocol::OK;
	std::map<lock_protocol::lockid_t, lock*>::iterator iter;
	pthread_mutex_lock(&mutex);
	iter = lockmap.find(lid);
	if (iter != lockmap.end()) {
		iter->second->status = lock::FREE;
		pthread_cond_signal(&(iter->second->lcond));
		pthread_mutex_unlock(&mutex);
		return ret;
	} else {
		ret = lock_protocol::IOERR;
		pthread_mutex_unlock(&mutex);
		return ret;
	}
}

