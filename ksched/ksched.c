/*
 * ksched.c - an accelerated scheduler interface for the IOKernel
 */

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/local.h>

#include <asm/set_memory.h>

#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/tlbflush.h>
#include <linux/capability.h>
#include <linux/cdev.h>

#include <linux/cpuidle.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/sort.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/sched/task.h>
#include <linux/smp.h>
#include <linux/uaccess.h>
#include <linux/signal.h>
#include <linux/version.h>

#include "ksched.h"
#include "../iokernel/pmc.h"


#define CORE_PERF_GLOBAL_CTRL_ENABLE_PMC_0 (0x1)
#define CORE_PERF_GLOBAL_CTRL_ENABLE_PMC_1 (0x2)

/* the character device that provides the ksched IOCTL interface */
static struct cdev ksched_cdev;

/* shared memory between the IOKernel and the Linux Kernel */
static __read_mostly struct ksched_shm_cpu *shm;
#define SHM_SIZE (NR_CPUS * sizeof(struct ksched_shm_cpu))

struct ksched_percpu {
	unsigned int		last_gen;
	local_t			busy;
        u64			last_sel;
	struct task_struct	*running_task;
};

/* per-cpu data to coordinate context switching and signal delivery */
static DEFINE_PER_CPU(struct ksched_percpu, kp);

enum {
	PARKED = 0,
	UNPARKED
};

void mark_task_parked(struct task_struct *tsk)
{
	/* borrow the trace field here which is origally used by Ftrace */
	tsk->trace = PARKED;
}

bool try_mark_task_unparked(struct task_struct *tsk) {
	bool success = false;

	if (tsk->trace == PARKED) {
		success = true;
		tsk->trace = UNPARKED;
	}

	return success;
}


/**
 * ksched_lookup_task - retreives a task from a pid number
 * @nr: the pid number
 *
 * Returns a task pointer or NULL if none was found.
 */
static struct task_struct *ksched_lookup_task(pid_t nr)
{
	struct pid *pid;

	pid = find_vpid(nr);
	if (unlikely(!pid))
		return NULL;
	return pid_task(pid, PIDTYPE_PID);
}

static void ksched_next_tid(struct ksched_percpu *kp, int cpu, pid_t tid)
{
	struct task_struct *p;
	int ret;
	unsigned long flags;
	bool already_running;

	/* release previous task */
	if (kp->running_task) {
		put_task_struct(kp->running_task);
		kp->running_task = NULL;
	}

	if (unlikely(tid == 0))
		return;

	rcu_read_lock();
	p = ksched_lookup_task(tid);
	if (unlikely(!p)) {
		rcu_read_unlock();
		return;
	}

	raw_spin_lock_irqsave(&p->pi_lock, flags);
#if LINUX_VERSION_CODE < KERNEL_VERSION(5,14,0)
	already_running = p->on_cpu || p->state == TASK_WAKING ||
			  p->state == TASK_RUNNING || !try_mark_task_unparked(p);
#else
	already_running = p->on_cpu || p->__state == TASK_WAKING ||
			  task_is_running(p) || !try_mark_task_unparked(p);
#endif
	raw_spin_unlock_irqrestore(&p->pi_lock, flags);
	if (unlikely(already_running)) {
		rcu_read_unlock();
		return;
	}

	ret = set_cpus_allowed_ptr(p, cpumask_of(cpu));
	if (unlikely(ret)) {
		mark_task_parked(p);
		rcu_read_unlock();
		return;
	}

	get_task_struct(p);
	kp->running_task = p;

	wake_up_process(p);
	rcu_read_unlock();

	return;
}


//is a kernel function that waits for a specific memory address to be modified by another CPU or device
static int ksched_mwait_on_addr(const unsigned int *addr, unsigned int hint,
				unsigned int val)
{
	unsigned int cur;

	lockdep_assert_irqs_disabled();

	/* first see if the condition is met without waiting */
	cur = smp_load_acquire(addr);
	while (cur == val)
	{
		cur = smp_load_acquire(addr);
	}
	
	return cur;
}


//__cpuidle used to define ksched_idle as an idele state handler
static int __attribute__((section(".cpuidle"))) ksched_idle(struct cpuidle_device *dev,
				 struct cpuidle_driver *drv, int index)
{
	struct ksched_percpu *p;
	struct ksched_shm_cpu *s;
	unsigned long gen;
	unsigned int hint;
	pid_t tid;
	int cpu;

	lockdep_assert_irqs_disabled();

	cpu = get_cpu();
	p = this_cpu_ptr(&kp);
	s = &shm[cpu];

	/* check if we entered the idle loop with a process still active */
	if (unlikely(p->running_task)) {
		if (p->running_task->flags & PF_EXITING) {
			put_task_struct(p->running_task);
			p->running_task = NULL;
		} else {
			ksched_mwait_on_addr(&s->gen, 0, s->gen);
			put_cpu();
			return index;
		}
	}

