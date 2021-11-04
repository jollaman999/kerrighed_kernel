#ifndef __HCC_TYPES_INTERNAL__
#define __HCC_TYPES_INTERNAL__

#include <hcc/sys/types.h>

#ifdef __KERNEL__
#include <hcc/krgnodemask.h>
#endif

#define HCCFCT(p) if(p!=NULL) p

#if defined(CONFIG_KERRIGHED) || defined(CONFIG_HCCRPC)

typedef unsigned char hcc_session_t;
typedef int hcc_subsession_t;
typedef unsigned long unique_id_t;   /**< Unique id type */

#endif /* CONFIG_KERRIGHED */

#ifdef __KERNEL__

#ifdef CONFIG_HCC_STREAM
struct dstream_socket { // shared node-wide
	unique_id_t id_socket;
	unique_id_t id_container;
	struct dstream_interface_ctnr *interface_ctnr;
	struct stream_socket *krg_socket;
};
#endif

#endif /* __KERNEL__ */

#endif /* __HCC_TYPES_INTERNAL__ */
