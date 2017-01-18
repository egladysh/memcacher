#ifndef SOCKET_WRAP_H
#define SOCKET_WRAP_H

#include <sys/types.h>
#include <vector>
#include <string>

#include "kqepoll.h"

namespace tcp
{
	struct socket
	{
		int fd_; //socket 

		explicit socket(const std::string& ip, int port);
		~socket();

		void set_non_blocking();

		void listen();

	protected:
		socket() //used by derived types
			:fd_(-1)
		{}

	private:
		//cannot copy
		socket(const socket&) = delete;
		socket& operator=(const socket&) = delete;
	};

	struct epoll
	{
		typedef std::vector<epoll_event> events;

		int fd_;
		events events_;

		explicit epoll(int max_events);
		~epoll();

		void listen_socket(socket& s);

		void add_descriptor(int fd, void* user);
		void remove_descriptor(int fd);
		int wait();

	private:
		//cannot copy
		epoll(const epoll&) = delete;
		epoll& operator=(const epoll&) = delete;
	};


	// returns false if no connections on this socket
	struct connection_info
	{
		int fd_;
		std::string host_;
		std::string port_;
	};
	bool accept_connection(connection_info& info, tcp::socket& s, tcp::epoll& ep);
}

#endif
