#ifndef ROUND_ROBIN_H
#define ROUND_ROBIN_H

#include <utility>

namespace mc
{
	template< typename Pool >
	struct round_robin
	{
		typedef Pool pool;

		explicit round_robin(pool p)
			:p_(std::move(p))
			,pos_(0)
		{ 
			assert(!p_.empty());
		}

		typename pool::value_type pick()
		{
			return p_[(pos_++) % p_.size()];
		}

	private:
		pool p_;
		size_t pos_;

		round_robin(const round_robin&) = delete;
		round_robin& operator=(const round_robin&) = delete;
	};
}

#endif
