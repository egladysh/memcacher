#ifndef MC_SESSION_H
#define MC_SESSION_H

#include <vector>
#include <stdint.h>
#include "protocol_binary.h"
#include "cache.h"

namespace mc //for memcache...
{
	//forward declarations
	struct server;

	struct session
	{
		typedef std::vector<unsigned char> buffer;

		int fd_; //connection socket
		int ctl_pipe_; //used for control when writing large data on the socket
		void* user_; //user data
		cache& c_;

		explicit session(int fd, int ctl_pipe, void* user, cache& c);
		~session();

		bool process_chunk(buffer b); //returns false if the session is to be closed
		bool control(buffer b); //control event on the session
		
	private:
		buffer request_;
		protocol_binary_request_header header_; //packet header
		struct write_control
		{
			const unsigned char* buf_;
			size_t len_;
			write_control()
				:buf_(nullptr)
				,len_(0)
			{}
			bool is_active() const {
				return len_ != 0;
			}
		};
		write_control wctl_;

		bool handle_request_set();
		bool handle_request_get();

		bool handle_request();
		bool validate_request();

		void error_response(protocol_binary_response_status err);

		bool begin_write(const unsigned char* buf, size_t len);
		bool continue_write();

		bool socket_write(const unsigned char* buf, size_t len);

		void reset();

		session(const session&) = delete;
		session& operator=(const session&) = delete;
	};
}

#endif
