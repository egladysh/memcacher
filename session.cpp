// session and request processing
//
#include "session.h"
#include <assert.h>
#include <unistd.h>
#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <algorithm>

#if !defined(__APPLE__)
	#include <endian.h>
	#include <byteswap.h>
#endif

using namespace mc;

namespace
{

#if !defined(__APPLE__)
	unsigned long long htonll(unsigned long long val)
	{
		if (__BYTE_ORDER == __BIG_ENDIAN) return (val);
		else return __bswap_64(val);
	}

	unsigned long long ntohll(unsigned long long val)
	{
		if (__BYTE_ORDER == __BIG_ENDIAN) return (val);
		else return __bswap_64(val);
	}
#endif

	session::buffer make_response_header(const protocol_binary_request_header& h
			,unsigned int err, unsigned char extlen, unsigned short keylen, unsigned int body_len)
	{
		protocol_binary_response_header r;
		memset(&r, 0, sizeof(r));

		r.response.magic = (uint8_t)PROTOCOL_BINARY_RES;
		r.response.opcode = h.request.opcode;
		r.response.keylen = (uint16_t)htons((uint16_t)keylen);

		r.response.extlen = (uint8_t)extlen;
		r.response.datatype = (uint8_t)PROTOCOL_BINARY_RAW_BYTES;
		r.response.status = (uint16_t)htons((uint16_t)err);

		r.response.bodylen = htonl((uint32_t)body_len);
		r.response.opaque = h.request.opaque;
		r.response.cas = htonll(h.request.cas);
		return session::buffer((unsigned char*)&r, (unsigned char*)&r + sizeof(r));
	}

	// for logs
	/*
	void print_header(const protocol_binary_request_header& h)
	{
		std::clog << "op=" << (unsigned int)h.request.opcode
			<< " extlen=" << (unsigned int)h.request.extlen
			<< " keylen=" << (unsigned int)h.request.keylen
			<< " bodylen=" << (unsigned int)h.request.bodylen
			<< " cas=" << (unsigned int)h.request.cas
			<< std::endl;
	}
	*/
}

session::session(int fd, int ctl_pipe, void* user, cache& c)
	:fd_(fd)
	,ctl_pipe_(ctl_pipe)
	,user_(user)
	,c_(c)
{
	assert(fd_ != -1);
}
session::~session()
{
	::close(fd_);
}

bool session::control(buffer b) //control event on the session
{
	if (!wctl_.is_active()) {
		assert(false);
		return false; //must only be write controls for now
	}
	return continue_write();
}

//returns false if the session is to be closed
bool session::process_chunk(buffer b)
{
	static_assert(sizeof(header_.bytes) == sizeof(protocol_binary_request_header), "the compiler doesn't pack the protocol types" );

	if (b.empty())
		return true;

	if (wctl_.is_active()) { //received data while a write is in progress, not good
		assert(false);
		return false; //will close the session
	}
	size_t old_size = request_.size();

	if (request_.empty()) { //new request?
		//check the magic number
		if (b[0] != PROTOCOL_BINARY_REQ) {
			return false; //close session
		}
	}
	
	request_.insert(request_.end(), b.begin(), b.end()); 

	//wait for complete header
	if (request_.size() < sizeof(protocol_binary_request_header))
		return true;

	if (old_size < sizeof(header_)) { //got full header
		protocol_binary_request_header* h = (protocol_binary_request_header*)(&request_[0]);

		memcpy(&header_, h, sizeof(header_));

		header_.request.keylen = ntohs(h->request.keylen);
		header_.request.bodylen = ntohl(h->request.bodylen);
		header_.request.cas = ntohll(h->request.cas);

		if (!validate_request())
			return false;
	}

	return handle_request();
}

bool session::handle_request_delete()
{
	cache::item item(std::move(request_), header_);

	try {
		if (!c_.remove(item, header_.request.cas)) {
			error_response(PROTOCOL_BINARY_RESPONSE_KEY_EEXISTS);
			return true;
		}

		//generate response
		buffer resp = make_response_header(header_, 0, 0, 0, 0);
		if (!socket_write(resp.data(), resp.size())) {
			return false;
		}
	}
	catch(const std::exception& e) { //some system error
		std::cerr << e.what() << std::endl;
		return false; //log and disconnect
	}
	return true;
}

bool session::handle_request_set()
{
	cache::item item(std::move(request_), header_);

	try {
		if (header_.request.cas) {
			if (!c_.cas(std::move(item), header_.request.cas)) {
				error_response(PROTOCOL_BINARY_RESPONSE_KEY_EEXISTS);
				return true;
			}
		}
		else {
			c_.set(std::move(item));
		}

		//generate response
		buffer resp = make_response_header(header_, 0, 0, 0, 0);
		if (!socket_write(resp.data(), resp.size())) {
			return false;
		}
	}
	catch(const std::exception& e) { //some system error
		std::cerr << e.what() << std::endl;
		return false; //log and disconnect
	}
	return true;
}

bool session::handle_request_get()
{
	typedef uint32_t flag_t;

	std::shared_ptr<cache::item> itm;

	{ //find item
		cache::item req(std::move(request_), header_);
		itm = c_.get(req.get_key());
		if (!itm) {
			error_response(PROTOCOL_BINARY_RESPONSE_KEY_ENOENT);
			return true;
		}
	}

	wctl_.item_ = itm;
	size_t value_len = itm->get_value_len();

	{ //place header and flags
		flag_t f = 0;

		buffer hdr = make_response_header(header_, 0, sizeof(f), 0, value_len + sizeof(f));
		hdr.insert(hdr.end(), (unsigned char*)&f, (unsigned char*)&f+sizeof(f));
		size_t first_packet_size = std::min(MAX_WRITE_SIZE, value_len);

		const unsigned char *buf = itm->get_data() + itm->h_.request.keylen;
		hdr.insert(hdr.end(), buf, buf + first_packet_size);

		wctl_.hdr_.swap(hdr);
		wctl_.offset_ = first_packet_size;
	}

	// write the response
	if (!continue_write()) {
		return false;
	}

	return true;
}

