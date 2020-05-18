#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/cpumask.h>

#include <linux/fs.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/sched/stat.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/irqnr.h>
#include <linux/sched/cputime.h>
#include <linux/tick.h>
#include <linux/delay.h>  // msleep


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Thiago F. Pappacena");
MODULE_DESCRIPTION("Module to automatically enable/disable cores of the system depending on the load.");
MODULE_VERSION("0.1.0");

static u64 get_idle_time(struct kernel_cpustat *kcs, int cpu) {
	u64 idle, idle_usecs = -1ULL;

	if (cpu_online(cpu))
		idle_usecs = get_cpu_idle_time_us(cpu, NULL);

	if (idle_usecs == -1ULL)
		/* !NO_HZ or cpu offline so we can rely on cpustat.idle */
		idle = kcs->cpustat[CPUTIME_IDLE];
	else
		idle = idle_usecs * NSEC_PER_USEC;

	return idle;
}

static u64 get_iowait_time(struct kernel_cpustat *kcs, int cpu) {
	u64 iowait, iowait_usecs = -1ULL;

	if (cpu_online(cpu))
		iowait_usecs = get_cpu_iowait_time_us(cpu, NULL);

	if (iowait_usecs == -1ULL)
		/* !NO_HZ or cpu offline so we can rely on cpustat.iowait */
		iowait = kcs->cpustat[CPUTIME_IOWAIT];
	else
		iowait = iowait_usecs * NSEC_PER_USEC;

	return iowait;
}


int inline get_cpu_usage(void) {
    // See fs/proc/stat.c for more details.
    int i, k, total_cores, idle_percent;
    u64 total = 0, user = 0, nice = 0, system = 0, 
        idle = 0, iowait = 0, irq = 0, softirq = 0;
    u64 last_user = 0, last_nice = 0, last_system = 0, 
        last_idle = 0, last_iowait = 0, last_irq = 0, last_softirq = 0; 

    total_cores = 0;
    for(k = 0; k < 2 ; k++) {
        for_each_online_cpu(i) {
            if (!cpu_active(i)) 
                continue;
            struct kernel_cpustat kcpustat;
            u64 *cpustat = kcpustat.cpustat;

            kcpustat = kcpustat_cpu(i);

            total_cores++;
            user += cpustat[CPUTIME_USER];
            nice += cpustat[CPUTIME_NICE];
            system += cpustat[CPUTIME_SYSTEM];
            idle += get_idle_time(&kcpustat, i);
            iowait += get_iowait_time(&kcpustat, i);
            irq	+= cpustat[CPUTIME_IRQ];
            softirq	+= cpustat[CPUTIME_SOFTIRQ];
        }

        if (k == 0) {
            last_user = user;
            last_nice = nice;
            last_system = system;
            last_idle = idle;
            last_iowait = iowait;
            last_irq = irq;
            last_softirq = softirq;

            user = 0;
            nice = 0;
            system = 0;
            idle = 0;
            iowait = 0;
            irq = 0;
            softirq = 0;

            msleep(100);
        }
        else {
            user = user - last_user;
            nice = nice - last_nice;
            system = system - last_system;
            idle = idle - last_idle;
            iowait = iowait - last_iowait;
            irq = irq - last_irq;
            softirq = softirq - last_softirq;
        }
    }

    total = user + nice + system + idle + iowait + irq + softirq;
    idle_percent = 100 * idle / total;
    printk(KERN_INFO "idle = %llu / total = %llu", idle, total);
    printk(KERN_INFO "Processors idle percent average = %d", idle_percent);

    return (100 - idle_percent);
}

static inline int get_cores_desired_delta(void) {
    int high_usage = 70, low_usage = 40, usage;
    usage = get_cpu_usage();
    if(usage >= high_usage)
        return +1;
    if (usage <= low_usage)
        return -1;
    return 0;
}

static inline void set_enabled_cores(int n) {
    int i, enabled, min_cpus = NR_CPUS / 2;
    if(n < min_cpus) {
        printk(KERN_INFO "Avoiding to set enabled cores to less then %d.", min_cpus);
    }

    for_each_possible_cpu(i) {
        if(i == 0)
            continue;
        enabled = i <= (n - 1);
        printk(KERN_INFO "~> %d = %d", i, enabled);
        set_cpu_active(i, enabled);
        set_cpu_present(i, enabled);
    }
}

static int __init cpuautoscaling_init(void) {
    if(!CONFIG_HOTPLUG_CPU) {
        printk(KERN_INFO "Hotplug should be enabled.");
        return 0;
    }

    set_enabled_cores(num_active_cpus() + get_cores_desired_delta());

    // kernel_cpu_stat *kcs;
    printk(KERN_INFO "%d/%d (%d%% usage ~> Delta %d) :)\n", 
        num_online_cpus(), num_possible_cpus(), get_cpu_usage(), get_cores_desired_delta());
    return 0;
}

static void __exit cpuautoscaling_exit(void) {
    printk(KERN_INFO "Goodbye, World!\n");
}

module_init(cpuautoscaling_init);
module_exit(cpuautoscaling_exit);
