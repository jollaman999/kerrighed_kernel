#ifndef _ASM_X86_CURRENT_H
#define _ASM_X86_CURRENT_H

#include <linux/compiler.h>
#include <asm/percpu.h>

#ifndef __ASSEMBLY__
#ifdef CONFIG_KRG_EPM
#include <linux/spinlock_types.h>
#endif

struct task_struct;

DECLARE_PER_CPU(struct task_struct *, current_task);

static __always_inline struct task_struct *get_current(void)
{
	return percpu_read_stable(current_task);
}

#ifdef CONFIG_KRG_EPM
extern spinlock_t krg_current_write_lock;

#define krg_current (get_current()->effective_current)
#define current ({							\
	struct task_struct *__cur = get_current();			\
	__cur->effective_current ? __cur->effective_current : __cur;	\
})

#define krg_current_save(tmp) do {  \
		tmp = krg_current;  \
		spin_lock(&krg_current_write_lock);  \
		krg_current = NULL; \
		spin_unlock(&krg_current_write_lock);  \
	} while (0)
#define krg_current_restore(tmp) do { \
		spin_lock(&krg_current_write_lock);  \
		krg_current = tmp;    \
		spin_unlock(&krg_current_write_lock);  \
	} while (0)

#else /* !CONFIG_KRG_EPM */
#define current get_current()
#endif /* !CONFIG_KRG_EPM */

#endif /* __ASSEMBLY__ */

#endif /* _ASM_X86_CURRENT_H */
