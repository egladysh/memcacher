#ifndef MC_CONFIG_H
#define MC_CONFIG_H

#include <vector>
#include <assert.h>

namespace mc
{
	static const char VER[]="1.0";

	static const size_t MAX_KEYLEN = 250;
	static const size_t MAX_VALUELEN = 1024*1024;
	static const size_t MAX_WRITE_SIZE = 4*1204;
	static const size_t MAX_EPOLL_EVENTS = 128;

	struct sysevent
	{
		enum event_type
		{
			system
			,session
		};

		event_type t_;
		void *user_;

		explicit sysevent(event_type t, void* u)
			:t_(t)
			,user_(u)
		{}
	};

	typedef std::vector<unsigned char> buffer;

	inline buffer serialize_sysevent(const sysevent& c)
	{
		
		return buffer(reinterpret_cast<const unsigned char*>(&c)
				,reinterpret_cast<const unsigned char*>(&c) + sizeof(c)
				);
	}
	inline sysevent deserialize_sysevent(const buffer& b)
	{
		assert(b.size() == sizeof(sysevent));
		return sysevent(*reinterpret_cast<const sysevent*>(b.data()));

	}
}

#endif

