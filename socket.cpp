// socket and epoll wrappers
//
#include "socket.h"
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdexcept>
#include <sstream>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <iostream>
#include <netinet/tcp.h>

using namespace tcp;

static void set_fcntl(int fd, int flags)
{
	int f = ::fcntl(fd, F_GETFL, 0);
	if (f == -1) {
		throw std::runtime_error("fcntl error, GETFL");
	}
	f |= flags;
	f = ::fcntl(fd, F_SETFL, f);
	if (f == -1) {
		throw std::runtime_error("fcntl error, SETFL");
	}
}

static void throw_error(const char* msg) {
	std::stringstream se;
	se << msg << ": " << errno;
	throw std::runtime_error(se.str());
}

struct address
{
	struct addrinfo *info_; //list of items

	address(const std::string& ip, int port)
	{
		struct addrinfo v; //hints
	    memset (&v, 0, sizeof(struct addrinfo));
		v.ai_family = AF_UNSPEC; // IPv4 and IPv6
		v.ai_socktype = SOCK_STREAM; // TCP
		v.ai_flags = AI_PASSIVE; // all interfaces

		std::stringstream ss;
		ss << port;

		int err = ::getaddrinfo(!ip.empty()?ip.c_str():NULL, ss.str().c_str(), &v, &info_);
		if (err) {
			std::stringstream es;
			es << "Unable to getaddrinfo: " << err;
			throw std::runtime_error(es.str());
		}
		assert(info_);
	}

	~address()
	{
		::freeaddrinfo(info_);
	}

	address(const address&) = delete;
	address& operator=(const address&) = delete;
};

socket::socket(const std::string& ip, int port)
	:fd_(-1)
{
    struct linger lng = {0, 0};
    int flags =1;

	address addr(ip, port);

	for (struct addrinfo *info = addr.info_; info != nullptr; info = info->ai_next) {
		int fd = ::socket (info->ai_family, info->ai_socktype, info->ai_protocol);
		if (fd == -1)
			continue;

		int err = setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (void *)&flags, sizeof(flags));
		if (err != 0) {
			std::cerr << "setsockopt error" << std::endl;
		}
		err = setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (void *)&flags, sizeof(flags));
		if (err != 0) {
			std::cerr << "setsockopt error" << std::endl;
		}
		err = setsockopt(fd, SOL_SOCKET, SO_LINGER, (void *)&lng, sizeof(lng));
		if (err != 0) {
			std::cerr << "setsockopt error" << std::endl;
		}

		err = ::bind(fd, info->ai_addr, info->ai_addrlen); 
		if (!err) //bind worked
		{
			fd_ = fd;
			break;
		}
		::close(fd);
	}

	if (fd_ == -1) {
		throw_error("socket bind error");
	}
}

socket::~socket()
{
	// count on RAII
	::close(fd_);
}

void socket::set_non_blocking()
{
	set_fcntl(fd_, O_NONBLOCK);
}

void socket::listen()
{
	int err = ::listen(fd_, SOMAXCONN);
	if (err == -1) {
		throw_error("socket listen error");
	}
}


	// epoll

epoll::epoll(int max_events)
{
	events_.resize(max_events); //allocate events buffer
	for (auto& v : events_) {
		v.data.ptr = nullptr;
	}
	fd_ = epoll_create1(0);
	if (fd_ == -1) {
		throw_error("epoll_create error");
	}
}

epoll::~epoll()
{
	::close(fd_);
}

void epoll::listen_socket(tcp::socket& s)
{
	struct epoll_event event;
	event.data.ptr = (void*)&s;
	event.events = EPOLLIN | EPOLLET;
	int err = epoll_ctl(fd_, EPOLL_CTL_ADD, s.fd_, &event);
	if (err == -1) {
		throw_error("listen epoll_ctl  error");
	}
	s.listen();
}

void epoll::add_descriptor(int fd, void* user)
{
	struct epoll_event event;
	event.data.ptr = user;
	event.events = EPOLLOUT | EPOLLIN | EPOLLET;
	int err = epoll_ctl(fd_, EPOLL_CTL_ADD, fd, &event);
	if (err == -1) {
		throw_error("add epoll_ctl error");
	}
}

void epoll::remove_descriptor(int fd)
{
	struct epoll_event event;
	event.events = EPOLLIN | EPOLLET | EPOLLOUT;
	int err = epoll_ctl(fd_, EPOLL_CTL_DEL, fd, &event);
	if (err == -1) {
		throw_error("add epoll_ctl error");
	}
}

int epoll::wait()
{
	int n = ::epoll_wait(fd_, &events_.at(0), events_.size(), -1);
	if (n == -1) {
		throw_error("epoll wait error");
	}
	return n;
}

bool tcp::accept_connection(connection_info& info, tcp::socket& s, tcp::epoll& ep)
{
	struct sockaddr in_addr;
	socklen_t in_len = sizeof(in_addr);
	info.fd_ = ::accept(s.fd_, &in_addr, &in_len);
	if (info.fd_ == -1)
	{
		if ( (errno == EAGAIN) || (errno == EWOULDBLOCK)) {
			// no incoming connections
			return false;
		}
		else {
			throw_error("incoming connection error");
		}
	}

	char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];
	int err = ::getnameinfo(&in_addr, in_len
			,hbuf, sizeof(hbuf)
			,sbuf, sizeof(sbuf)
			,NI_NUMERICHOST | NI_NUMERICSERV);

	if (err) 
		throw_error("getnameinfo");

	info.host_ = std::string(hbuf);
	info.port_ = std::string(sbuf);

	set_fcntl(info.fd_, O_NONBLOCK);

	return true;
}
