// thread safe FIFO queue
//
#ifndef SAFE_QUEUE_H
#define SAFE_QUEUE_H 

#include <assert.h>
#include <deque>
#include <condition_variable>
#include <mutex>

namespace mc //for memcache...
{

	//	to be used in producer/consumer kind of stuff
	
	template< typename T >
	struct safe_queue
	{
		typedef std::deque<T> queue;
		typedef T value_type;

		explicit safe_queue()
		{}

		//pass by value as it's intended for use with the move semantic
		void push(value_type v)
		{
			std::unique_lock<std::mutex> lock(m_);
			q_.push_back(std::move(v));
			if (q_.size() == 1)
				con_.notify_one();
		}

		value_type wait_next()
		{
			std::unique_lock<std::mutex> lock(m_);
			while (true) {
				if (q_.size()) {
					return next();
				}
				con_.wait(lock);
			}
			throw 1; //to make compiler happy
		}

		bool is_empty() 
		{
			std::unique_lock<std::mutex> lock(m_);
			return q_.empty();
		}

	private:
		std::mutex m_;
		std::condition_variable con_;
		queue q_;

		value_type next() 
		{
			assert(q_.size());
			value_type v(std::move(q_.front()));
			q_.pop_front();
			return v;
		}
	};

}

#endif
