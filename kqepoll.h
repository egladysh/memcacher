#ifndef KQEPOLL_H
#define KQEPOLL_H

#if defined(USE_EPOLL)
	#include <sys/epoll.h>

#else //!USE_EPOLL

#include <sys/event.h>

namespace kq 
{

	typedef union epoll_data {
		void    *ptr;
		int      fd;
		uint32_t u32;
		uint64_t u64;
	} epoll_data_t;

	struct epoll_event {
		uint32_t     events;    /* Epoll events */
		epoll_data_t data;      /* User data variable */
	};

	const static int EPOLL_CTL_ADD = 1;
	const static int EPOLL_CTL_DEL = 2;

	enum EPOLL_EVENTS
	{
		EPOLLIN = 0x001,
		EPOLLPRI = 0x002,
		EPOLLOUT = 0x004,
		EPOLLERR = 0x008,
		EPOLLRDHUP = 0x2000,
		EPOLLHUP = 0x4000,
		EPOLLET = (1 << 31)
	};

	int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event);
	int epoll_create1(int flags);
	int epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout);
}
using namespace kq;

#endif //USE_EPOLL


#endif
