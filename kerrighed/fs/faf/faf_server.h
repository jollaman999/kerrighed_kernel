/** Kerrighed FAF Server.
 *  @file faf_server.h
 *
 *  @author Renaud Lottiaux
 */

#ifndef __FAF_SERVER__
#define __FAF_SERVER__

#include <linux/socket.h>

/*--------------------------------------------------------------------------*
 *                                                                          *
 *                                  TYPES                                   *
 *                                                                          *
 *--------------------------------------------------------------------------*/

struct faf_rw_msg {
	unsigned int server_fd;
	size_t count;
	loff_t pos;
};

struct faf_rw_ret {
	ssize_t ret;
	loff_t pos;
};

struct faf_d_path_msg {
	unsigned int server_fd;
	int deleted;
	size_t count;
};

struct faf_notify_msg {
	unsigned int server_fd;
	unsigned long objid;
};

struct faf_stat_msg {
	unsigned int server_fd;
	long flags;
};

struct faf_statfs_msg {
	unsigned int server_fd;
};

struct faf_ctl_msg {
	unsigned int server_fd;
	unsigned int cmd;
	union {
		unsigned long arg;
		struct flock flock;
#if BITS_PER_LONG == 32
		struct flock64 flock64;
#endif
		struct f_owner_ex owner;
	};
};

struct faf_seek_msg {
	unsigned int server_fd;
	off_t offset;
	unsigned int origin;
};

struct faf_llseek_msg {
	unsigned int server_fd;
	unsigned long offset_high;
	unsigned long offset_low;
	unsigned int origin;
};

struct faf_bind_msg {
	unsigned int server_fd;
	int addrlen;
	struct sockaddr_storage sa;
};

struct faf_listen_msg {
	unsigned int server_fd;
	int sub_chan;
	int backlog;
};

struct faf_shutdown_msg {
	unsigned int server_fd;
	int how;
};

struct faf_setsockopt_msg {
	unsigned int server_fd;
	int level;
	int optname;
	char __user *optval;
	int optlen;
};

struct faf_getsockopt_msg {
	unsigned int server_fd;
	int level;
	int optname;
	char __user *optval;
	int __user *optlen;
};

struct faf_sendmsg_msg {
	unsigned int server_fd;
	unsigned int flags;
	size_t total_len;
};

struct faf_poll_wait_msg {
	unsigned int server_fd;
	unsigned long objid;
	int wait;
};

/*--------------------------------------------------------------------------*
 *                                                                          *
 *                              EXTERN FUNCTIONS                            *
 *                                                                          *
 *--------------------------------------------------------------------------*/

void faf_server_init (void);
void faf_server_finalize (void);

#endif // __FAF_SERVER__
