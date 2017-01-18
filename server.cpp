// server thread and session management 
//
#include "server.h"
#include <functional>
#include <iostream>


using namespace mc;

server::server(bool thread)
	:thread_(thread)
{
}

server::~server()
{
	push(data_chunk(data_chunk::ctl_shutdown, nullptr));
	if (t_.get())
		t_->join();
}

void server::start()
{
	if (thread_)
		t_.reset( new std::thread(std::bind(&server::process, this)) );
}

void server::register_session(session *s)
{
	assert(sessions_.find(s) == sessions_.end());
	//mark session activity time, just in case we want to enforce an idle timeout later
	sessions_[s] = std::time(NULL);
}

void server::handle_close(session* s)
{
	auto v = is_active_session(s);
	if (!v.first)
		return;
	close_session(v.second);
}

void server::read_data(session* s, buffer b)
{
	auto v = is_active_session(s);
	if (!v.first) {
		std::clog << "inactive read" << std::endl;
		return;
	}
	// process data
	auto it = v.second;
	try {
		if (!it->first->process_chunk(std::move(b))) {
			close_session(it);
		}
	}
	catch (const std::exception& e) {
		std::cerr << e.what() << std::endl;
		close_session(it);
	}
}

void server::control_session(session* s, buffer b)
{
	auto v = is_active_session(s);
	if (!v.first)
		return;
	auto it = v.second;
	try {
		if (!it->first->control(std::move(b))) {
			close_session(it);
		}
	}
	catch (const std::exception& e) {
		std::cerr << e.what() << std::endl;
		close_session(it);
	}
}

bool server::handle_chunk(const data_chunk& v)
{
	assert(v.s_);

	switch (v.t_)
	{
		case data_chunk::ctl_new_session:
			assert(v.b_.empty());
			register_session(v.s_);
			break;
		case data_chunk::ctl_read:
			read_data(v.s_, std::move(v.b_));
			break;
		case data_chunk::ctl_session_ctl:
			control_session(v.s_, std::move(v.b_));
			break;
		case data_chunk::ctl_close:
			assert(v.b_.empty());
			handle_close(v.s_);
			break;
		case data_chunk::ctl_shutdown:
			return false;
		default:
			assert(false);
			break;
	}
	return true;
}

std::pair<bool, server::sessions::iterator> server::is_active_session(session* s)
{
	auto it = sessions_.find(s);
	if (it == sessions_.end()) { //session isn't alive
		return std::make_pair(false, it);
	}
	if (it->first->fd_ != s->fd_) { //additional paranoid check
		return std::make_pair(false, it);
	}
	it->second = std::time(NULL); 
	return std::make_pair(true, it);
}
void server::close_session(sessions::iterator sit)
{
	delete sit->first; //will close the socket
	sessions_.erase(sit);
}

void server::cleanup()
{
	for(auto& v: sessions_) {
		delete v.first;
	}
	sessions_.clear();
}

void server::process() //main process thread
{
	while(true) {
		auto v = q_.wait_next();
		assert(v.s_);
		if (!handle_chunk(v))
			break;
	}
	cleanup();
}