//the request header is ready by now
bool session::handle_request()
{
	//wait for complete packet
	if (request_.size() > header_.request.bodylen + sizeof(header_)) { //packet tool large
		error_response(PROTOCOL_BINARY_RESPONSE_EINVAL);
		return false;
	}
	else if (request_.size() < header_.request.bodylen + sizeof(header_)) { //wait completion
		return true;
	}

	//got complete packet
	bool ret = true;
	switch (header_.request.opcode) {
		case PROTOCOL_BINARY_CMD_SET:
			ret=handle_request_set();
			break;
		case PROTOCOL_BINARY_CMD_GET:
			ret=handle_request_get();
			break;
		case PROTOCOL_BINARY_CMD_DELETE:
			ret=handle_request_delete();
			break;
		default:
			error_response(PROTOCOL_BINARY_RESPONSE_UNKNOWN_COMMAND);
			break;
	}
	reset();
	return ret;
}


bool session::validate_request()
{
	bool ok = true;
	switch (header_.request.opcode) {
		case PROTOCOL_BINARY_CMD_SET:
            if (header_.request.extlen != 8 
					|| header_.request.keylen == 0 
					|| header_.request.bodylen < header_.request.keylen + 8
					|| header_.request.keylen > MAX_KEYLEN
				) {
				error_response(PROTOCOL_BINARY_RESPONSE_EINVAL);
				ok = false;
			}
			if (header_.request.bodylen > MAX_VALUELEN + header_.request.keylen + 8) {
				error_response(PROTOCOL_BINARY_RESPONSE_E2BIG);
				ok = false;
			}
			break;
		case PROTOCOL_BINARY_CMD_GET:
            if (header_.request.extlen != 0 
					|| header_.request.keylen == 0 
					|| header_.request.bodylen != header_.request.keylen
				) {
				error_response(PROTOCOL_BINARY_RESPONSE_EINVAL);
				ok = false;
			}
			break;
		case PROTOCOL_BINARY_CMD_DELETE:
            if (header_.request.extlen != 0 
					|| header_.request.keylen == 0 
					|| header_.request.bodylen != header_.request.keylen
				) {
				error_response(PROTOCOL_BINARY_RESPONSE_EINVAL);
				ok = false;
			}
			break;
		default:
			error_response(PROTOCOL_BINARY_RESPONSE_UNKNOWN_COMMAND);
			break;
	}
	return ok;
}


void session::error_response(protocol_binary_response_status err)
{
	const char *errstr = nullptr;

	switch (err) {
		case PROTOCOL_BINARY_RESPONSE_KEY_EEXISTS:
			errstr = "Entry exists for key";
			break;
		case PROTOCOL_BINARY_RESPONSE_KEY_ENOENT:
			errstr = "Not found";
			break;
		case PROTOCOL_BINARY_RESPONSE_EINVAL:
			errstr = "Bad parameters";
			break;
		case PROTOCOL_BINARY_RESPONSE_UNKNOWN_COMMAND:
			errstr = "Unsupported command";
			break;
		case PROTOCOL_BINARY_RESPONSE_E2BIG:
			errstr = "Too large";
			break;
		default:
			assert(false);
			break;
	}

	size_t len = 0;
	if (errstr)
		len = strlen(errstr);
	buffer buf = make_response_header(header_, err, 0, 0, len);
	if (len)
		buf.insert(buf.end(), errstr, errstr + len);

	socket_write(&buf[0], buf.size());

	reset();
}

bool session::continue_write()
{
	if (!wctl_.is_active()) {
		assert(false);
		return false;
	}

	//write a chunk
	auto pnt = wctl_.next();
	size_t len = std::min(MAX_WRITE_SIZE, pnt.second);
	if (!len) {
		wctl_.reset();
		return true;
	}

	ssize_t cnt = ::write(fd_, pnt.first, len);
	if (cnt == -1) {
		if (errno != EAGAIN && errno != EWOULDBLOCK) {
			std::cerr << "continue write error: fd=" << fd_ << " errno=" << errno << std::endl;
			return false;
		}
	}
	else if (!cnt) {
		return false;
	}

	//move pointers
	wctl_.move(cnt);

	if (!wctl_.is_active()) { //done writing
		wctl_.reset();
		return true;
	}

	//schedule a write control event, it'll give an opportunity
	//to other sessions handle stuff
	mc::sysevent wrtctl(mc::sysevent::session, this);
	buffer b = serialize_sysevent(wrtctl);
	cnt = ::write(ctl_pipe_, &b[0], b.size());
	assert(cnt != -1);
	return true;
}

bool session::socket_write(const unsigned char* buf, size_t len)
{
	while (len) {
		ssize_t cnt = ::write(fd_, buf, len);
		if (cnt == -1) {
			if (errno != EAGAIN && errno != EWOULDBLOCK) {
				std::cerr << "write error: fd=" << fd_ << " errno=" << errno << std::endl;
				return false;
			}
		}
		else {
			assert(cnt <= len);
			len -= cnt;
		}
	}
	return true;
}

void session::reset()
{
	request_.clear(); 
}

