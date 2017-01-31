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

cache::cache(size_t maxmemsize, bool thread_safe)
	:maxmemsize_(maxmemsize)
	,used_mem_(0)
{
	assert(maxmemsize);

	if (thread_safe)
		m_.reset(new std::mutex);

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

bool cache::remove(const item& v, uint64_t cas)
{
	if (m_) {
		std::unique_lock<std::mutex> lock(*m_);
		return do_remove(v, cas);
	}
	else {
		return do_remove(v, cas);
	}
}

bool cache::cas(item v, uint64_t cas)
{
	if (m_) {
		std::unique_lock<std::mutex> lock(*m_);
		return do_cas(std::move(v), cas);
	}
	else {
		return do_cas(std::move(v), cas);
	}
}

void cache::set(item v)
{
	if (m_) {
		std::unique_lock<std::mutex> lock(*m_);
		do_set(std::move(v));
	}
	else {
		do_set(std::move(v));
	}
}

std::shared_ptr<cache::item> cache::get(const key& k)
{
	if (m_) {
		std::unique_lock<std::mutex> lock(*m_);
		return do_get(k);
	}
	else {
		return do_get(k);
	}
}

bool cache::get_value(std::vector<unsigned char>& v, const key& k)
{
	if (m_) {
		std::unique_lock<std::mutex> lock(*m_);
		return do_get_value(v, k);
	}
	else {
		return do_get_value(v, k);
	}
}

bool cache::do_cas(item v, uint64_t cas)
{
	//handle cas
	std::shared_ptr<item> p = do_get(v.get_key());
	if (p && p->h_.request.cas != cas) {
		return false;
	}
	do_set(std::move(v));
	return true;
}

void cache::do_set(item v)
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
		std::shared_ptr<item> pi(new item(std::move(v)));
		h_.emplace(std::make_pair(k, pi));
	}
	catch (const std::exception&)
	{
		lru_.erase(lruit);
		throw;
	}
	used_mem_ += itemmem;
}

bool cache::do_get_value(std::vector<unsigned char>& v, const key& k)
{
	std::shared_ptr<item> p = do_get(k);
	if (!p)
		return false;
	unsigned int value_len = p->get_data_len() - p->h_.request.keylen;
	const unsigned char* pd = p->get_data()+p->h_.request.keylen;
	v.insert(v.end(), pd, pd + value_len);
	return true;
}

std::shared_ptr<cache::item> cache::do_get(const key& k)
{
	auto it = h_.find(k);
	if (it == h_.end())
		return std::shared_ptr<item>();

	assert(!lru_.empty());

	//refresh in the LRU list
	if (it->second->lru_ref_ != --lru_.end()) {
		lru_.splice(lru_.end(), lru_, it->second->lru_ref_);
	}
	
	return it->second;
}

bool cache::do_remove(const item& v, uint64_t cas)
{
	try {
		if (cas) {
			//handle cas
			std::shared_ptr<item> p = do_get(v.get_key());
			if (p && p->h_.request.cas != cas) {
				return false;
			}
		}
		delete_item(v.get_key());
	}
	catch (const std::exception&) {
	}
	return true;
}

void cache::delete_item(key k) //pass by value
{
	auto it = h_.find(k);
	if (it == h_.end()) {
		throw std::runtime_error("cache integrity error");
	}

	//remove from LRU
	lru_.erase(it->second->lru_ref_);

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