	/* mark the core as idle if a new request isn't waiting */
	local_set(&p->busy, false);
	if (s->busy && smp_load_acquire(&s->gen) == p->last_gen)
		WRITE_ONCE(s->busy, false);

	/* use the mwait instruction to efficiently wait for the next request */
	hint = READ_ONCE(s->mwait_hint);
	gen = ksched_mwait_on_addr(&s->gen, hint, p->last_gen);
	if (gen != p->last_gen) {
		tid = READ_ONCE(s->tid);
		p->last_gen = gen;
		ksched_next_tid(p, cpu, tid);
		WRITE_ONCE(s->busy, p->running_task != NULL);
		local_set(&p->busy, true);
		smp_store_release(&s->last_gen, gen);
	}

	put_cpu();

	return index;
}

static inline long get_granted_core_id(void)
{
	struct ksched_percpu *p = this_cpu_ptr(&kp);

	if (unlikely(p->running_task == NULL ||
		     p->running_task->pid != task_pid_vnr(current))) {
		/* The thread is waken up by a user-sent signal instead of
		 * the iokernel. In this case no core was granted. We should
		 * put the thread back into sleep immediately after handling
		 * the signal. */
	  printk(KERN_INFO "p == NULL, %d", p->running_task == NULL);
	  printk(KERN_INFO "error happened in get_granted_core_id");
		return -ERESTARTSYS;
	}

	return smp_processor_id();
}

static long ksched_park(void)
{
	struct ksched_percpu *p;
	struct ksched_shm_cpu *s;
	unsigned long gen;
	pid_t tid;
	int cpu;

	cpu = get_cpu();
	p = this_cpu_ptr(&kp);
	s = &shm[cpu];

	local_set(&p->busy, false);

	if (unlikely(signal_pending(current))) {
		local_set(&p->busy, true);
		put_cpu();
		return -ERESTARTSYS;
	}

	/* check if a new request is available yet */
	gen = smp_load_acquire(&s->gen);
	if (gen == p->last_gen) {
		WRITE_ONCE(s->busy, false);
		ksched_next_tid(p, cpu, 0);
		put_cpu();
		goto park;
	}

	/* determine the next task to run */
	tid = READ_ONCE(s->tid);
	p->last_gen = gen;

	/* are we waking the current pid? */
	if (tid == task_pid_vnr(current)) {
		WRITE_ONCE(s->busy, true);
		local_set(&p->busy, true);
		smp_store_release(&s->last_gen, gen);
		put_cpu();
		return smp_processor_id();
	}

	ksched_next_tid(p, cpu, tid);
	WRITE_ONCE(s->busy, p->running_task != NULL);
	local_set(&p->busy, p->running_task != NULL);
	smp_store_release(&s->last_gen, gen);
	put_cpu();

park:
	/* put this task to sleep and reschedule so the next task can run */
	__set_current_state(TASK_INTERRUPTIBLE);
	mark_task_parked(current);
	schedule();
	__set_current_state(TASK_RUNNING);
	return get_granted_core_id();
}

static long ksched_start(void)
{
	
	printk(KERN_INFO "ksched_start was called");
	/* put this task to sleep and reschedule so the next task can run */
	__set_current_state(TASK_INTERRUPTIBLE);
	mark_task_parked(current);
	printk(KERN_INFO "calling schedule");
	printk(KERN_INFO "calling schedule");
	schedule();
	printk(KERN_INFO "returned from schedule");
	printk(KERN_INFO "returned from schedule");
	__set_current_state(TASK_RUNNING);
	return get_granted_core_id();
}

static void ksched_deliver_signal(struct ksched_percpu *p, unsigned int signum)
{
	/* if core is already idle, don't bother delivering signals */
	if (!local_read(&p->busy))
		return;

	if (p->running_task)
		send_sig(signum, p->running_task, 0);
}


//not sure if this is correct
unsigned long long rdtsc_tmp(void)
{
    unsigned long long val;
    asm volatile("mrs %0, CNTVCT_EL0" : "=r" (val));
    return val;
}

static void ksched_ipi(void *unused)
{
	struct ksched_percpu *p;
	struct ksched_shm_cpu *s;
	int cpu, tmp;

	cpu = get_cpu();
	p = this_cpu_ptr(&kp);
	s = &shm[cpu];

	/* check if a signal has been requested */
	tmp = smp_load_acquire(&s->sig);
	if (tmp == p->last_gen) {
		ksched_deliver_signal(p, READ_ONCE(s->signum));
		smp_store_release(&s->sig, 0);
	}

	put_cpu();
}

