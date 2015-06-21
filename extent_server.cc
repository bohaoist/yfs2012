// the extent server implementation

#include "extent_server.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

extent_server::extent_server() {
	int ret;
	pthread_mutex_init(&mutex, NULL);
	//root i-number is 1
	put(1, "", ret);
}


int extent_server::put(extent_protocol::extentid_t id, std::string buf, int &)
{
  // You fill this in for Lab 2.
  ScopedLock _l(&mutex);
  extent_protocol::attr attr;
  attr.atime = attr.mtime = attr.ctime = time(NULL);
  if (file_map.find(id) != file_map.end()) 
	  attr.atime = file_map[id].attr.atime;
  attr.size = buf.size();
	file_map[id].data = buf;
	file_map[id].attr = attr;	
	return extent_protocol::OK;
//  return extent_protocol::IOERR;
}

int extent_server::get(extent_protocol::extentid_t id, std::string &buf)
{
  // You fill this in for Lab 2.
  ScopedLock _l(&mutex);
  if (file_map.find(id) != file_map.end()) {
	file_map[id].attr.atime = time(NULL);
	buf = file_map[id].data;
	return extent_protocol::OK;
  }
  return extent_protocol::NOENT;
//  return extent_protocol::IOERR;
}

int extent_server::getattr(extent_protocol::extentid_t id, extent_protocol::attr &a)
{
  // You fill this in for Lab 2.
  // You replace this with a real implementation. We send a phony response
  // for now because it's difficult to get FUSE to do anything (including
  // unmount) if getattr fails.
/*  a.size = 0;
  a.atime = 0;
  a.mtime = 0;
  a.ctime = 0;*/
	ScopedLock _l(&mutex);
	if (file_map.find(id) != file_map.end()) {
		a = file_map[id].attr;
		return extent_protocol::OK;
	}
	return extent_protocol::NOENT;	
//  return extent_protocol::OK;
}

int extent_server::remove(extent_protocol::extentid_t id, int &)
{
  // You fill this in for Lab 2.
	std::map<extent_protocol::extentid_t, extent>::iterator iter;
	iter  = file_map.find(id);
  if (iter != file_map.end()) {
	file_map.erase(iter);
	return extent_protocol::OK;
  } else {
	return extent_protocol::NOENT;
  }
//  return extent_protocol::IOERR;
}

