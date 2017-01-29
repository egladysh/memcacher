#include <iostream>
#include <stdexcept>
#include <string>
#include <assert.h>
#include <sstream>
#include <ctype.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>

#include "config.h"
#include "socket.h"
#include "server.h"
#include "round_robin.h"
#include "cache.h"
#include "pipe.h"


extern int daemonize(int nochdir, int noclose);

static const char* appname = 0;

static bool is_event_error(const epoll_event& e)
{
	return ((e.events & EPOLLERR)|| (e.events & EPOLLHUP))?true:false; //  || (!(e.events & EPOLLIN));
}

typedef std::shared_ptr<mc::server> server_ptr;
typedef std::vector<server_ptr> servers;
static void accept_incoming_connections(int ctl_pipe, tcp::socket& s, tcp::epoll& ep, mc::round_robin<servers>& server_pool);

//global cache
std::auto_ptr<mc::cache> g_cache;

// this will listen for connections and push the incoming data chunks to mc::server for processing
static void server_loop(tcp::socket& s, unsigned int maxevents, unsigned int threads, unsigned int max_connections)
{
	assert(maxevents);

	// create server pool
	servers srvs;
	if (threads > 1) {
		for (unsigned int i = 1; i != threads; ++i) {
			// mc::server will do the actual job on its own thread
			server_ptr p(new mc::server());
			p->start();
			srvs.push_back(p);
		}
	}
	else {
		server_ptr p(new mc::server(false));
		p->start();
		srvs.push_back(p);
	}
	mc::round_robin<servers> server_pool(std::move(srvs));

	tcp::epoll ep(maxevents); //we'll use epoll
	// start listening
	ep.listen_socket(s);

	//system/session control event pipe
	//note: posix guarantees atomic read/write on pipes up to 512 bytes
	//so we should be fine
	mc::pipe sysctl;
	{ //add it to epoll
		static_assert(sizeof(mc::sysevent) < 512, "control event is too big");
		struct epoll_event event;
		event.data.ptr = &sysctl;
		event.events = EPOLLOUT | EPOLLIN;
		int err = epoll_ctl(ep.fd_, EPOLL_CTL_ADD, sysctl.read_end(), &event);
		if (err == -1) {
			throw std::runtime_error("control pipe add error");
		}
	}

	// the loop "never" stops
	// TODO: come up with a graceful shutdown
	while (true) {
		// wait for events
		int n = ep.wait();

		// handle events
		for (int i = 0; i < n; ++i) {
			epoll_event& e = ep.events_[i];

			if (is_event_error(e)) {
				if (&s != static_cast<tcp::socket*>(e.data.ptr)) { //not listening socket, close the session
					if (e.data.ptr) { //clean up the active session
						mc::session* ses = static_cast<mc::session*>(e.data.ptr);
						std::cerr << "connection event error: " << ses->fd_ << std::endl;
						// tell the server that the session is to closed
						static_cast<mc::server*>(ses->user_)->push(mc::server::data_chunk(mc::server::data_chunk::ctl_close, ses));
					}
				}
				else {
					std::cerr << "socket event error: " << s.fd_ << std::endl;
				}
				continue;
			}

			if (&s == static_cast<tcp::socket*>(e.data.ptr)) { //event on the listening socket means a new connection
				accept_incoming_connections(sysctl.write_end(), s, ep, server_pool);
			}

			else if (&sysctl == static_cast<mc::pipe*>(e.data.ptr)) { //session control event
				mc::buffer buf;
				buf.resize(sizeof(mc::sysevent));

				while(true) { //read the control pipe while there are control events
					ssize_t count = ::read(sysctl.read_end(), &buf[0], buf.size());
					if(count == -1) {
						if (errno == EAGAIN || errno == EWOULDBLOCK) {
							break;
						}
						else {
							std::stringstream ss;
							ss << "control pipe error: " << errno << std::endl;
							throw std::runtime_error(ss.str());
						}
					}
					else if(!count) {
						break;
					}
					mc::sysevent se = mc::deserialize_sysevent(buf);

					//session control type
					if (se.t_ == mc::sysevent::session) {
						mc::session* ses = static_cast<mc::session*>(se.user_);
						assert(ses);
						static_cast<mc::server*>(ses->user_)->push(mc::server::data_chunk(mc::server::data_chunk::ctl_session_ctl, ses));
					}
				}
			}

			else { //incoming data on one of the sessions
				mc::session* ses = static_cast<mc::session*>(e.data.ptr);

				if (!ses) { //normally this shouldn't happen
					std::cerr << "unlinked session" << std::endl;
					continue;
				}

				// read data and enqueue it for processing by the server
				// it should be pretty fast, we don't actually do any data copies here
				bool closed  = true; //assume closed unless we get valid data count
				while(true) {
					mc::server::data_chunk::buffer buf;
					buf.resize(512); //512b max chunks

					ssize_t count = ::read(ses->fd_, &buf[0], buf.size());
					if (count == -1) {
						if (errno != EAGAIN) {
							std::cerr << "read error: " << ses->fd_ << std::endl;
							count = 0;
						}
						else {
							break; // all read
						}
					}
					else if (count) { //valid count
						assert(count <= buf.size());
						closed = false;
					}

					if (!closed && !count) //nothing to send
						break;

					if (!closed) {
						//resize according to the actual count, it won't reallocate anything...
						buf.erase(buf.begin()+count, buf.end());

						//hand the chunk over to the server
						static_cast<mc::server*>(ses->user_)->push(mc::server::data_chunk(mc::server::data_chunk::ctl_read, ses, std::move(buf)));
					}
					else {
						assert(!count);
						// tell the server that the session is to closed
						static_cast<mc::server*>(ses->user_)->push(mc::server::data_chunk(mc::server::data_chunk::ctl_close, ses));
						break;
					}
				}
			}
		}
	}
}

