#include <linux/cpumask.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/irqnr.h>
#include <asm/cputime.h>

#ifndef arch_irq_stat_cpu
#define arch_irq_stat_cpu(cpu) 0
#endif
#ifndef arch_irq_stat
#define arch_irq_stat() 0
#endif
#ifndef arch_idle_time
#define arch_idle_time(cpu) 0
#endif

static int show_stat(struct seq_file *p, void *v)
{
	int i, j;
	unsigned long jif;
	cputime64_t user, nice, system, idle, iowait, irq, softirq, steal;
	cputime64_t guest, guest_nice;
	u64 sum = 0;
	u64 sum_softirq = 0;
	unsigned int per_softirq_sums[NR_SOFTIRQS] = {0};
	struct timespec boottime;

	user = nice = system = idle = iowait =
		irq = softirq = steal = 0;
	guest = guest_nice = 0;
	getboottime(&boottime);
	jif = boottime.tv_sec;

	for_each_possible_cpu(i) {
		user += kstat_cpu(i).cpustat.user;
		nice += kstat_cpu(i).cpustat.nice;
		system += kstat_cpu(i).cpustat.system;
		idle += kstat_cpu(i).cpustat.idle + arch_idle_time(i);
		iowait += kstat_cpu(i).cpustat.iowait;
		irq += kstat_cpu(i).cpustat.irq;
		softirq += kstat_cpu(i).cpustat.softirq;
		steal += kstat_cpu(i).cpustat.steal;
		guest += kstat_cpu(i).cpustat.guest;
		guest_nice += kstat_cpu(i).cpustat.guest_nice;
		sum += kstat_cpu_irqs_sum(i);
		sum += arch_irq_stat_cpu(i);

		for (j = 0; j < NR_SOFTIRQS; j++) {
			unsigned int softirq_stat = kstat_softirqs_cpu(j, i);

			per_softirq_sums[j] += softirq_stat;
			sum_softirq += softirq_stat;
		}
	}
	sum += arch_irq_stat();

	seq_puts(p, "cpu ");
	seq_put_decimal_ull(p, ' ', cputime64_to_clock_t(user));
	seq_put_decimal_ull(p, ' ', cputime64_to_clock_t(nice));
	seq_put_decimal_ull(p, ' ', cputime64_to_clock_t(system));
	seq_put_decimal_ull(p, ' ', cputime64_to_clock_t(idle));
	seq_put_decimal_ull(p, ' ', cputime64_to_clock_t(iowait));
	seq_put_decimal_ull(p, ' ', cputime64_to_clock_t(irq));
	seq_put_decimal_ull(p, ' ', cputime64_to_clock_t(softirq));
	seq_put_decimal_ull(p, ' ', cputime64_to_clock_t(steal));
	seq_put_decimal_ull(p, ' ', cputime64_to_clock_t(guest));
	seq_put_decimal_ull(p, ' ', cputime64_to_clock_t(guest_nice));
	seq_putc(p, '\n');

	for_each_online_cpu(i) {
		/* Copy values here to work around gcc-2.95.3, gcc-2.96 */
		user = kstat_cpu(i).cpustat.user;
		nice = kstat_cpu(i).cpustat.nice;
		system = kstat_cpu(i).cpustat.system;
		idle = kstat_cpu(i).cpustat.idle + arch_idle_time(i);
		iowait = kstat_cpu(i).cpustat.iowait;
		irq = kstat_cpu(i).cpustat.irq;
		softirq = kstat_cpu(i).cpustat.softirq;
		steal = kstat_cpu(i).cpustat.steal;
		guest = kstat_cpu(i).cpustat.guest;
		guest_nice = kstat_cpu(i).cpustat.guest_nice;
		seq_printf(p, "cpu%d", i);
		seq_put_decimal_ull(p, ' ', cputime64_to_clock_t(user));
		seq_put_decimal_ull(p, ' ', cputime64_to_clock_t(nice));
		seq_put_decimal_ull(p, ' ', cputime64_to_clock_t(system));
		seq_put_decimal_ull(p, ' ', cputime64_to_clock_t(idle));
		seq_put_decimal_ull(p, ' ', cputime64_to_clock_t(iowait));
		seq_put_decimal_ull(p, ' ', cputime64_to_clock_t(irq));
		seq_put_decimal_ull(p, ' ', cputime64_to_clock_t(softirq));
		seq_put_decimal_ull(p, ' ', cputime64_to_clock_t(steal));
		seq_put_decimal_ull(p, ' ', cputime64_to_clock_t(guest));
		seq_put_decimal_ull(p, ' ', cputime64_to_clock_t(guest_nice));
		seq_putc(p, '\n');
	}
	seq_printf(p, "intr %llu", (unsigned long long)sum);

	/* sum again ? it could be updated? */
	for_each_irq_nr(j)
		seq_put_decimal_ull(p, ' ', kstat_irqs(j));

	seq_printf(p,
		"\nctxt %llu\n"
		"btime %lu\n"
		"processes %lu\n"
		"procs_running %lu\n"
		"procs_blocked %lu\n",
		nr_context_switches(),
		(unsigned long)jif,
		total_forks,
		nr_running(),
		nr_iowait());

	seq_printf(p, "softirq %llu", (unsigned long long)sum_softirq);

	for (i = 0; i < NR_SOFTIRQS; i++)
		seq_put_decimal_ull(p, ' ', per_softirq_sums[i]);
	seq_putc(p, '\n');

	return 0;
}

static int stat_open(struct inode *inode, struct file *file)
{
	unsigned size = 1024 + 128 * num_possible_cpus();
	char *buf;
	struct seq_file *m;
	int res;

	/* minimum size to display an interrupt count : 2 bytes */
	size += 2 * nr_irqs;

	/* don't ask for more than the kmalloc() max size */
	if (size > KMALLOC_MAX_SIZE)
		size = KMALLOC_MAX_SIZE;
	buf = kmalloc(size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	res = single_open(file, show_stat, NULL);
	if (!res) {
		m = file->private_data;
		m->buf = buf;
		m->size = ksize(buf);
	} else
		kfree(buf);
	return res;
}

static const struct file_operations proc_stat_operations = {
	.open		= stat_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init proc_stat_init(void)
{
	proc_create("stat", 0, NULL, &proc_stat_operations);
	return 0;
}
module_init(proc_stat_init);
