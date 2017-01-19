#ifndef MC_CACHE_H
#define MC_CACHE_H

#include <assert.h>
#include <unordered_map>
#include <vector>
#include <list>
#include <string.h>
#include <functional>
#include <mutex>
#include <memory>
#include "protocol_binary.h"
#include "config.h"

namespace mc
{
	struct cache
	{
		struct key
		{
			const unsigned char* d_; //key data
			size_t len_; //key len
			size_t memsize_; //size of key+value in memory (bytes)

			explicit key(const unsigned char* d, size_t len, size_t memsize)
				:d_(d)
				,len_(len)
				,memsize_(memsize)
			{}
			explicit key(const unsigned char* d, size_t len)
				:d_(d)
				,len_(len)
			{}

			bool operator==(const key& k) const
			{
				if (this == &k)
					return true;
				if (len_ != k.len_)
					return false;
				if (len_ == 0)
					return true;
				return ::memcmp(d_, k.d_, len_) == 0;
			}
		};

		typedef std::list<key> lru; //LRU linked list

		struct item
		{
			typedef std::vector<unsigned char> data;
			data d_; 
			protocol_binary_request_header h_;
			lru::iterator lru_ref_; //location in LRU list (list iterators are valid till deleted)

			explicit item(data d, const protocol_binary_request_header& h)
				:d_(d)
				,h_(h)
			{
				assert(d.size() >= h_.request.extlen + sizeof(h_));
			}
			item(item&& v)
				:d_(std::move(v.d_))
				,h_(v.h_)
				,lru_ref_(v.lru_ref_)
			{}

			key get_key() const
			{
				return key(
						d_.data() + sizeof(h_) + h_.request.extlen
						,h_.request.keylen
						,d_.size()
						);
			}
			const unsigned char* get_data() const
			{
				return d_.data() + h_.request.extlen + sizeof(h_);
			}
			const size_t get_data_len() const
			{
				return d_.size() - h_.request.extlen - sizeof(h_);
			}

			void set_lru(lru::iterator it)
			{
				lru_ref_ = it;
			}

		private:
			//never copy data around, move only
			item(const item&) = delete;
			item& operator=(const item&) = delete;
		};


		struct hasher
		{
			size_t operator()(const key& k) const;
		};

		typedef std::unordered_map<key, item, hasher> hash;

		cache(size_t maxmemsize, bool thread_safe);
		~cache();

		//may throw
		void set(item v);
		bool cas(item v, uint64_t cas);

		bool get_value(std::vector<unsigned char>& v, const key& k);
	
	private:
		std::unique_ptr<std::mutex> m_;

		size_t maxmemsize_;
		size_t used_mem_;

		hash h_;
		lru lru_;

		void do_set(item v);
		bool do_cas(item v, uint64_t cas);

		bool do_get_value(std::vector<unsigned char>& v, const key& k);
		const item* do_get(const key& k);

		void delete_item(key k);
		void free_mem(size_t size);


		cache(const cache&) = delete;
		cache& operator=(cache&) = delete;
	};

}

#endif