static int get_user_cpu_mask(const unsigned long __user *user_mask_ptr,
			     unsigned len, struct cpumask *new_mask)
{
	if (len < cpumask_size())
		cpumask_clear(new_mask);
	else if (len > cpumask_size())
		len = cpumask_size();

	return copy_from_user(new_mask, user_mask_ptr, len) ? -EFAULT : 0;
}

static long ksched_intr(struct ksched_intr_req __user *ureq)
{
	cpumask_var_t mask;
	struct ksched_intr_req req;

	/* only the IOKernel can send interrupts (privileged) */
	if (unlikely(!capable(CAP_SYS_ADMIN)))
		return -EACCES;

	/* validate inputs */
	if (unlikely(copy_from_user(&req, ureq, sizeof(req))))
		return -EFAULT;
	if (unlikely(!alloc_cpumask_var(&mask, GFP_KERNEL)))
		return -ENOMEM;
	if (unlikely(get_user_cpu_mask((const unsigned long __user *)req.mask,
				       req.len, mask))) {
		free_cpumask_var(mask);
		return -EFAULT;
	}

	smp_call_function_many(mask, ksched_ipi, NULL, false);
	free_cpumask_var(mask);
	return 0;
}

static long
ksched_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	
	/* validate input */
	if (unlikely(_IOC_TYPE(cmd) != KSCHED_MAGIC))
		return -ENOTTY;
	if (unlikely(_IOC_NR(cmd) > KSCHED_IOC_MAXNR))
		return -ENOTTY;
	
	int res;
	long res1;
	switch (cmd) {
	case KSCHED_IOC_START:
		res = ksched_start();
		if(copy_to_user((int*)arg, &res, sizeof(res))){
			printk("ksched: failed copying data to user!\n");
		}
		return 0;
	case KSCHED_IOC_PARK:
		res1 = ksched_park();
		if(copy_to_user((int*)arg, &res1, sizeof(res1))){
			printk("ksched: failed copying data to user!\n");
		}
		return 0;
	case KSCHED_IOC_INTR:
		return ksched_intr((void __user *)arg);
	default:
		break;
	}

	return -ENOTTY;
}

static int ksched_mmap(struct file *file, struct vm_area_struct *vma)
{
	/* only the IOKernel can access the shared region (privileged) */
	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;
	return remap_vmalloc_range(vma, (void *)shm, vma->vm_pgoff);
}

static int ksched_open(struct inode *inode, struct file *filp)
{
	return 0;
}

static int ksched_release(struct inode *inode, struct file *filp)
{
	return 0;
}

static struct file_operations ksched_ops = {
	.owner		= THIS_MODULE,
	.mmap		= ksched_mmap,
	.unlocked_ioctl	= ksched_ioctl,
	.open		= ksched_open,
	.release	= ksched_release,
};

static struct cpuidle_driver ksched_driver = {
    .name = "ksched_driver",
    .owner = THIS_MODULE,
    .states[0] = {
        .enter = ksched_idle,
        .exit_latency = 1,
        .target_residency = 1,
        .name = "ksched_idle",
        .desc = "aarch64 ksched",
    },
    .state_count = 1,
};


/*************************************************************************************************
THIS MODULE CANNOT BE USED! It was left in caladan anyway for future work with the ported version.
**************************************************************************************************/

static int __init ksched_init(void)
{
	dev_t devno_ksched = MKDEV(KSCHED_MAJOR, KSCHED_MINOR);
	int ret;
	
	ret = register_chrdev_region(devno_ksched, 1, "ksched");
	if (ret)
		return ret;

	cdev_init(&ksched_cdev, &ksched_ops);
	ret = cdev_add(&ksched_cdev, devno_ksched, 1);
	if (ret)
		goto fail_ksched_cdev_add;

	shm = vmalloc_user(SHM_SIZE);
	if (!shm) {
		ret = -ENOMEM;
		goto fail_shm;
	}
	memset(shm, 0, SHM_SIZE);

	ret = cpuidle_register_driver(&ksched_driver);
	if (ret)
		goto reg_driver;

	printk(KERN_INFO "ksched: API ported V2 enabled");
	return 0;

reg_driver:
	vfree(shm);
fail_shm:
	cdev_del(&ksched_cdev);
fail_ksched_cdev_add:
	unregister_chrdev_region(devno_ksched, 1);
	return ret;
}

static void __exit ksched_exit(void)
{
	int cpu;
	struct ksched_percpu *p;

	dev_t devno_ksched = MKDEV(KSCHED_MAJOR, KSCHED_MINOR);

	cpuidle_unregister_driver(&ksched_driver);
     
	vfree(shm);
	cdev_del(&ksched_cdev);
	unregister_chrdev_region(devno_ksched, 1);

	for_each_online_cpu(cpu) {
		p = per_cpu_ptr(&kp, cpu);
		if (p->running_task)
			put_task_struct(p->running_task);
	}
}

module_init(ksched_init);
module_exit(ksched_exit);

MODULE_LICENSE("GPL");
