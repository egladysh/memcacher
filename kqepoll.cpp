#include "config.h"
#include "kqepoll.h"

#include <assert.h>
#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#include <iostream>


using namespace kq;


int kq::epoll_ctl(int epfd, int op, int fd, struct epoll_event *event)
{
	struct kevent ke;
	if (op == EPOLL_CTL_ADD) {
		if (!(event->events & EPOLLOUT)) {
			EV_SET(&ke, fd, EVFILT_READ, EV_ADD, 0, 5, event->data.ptr); //assume listening socket
		}
		else {
			EV_SET(&ke, fd, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, event->data.ptr);
		}
	}
	else if (op == EPOLL_CTL_DEL) {
		EV_SET(&ke, fd, EVFILT_READ, EV_DELETE, 0, 0, event->data.ptr);
	}
	else {
		assert(false);
		return -1;
	}
	return kevent(epfd, &ke, 1, NULL, 0, NULL);
}

int kq::epoll_create1(int flags)
{
	return ::kqueue();
}

int kq::epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout)
{
	static struct kevent ke[mc::MAX_EPOLL_EVENTS];
	assert(maxevents <= sizeof(ke)/sizeof(ke[0]));
	int n = kevent(epfd, NULL, 0, ke, sizeof(ke)/sizeof(ke[0]), NULL);
	if (n == -1) {
		assert(false);
		return -1;
	}
	for (int i = 0; i < n; ++i) {
		events[i].data.ptr = ke[i].udata;
		if (ke[i].flags & EV_ERROR)
			events[i].events = EPOLLERR;
	}
	return n;
}