static void accept_incoming_connections(int ctl_pipe, tcp::socket& s, tcp::epoll& ep, mc::round_robin<servers>& server_pool)
{
	try {
		tcp::connection_info info;
		while (tcp::accept_connection(info, s, ep)) { //accept all connections
			//pick a server and create session...
			//sessions are deleted by the server always
			mc::server* server = server_pool.pick().get();
			mc::session* ses = new mc::session(info.fd_, ctl_pipe, server, *g_cache); 
			try {
				ep.add_descriptor(info.fd_, ses);
				server->push(mc::server::data_chunk(mc::server::data_chunk::ctl_new_session, ses)); //notify server about a new session
			}
			catch (const std::exception&) {
				delete ses;
				throw;
			}
		} 
	}
	catch (const std::exception& e) { //an accept failed, log and continue
		std::cerr << e.what() << std::endl;
	}
}

static void usage_help()
{
	std::cerr << "Ver: " << mc::VER << " Usage: " << appname << "[options]" << std::endl
        << "  -d run as a daemon" << std::endl
		<< "  -l IP address of the listening socket, default 124.0.0.1" << std::endl
		<< "  -p Port number, default is 11211" << std::endl
		<< "  -t Number of threads, default is 1" << std::endl
		<< "  -m Max cache memory (MB), default is 500" << std::endl
		<< "  -c Max number of simultaneous connections, default is 1024" << std::endl
		<< "Example:" << std::endl
		<< " " << appname << " -p 5000 -t 2 -m 100" << std::endl
		<< std::endl;
}

static unsigned int parse_number(const char* p)
{
	if (!*p) {
		throw std::runtime_error("missing number in a numeric option");
	}
	unsigned int n = 0;
	std::stringstream ss;
	for (; *p; ++p) {
		if (!::isdigit(*p)) { //make sure valid digits
			throw std::runtime_error("bad number in a numeric option");
		}
		ss << *p;
	}
	ss >> n;
	return n;
}

int main(int argc, char* argv[])
{
    ::sigignore(SIGPIPE); //ignore this signal
    
	//appname = argv[0]; //TODO fix it to get rid of the full path
	appname = "memcacher";

	unsigned int port = 11211; //default port
	unsigned int threads = 1; //number threads
	unsigned int cachemem = 500; //~max memory for the cache in MB
	unsigned int max_connections = 1024;
	std::string ip = ""; //default 127.0.0.1
	bool daemon_mode = false;

	// parse command line
	try {
		for (int i = 1; i < argc; ++i) {
			const char* arg = argv[i];
			if (!*arg) 
				continue;
			if (arg[0] != '-' || !arg[1]) {
				throw std::runtime_error("bad command line");
			}

			if (arg[2]) {
				throw std::runtime_error("bad command line");
			}

			switch (arg[1])
			{
				case 'd':
					daemon_mode = true;
					break;
				case 'p': //parse port number
					if (i + 1 == argc) {
						throw std::runtime_error("bad command line");
					}
					port = parse_number(argv[++i]);
					break;
				case 't': //parse working thread number
					if (i + 1 == argc) {
						throw std::runtime_error("bad command line");
					}
					threads = parse_number(argv[++i]);
					break;
				case 'c': //parse working thread number
					if (i + 1 == argc) {
						throw std::runtime_error("bad command line");
					}
					max_connections = parse_number(argv[++i]);
					if (!max_connections) {
						throw std::runtime_error("max connections must be positive number");
					}
					break;
				case 'm': //parse cache size
					if (i + 1 == argc) {
						throw std::runtime_error("bad command line");
					}
					cachemem = parse_number(argv[++i]);
					if (!cachemem) {
						throw std::runtime_error("Bad cache memory size");
					}
					break;
				case 'l': //parse listen IP
					if (i + 1 == argc) {
						throw std::runtime_error("bad command line");
					}
					ip = std::string(argv[++i]);
					break;
				default:
					throw std::runtime_error("unsupported option");
			}
		}
	}
	catch (const std::exception& e) {
		std::cerr << e.what() << std::endl;
		usage_help();
		return 1;
	}

    if (daemon_mode) {
        if (sigignore(SIGHUP) == -1) {
            perror("Failed to ignore SIGHUP");
        }
        if (daemonize(0, 0) == -1) {
			std::cerr << "failed to daemonize" << std::endl;
			return 1;
        }
    }

	std::clog << "ver: " << mc::VER << " listen: " << ip << ":" << port << " threads:" << threads << " cachmem:" << cachemem << "MB" << " connections:" << max_connections << std::endl;
	
	try {
		//allocate cache
		g_cache.reset(new mc::cache(cachemem*1024*1024, threads > 1));

		// bind a TCP socket
		tcp::socket s(ip, port);
		s.set_non_blocking();
		std::clog << "socket created..." << std::endl;

		// run it
		server_loop(s, mc::MAX_EPOLL_EVENTS, threads, max_connections);
	}
	catch (const std::exception& e) {
		std::clog << e.what() << std::endl;
		return 1;
	}


	return 0;
}

