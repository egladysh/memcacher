#ifndef MC_SERVER_H
#define MC_SERVER_H

#include <memory>
#include <thread>
#include <map>
#include "config.h"
#include "session.h"
#include "safe_queue.h"
#include <ctime>

namespace mc
{
	// this will do the actual server job
	
	struct server
	{
		struct data_chunk
		{
			typedef mc::buffer buffer;

			enum type
			{
				ctl_read //new data in buffer
				,ctl_session_ctl //session control, just calls a session control method
				,ctl_close //close session
				,ctl_new_session
				,ctl_shutdown //shutdown server
			};


			type t_;
			session* s_; //session
			buffer b_;

			explicit data_chunk(type t, session* s, buffer b)
				:t_(t)
				,s_(s)
				,b_(std::move(b))
			{}
			explicit data_chunk(type t, session* s)
				:t_(t)
				,s_(s)
			{}

			// move c'tor
			data_chunk(data_chunk&& d)
				:t_(d.t_)
				,s_(d.s_)
				,b_(std::move(d.b_))
			{
			}

		private:
			//make sure we never copy data chunks, move only
			data_chunk(const data_chunk&) = delete;
			data_chunk& operator=(const data_chunk&) = delete;
		};

		typedef safe_queue<data_chunk> queue;

		queue q_;
		bool thread_;

		explicit server(bool enable_thread = true);
		~server();

		void start();


		void push(data_chunk d) //called by the producer
		{
			if (t_) {
				q_.push(std::move(d));
			}
			else {
				handle_chunk(d);
			}
		}


	private:
		std::unique_ptr<std::thread> t_;
		server(const server&) = delete;
		server& operator=(const server&) = delete;
		typedef std::unordered_map<session*, std::time_t> sessions;
		sessions sessions_; //active sessions

		bool handle_chunk(const data_chunk& v);

		void register_session(session *s);
		void handle_close(session* s);
		void read_data(session* s, buffer b);
		void control_session(session* s, buffer b);

		std::pair<bool, sessions::iterator> is_active_session(session* s);

		void close_session(sessions::iterator sit);
		void cleanup();

		void process(); //main process, executed in a thread
	};
}

#endif
