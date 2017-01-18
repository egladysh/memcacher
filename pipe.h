#ifndef PIPE_WRAP_H
#define PIPE_WRAP_H

#include <unistd.h>
#include <fcntl.h>
#include <stdexcept>


namespace mc
{
	struct pipe
	{
	
		pipe()
		{
			if (::pipe(fd_) == -1) {
				throw std::runtime_error("unable create pipe");
			}
			set_fcntl(fd_[0], O_NONBLOCK);
		}

		~pipe()
		{
			::close(fd_[0]);
			::close(fd_[1]);
		}

		int read_end() const
		{
			return fd_[0];
		}

		int write_end() const
		{
			return fd_[1];
		}
	
	private:
		int fd_[0];

		void set_fcntl(int fd, int flags)
		{
			int flg = ::fcntl(fd, F_GETFL);
			::fcntl(fd, F_SETFL, flg | O_NONBLOCK);
		}

		pipe(const pipe&) = delete;
		pipe& operator=(const pipe&) = delete;
	};

}

#endif

