
// RPC stubs for clients to talk to extent_server

#include "extent_client.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <time.h>

// The calls assume that the caller holds a lock on the extent
extent_client_cache::extent_client_cache(std::string dst)
	:extent_client(dst)
{
	VERIFY(pthread_mutex_init(&extent_mutex, NULL) == 0);
}

extent_protocol::status
extent_client_cache::get(extent_protocol::extentid_t eid, std::string &buf)
{
	extent_protocol::status ret = extent_protocol::OK;
	ScopedLock _m(&extent_mutex);
	bool flag = file_cached.count(eid);
	if (flag) {
		switch (file_cached[eid].status) {
			case UPDATED:
			case MODIFIED:
				buf = file_cached[eid].data;
				file_cached[eid].attr.atime = time(NULL);
				break;
			case NONE:
				ret = cl->call(extent_protocol::get, eid, buf);
				if (ret == extent_protocol::OK) {
					file_cached[eid].data = buf;
					file_cached[eid].status = UPDATED;
					file_cached[eid].attr.atime = time(NULL);
					file_cached[eid].attr.size = buf.size();
				}
				break;
			case REMOVED:
			default:
				ret = extent_protocol::NOENT;
				break;
		}
	} else {
		ret = cl->call(extent_protocol::get, eid, buf);
		if (ret == extent_protocol::OK) {	
			file_cached[eid].data = buf;
			file_cached[eid].status = UPDATED;
			file_cached[eid].attr.atime = time(NULL);
			file_cached[eid].attr.size = buf.size();
			file_cached[eid].attr.ctime = 0;
			file_cached[eid].attr.mtime = 0;
		}
	}
	buf = file_cached[eid].data;
	return ret;
}

extent_protocol::status
extent_client_cache::getattr(extent_protocol::extentid_t eid, 
		       extent_protocol::attr &attr)
{
	extent_protocol::status ret = extent_protocol::OK;
	extent_protocol::attr *a, temp;
	ScopedLock _m(&extent_mutex);
	bool flag = file_cached.count(eid);
	if (flag) {
		switch (file_cached[eid].status) {
			case UPDATED:
			case MODIFIED:
			case NONE:
				a = &file_cached[eid].attr;
				if (!a->atime || !a->ctime || !a->mtime) {
					ret = cl->call(extent_protocol::getattr, eid, temp);
					if (ret == extent_protocol::OK) { 
						if (!a->atime)
							a->atime = temp.atime;
						if (!a->ctime)
							a->ctime = temp.ctime;
						if (!a->mtime)
							a->mtime = temp.mtime;
						if (file_cached[eid].status == NONE)
							a->size = temp.size;
						else 
							a->size = file_cached[eid].attr.size;
					}
				}
				break;
			case REMOVED:
			default:
				ret = extent_protocol::NOENT;
				break;
		}
	} else {
		file_cached[eid].status = NONE;
		a = &file_cached[eid].attr;
		a->atime = a->mtime = a->ctime = 0;
		a->size = 0;
		ret = cl->call(extent_protocol::getattr, eid, temp);
		if (ret == extent_protocol::OK) {
			a->atime = temp.atime;
			a->ctime = temp.ctime;
			a->mtime = temp.mtime;
			a->size = temp.size;
		}
	}
	attr = *a;
	return ret;
}

extent_protocol::status
extent_client_cache::put(extent_protocol::extentid_t eid, std::string buf)
{
  	extent_protocol::status ret = extent_protocol::OK;
  	int r;
	ScopedLock _m(&extent_mutex);
	bool flag = file_cached.count(eid);

	if (flag) {
		switch (file_cached[eid].status) {
			case NONE:
			case UPDATED:
			case MODIFIED:
				file_cached[eid].data = buf;
				file_cached[eid].status = MODIFIED;
				file_cached[eid].attr.mtime = time(NULL);
				file_cached[eid].attr.ctime = time(NULL);
				file_cached[eid].attr.size = buf.size();
				break;
			case REMOVED:
				ret = extent_protocol::NOENT;
				break;
		}
	} else {
		file_cached[eid].data = buf;
		file_cached[eid].status = MODIFIED;
		file_cached[eid].attr.atime = time(NULL);
		file_cached[eid].attr.mtime = time(NULL);
		file_cached[eid].attr.ctime = time(NULL);
		file_cached[eid].attr.size = buf.size();
	}
  	return ret;
}

extent_protocol::status
extent_client_cache::remove(extent_protocol::extentid_t eid)
{
  	extent_protocol::status ret = extent_protocol::OK;
  	int r;
  	ScopedLock _m(&extent_mutex);
  	bool flag = file_cached.count(eid);
	if (flag) {
		switch (file_cached[eid].status) {
			case NONE:
			case UPDATED:
			case MODIFIED:
				file_cached[eid].status = REMOVED;
				break;
			case REMOVED:
				ret = extent_protocol::NOENT;
				break;
		}
	} else {
		file_cached[eid].status = REMOVED;
		//	ret = extent_protocol::NOENT;
	}
	return ret;
}

extent_protocol::status
extent_client_cache::flush(extent_protocol::extentid_t eid)
{
	extent_protocol::status ret = extent_protocol::OK;
	int r;
	ScopedLock _m(&extent_mutex);
	bool flag = file_cached.count(eid);
	if (flag) {	
		switch(file_cached[eid].status) {
			case MODIFIED:
				ret = cl->call(extent_protocol::put, eid, file_cached[eid].data, r);
				break;
			case REMOVED:
				ret = cl->call(extent_protocol::remove, eid);
				break;
			case NONE:
			case UPDATED:
			default:
				break;
		}
		file_cached.erase(eid);	
	} else {
		ret = extent_protocol::NOENT;
	}
	return ret;
}
