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
			buffer hdr_;
			size_t offset_;
			std::shared_ptr<cache::item> item_;

			write_control()
				:offset_(0)
				,pos_(0)
			{}

			std::pair<const unsigned char*, size_t> next() 
			{
				if (!hdr_.empty()) {
					assert(pos_ < hdr_.size());
					return std::make_pair(hdr_.data() + pos_, hdr_.size() - pos_);
				}
				assert(item_);
				assert(item_->get_value_len() > pos_);
				return std::make_pair(item_->get_value() + pos_, item_->get_value_len() - pos_);
			}
			void move(size_t cnt)
			{
				if (!hdr_.empty()) {
					pos_ += cnt;
					if (pos_ >= hdr_.size()) {
						hdr_.clear();
						pos_ = offset_;
					}
				}
				else {
					pos_ += cnt;
				}
			}

			bool is_active() const
			{
				if (!item_) {
					return !hdr_.empty() && pos_ < hdr_.size();
				}
				//header is active
				if (!hdr_.empty() && pos_ < hdr_.size()) {
					return true;
				}
				// item is active
				return pos_ < item_->get_value_len();
			}

			void reset()
			{
				item_.reset();
				pos_ = 0;
				offset_ = 0;
			}
			
		private:
			size_t pos_;
		};
		write_control wctl_;

		bool handle_request_set();
		bool handle_request_get();
		bool handle_request_delete();

		bool handle_request();
		bool validate_request();

		void error_response(protocol_binary_response_status err);

		bool begin_write(buffer buf);
		bool continue_write();

		bool socket_write(const unsigned char* buf, size_t len);

		void reset();

		session(const session&) = delete;
		session& operator=(const session&) = delete;
	};
}

#endif
