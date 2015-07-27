// extent client interface.

#ifndef extent_client_h
#define extent_client_h

#include <string>
#include "extent_protocol.h"
#include "rpc.h"

class extent_client {
 private:
 protected:
  rpcc *cl;

 public:
  extent_client(std::string dst);

  virtual extent_protocol::status get(extent_protocol::extentid_t eid, 
			      std::string &buf);
  virtual extent_protocol::status getattr(extent_protocol::extentid_t eid, 
				  extent_protocol::attr &a);
  virtual extent_protocol::status put(extent_protocol::extentid_t eid, std::string buf);
  virtual extent_protocol::status remove(extent_protocol::extentid_t eid);
};

class extent_client_cache : public extent_client {
	enum file_state {NONE,UPDATED, MODIFIED, REMOVED};
	struct extent {
		std::string data;
		file_state status;
		extent_protocol::attr attr;
		extent():status(NONE) {}
	};
	public:
		extent_client_cache(std::string dst);
		extent_protocol::status get(extent_protocol::extentid_t eid, 
			      std::string &buf);
		extent_protocol::status getattr(extent_protocol::extentid_t eid, 
				  extent_protocol::attr &a);
		extent_protocol::status put(extent_protocol::extentid_t eid, std::string buf);
		extent_protocol::status remove(extent_protocol::extentid_t eid);
		extent_protocol::status flush(extent_protocol::extentid_t eid);
	private:
		pthread_mutex_t extent_mutex; 
		std::map <extent_protocol::extentid_t, extent>file_cached;
};
#endif 

