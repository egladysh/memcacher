#include "cache.h"
#include "session.h"
#include "murmur3_hash.h"
#include <assert.h>
#include <algorithm>
#include <stdexcept>
#include <iostream>

using namespace mc;

size_t cache::hasher::operator()(const key& k) const
{
	return MurmurHash3_x86_32(k.d_, k.len_);
}

cache::cache(size_t maxmemsize)
	:maxmemsize_(maxmemsize)
	,used_mem_(0)
{
	assert(maxmemsize);

	//some initial hints for the hash
	//assuming the average value size is 1% of the max
	size_t itemmem = (MAX_VALUELEN + MAX_KEYLEN)/100 + sizeof(protocol_binary_request_header);
	if (itemmem) {
		size_t items = maxmemsize_/itemmem;
		std::clog << "cache params: itemmem=" << itemmem << " maxmemsize=" << maxmemsize_ << " items=" << items << std::endl;

		h_.reserve(items); //this is just a hint for the hash table to pre-allocate some buckets
	}
}

cache::~cache()
{
}

void cache::set(item v)
{
	key k = v.get_key();
	
	auto it = h_.find(k);
	if (it != h_.end()) {
		delete_item(k);
	}

	size_t itemmem = (sizeof(v.h_) + v.h_.request.bodylen);

	if (itemmem + used_mem_ > maxmemsize_) {
		//try to free at least 1% of the max
		free_mem(std::max(itemmem*2, maxmemsize_/100));
	}

	auto lruit = lru_.insert(lru_.end(), k); //add to the LRU
	v.set_lru(lruit);

	try {
		h_.emplace(std::make_pair(k, std::move(v)));
	}
	catch (const std::exception&)
	{
		lru_.erase(lruit);
		throw;
	}
	used_mem_ += itemmem;
}

const cache::item* cache::get(const key& k)
{
	auto it = h_.find(k);
	if (it == h_.end())
		return nullptr;

	assert(!lru_.empty());

	//refresh in the LRU list
	if (it->second.lru_ref_ != --lru_.end()) {
		lru_.splice(lru_.end(), lru_, it->second.lru_ref_);
	}
	
	return &(it->second);
}

void cache::delete_item(key k) //pass by value
{
	auto it = h_.find(k);
	if (it == h_.end()) {
		throw std::runtime_error("cache integrity error");
	}

	//remove from LRU
	lru_.erase(it->second.lru_ref_);

	h_.erase(it);

	used_mem_ -= k.memsize_;
}

void cache::free_mem(size_t size) //size to free
{
	assert(size);

	size_t freed = 0;
	//remove according to LRU
	for (auto it = lru_.begin(); it != lru_.end() && freed < size; ) {
		auto n = it->memsize_;
		auto tmp = it;
		++it;
		delete_item(*tmp);
		freed += n;
	}
}

