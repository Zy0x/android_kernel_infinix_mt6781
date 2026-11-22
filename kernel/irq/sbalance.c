// SPDX-License-Identifier: GPL-2.0
/*
 * Simple Kernel IRQ Balancer for Heterogeneous ARM64 SoCs (sbalance)
 *
 * Samples per-CPU IRQ activity on system_power_efficient_wq and offloads
 * high-rate peripheral interrupts (GPU, UFS/eMMC, WiFi, audio, IPC/MBOX,
 * CMDQ) off big performance cores onto the least-loaded online little core
 * while strictly protecting touch, display, timer, and TEE interrupts.
 */
#define pr_fmt(fmt) "sbalance: " fmt

#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/ctype.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdesc.h>
#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/workqueue.h>

#define SBAL_MAX_CPUS		32
#define SBAL_NAME_LEN		48
#define SBAL_MIN_INTERVAL_MS	500U
#define SBAL_MAX_INTERVAL_MS	30000U

static bool enabled = true;
module_param(enabled, bool, 0644);
MODULE_PARM_DESC(enabled, "Enable or pause runtime IRQ balancing");

static unsigned int interval_ms = 2000;
module_param(interval_ms, uint, 0644);
MODULE_PARM_DESC(interval_ms, "Sampling interval in milliseconds (500-30000)");

static unsigned int irq_threshold = 30;
module_param(irq_threshold, uint, 0644);
MODULE_PARM_DESC(irq_threshold, "Minimum per-interval IRQ count before moving");

static bool offload_big_cores = true;
module_param(offload_big_cores, bool, 0644);
MODULE_PARM_DESC(offload_big_cores, "Prefer migrating heavy IRQs off big CPUs");

static unsigned int big_cpu_start = 6;
module_param(big_cpu_start, uint, 0644);
MODULE_PARM_DESC(big_cpu_start, "First CPU index considered a big core (0-N)");

static unsigned int rebalance_diff = 100;
module_param(rebalance_diff, uint, 0644);
MODULE_PARM_DESC(rebalance_diff, "Min load delta before rebalancing little CPUs");

static unsigned long moves_count;
module_param(moves_count, ulong, 0444);
MODULE_PARM_DESC(moves_count, "Total IRQ affinity migrations performed");

static unsigned long scans_count;
module_param(scans_count, ulong, 0444);
MODULE_PARM_DESC(scans_count, "Total balancing scan cycles completed");

struct sbal_cand {
	unsigned int irq;
	unsigned int total_delta;
	unsigned int big_delta;
	int cur_single_cpu;
	bool on_big;
	struct cpumask curr_mask;
};

static struct delayed_work sbal_work;
static unsigned int *prev_counts;
static struct sbal_cand *cands;
static unsigned int tracked_irqs;
static unsigned int tracked_cpus;
static bool counts_initialized;

static const char * const protected_irq_names[] = {
	"arch_timer",
	"mtk-clkevt",
	"arm-pmu",
	"trustonic",
	"novatek",
	"nvt-ts",
	"11013000.spi3",
	"goodix",
	"focaltech",
	"silfp",
	"esd_te",
	"disp_",
	".dsi",
	"mtk-kpd",
	"mt6358",
	NULL,
};

static bool sbal_strcasestr(const char *haystack, const char *needle)
{
	size_t hlen, nlen, i, j;

	if (!haystack || !needle)
		return false;

	hlen = strlen(haystack);
	nlen = strlen(needle);
	if (nlen == 0)
		return true;
	if (hlen < nlen)
		return false;

	for (i = 0; i <= hlen - nlen; i++) {
		for (j = 0; j < nlen; j++) {
			if (tolower((unsigned char)haystack[i + j]) !=
			    tolower((unsigned char)needle[j]))
				break;
		}
		if (j == nlen)
			return true;
	}
	return false;
}

static bool is_protected_irq_name(const char *name)
{
	int i;

	if (!name || !name[0])
		return true;

	for (i = 0; protected_irq_names[i]; i++) {
		if (sbal_strcasestr(name, protected_irq_names[i]))
			return true;
	}
	return false;
}

static unsigned int clamp_interval_ms(void)
{
	unsigned int ms = READ_ONCE(interval_ms);

	if (ms < SBAL_MIN_INTERVAL_MS)
		ms = SBAL_MIN_INTERVAL_MS;
	else if (ms > SBAL_MAX_INTERVAL_MS)
		ms = SBAL_MAX_INTERVAL_MS;
	return ms;
}

static int find_least_loaded_cpu(const u64 *cpu_load, bool little_only)
{
	int cpu;
	int best_cpu = -1;
	u64 best_load = U64_MAX;
	unsigned int limit = READ_ONCE(big_cpu_start);

	for_each_online_cpu(cpu) {
		if (cpu < 0 || (unsigned int)cpu >= tracked_cpus)
			continue;
		if (little_only && (unsigned int)cpu >= limit)
			continue;
		if (cpu_load[cpu] < best_load) {
			best_load = cpu_load[cpu];
			best_cpu = cpu;
		}
	}

	if (best_cpu < 0 && little_only)
		return find_least_loaded_cpu(cpu_load, false);

	return best_cpu;
}

static void sbal_worker_fn(struct work_struct *work)
{
	u64 cpu_load[SBAL_MAX_CPUS];
	unsigned int cand_count = 0;
	unsigned int thresh = READ_ONCE(irq_threshold);
	unsigned int big_start = READ_ONCE(big_cpu_start);
	unsigned int rdiff = READ_ONCE(rebalance_diff);
	bool do_offload = READ_ONCE(offload_big_cores);
	unsigned int irq, i;
	int cpu;

	if (!READ_ONCE(enabled))
		goto reschedule;

	memset(cpu_load, 0, sizeof(cpu_load));

	for (irq = 0; irq < tracked_irqs; irq++) {
		struct irq_desc *desc;
		unsigned int total_delta = 0;
		unsigned int big_delta = 0;
		unsigned long flags;
		char irq_name[SBAL_NAME_LEN];
		struct cpumask curr_mask;
		bool movable = false;
		bool on_big = false;
		int single_cpu = -1;

		desc = irq_to_desc(irq);
		if (!desc || !desc->kstat_irqs)
			continue;

		for_each_possible_cpu(cpu) {
			unsigned int idx;
			unsigned int cur;
			unsigned int prev;
			unsigned int delta;

			if ((unsigned int)cpu >= tracked_cpus)
				continue;

			idx = irq * tracked_cpus + (unsigned int)cpu;
			cur = kstat_irqs_cpu(irq, cpu);
			prev = prev_counts[idx];
			prev_counts[idx] = cur;

			if (!counts_initialized)
				continue;

			delta = cur - prev;
			total_delta += delta;
			if ((unsigned int)cpu >= big_start)
				big_delta += delta;
			cpu_load[cpu] += delta;
		}

		if (!counts_initialized || total_delta < thresh)
			continue;

		irq_name[0] = '\0';
		cpumask_clear(&curr_mask);

		raw_spin_lock_irqsave(&desc->lock, flags);
		if (desc->action && desc->action->name &&
		    desc->irq_data.chip && desc->irq_data.chip->irq_set_affinity &&
		    irqd_can_balance(&desc->irq_data) &&
		    irqd_is_activated(&desc->irq_data) &&
		    !irqd_is_per_cpu(&desc->irq_data) &&
		    !irqd_affinity_is_managed(&desc->irq_data)) {
			strlcpy(irq_name, desc->action->name, sizeof(irq_name));
			cpumask_copy(&curr_mask,
				     irq_data_get_affinity_mask(&desc->irq_data));
			movable = true;
		}
		raw_spin_unlock_irqrestore(&desc->lock, flags);

		if (!movable || !irq_can_set_affinity_usr(irq) ||
		    irq_balancing_disabled(irq) ||
		    is_protected_irq_name(irq_name))
			continue;

		for_each_cpu(cpu, &curr_mask) {
			if ((unsigned int)cpu >= big_start && cpu_online(cpu)) {
				on_big = true;
				break;
			}
		}

		if (cpumask_weight(&curr_mask) == 1)
			single_cpu = cpumask_first(&curr_mask);

		if (!on_big && single_cpu >= 0 && big_delta > 0)
			on_big = ((unsigned int)single_cpu >= big_start);

		if (do_offload && !on_big && single_cpu < 0)
			on_big = (big_delta > 0);

		cands[cand_count].irq = irq;
		cands[cand_count].total_delta = total_delta;
		cands[cand_count].big_delta = big_delta;
		cands[cand_count].cur_single_cpu = single_cpu;
		cands[cand_count].on_big = on_big;
		cpumask_copy(&cands[cand_count].curr_mask, &curr_mask);
		cand_count++;
	}

	if (!counts_initialized) {
		counts_initialized = true;
		goto reschedule;
	}

	scans_count++;

	cpus_read_lock();
	for (i = 0; i < cand_count; i++) {
		struct sbal_cand *c = &cands[i];
		int best_cpu;
		int ret;

		best_cpu = find_least_loaded_cpu(cpu_load, do_offload);
		if (best_cpu < 0)
			continue;

		if (c->cur_single_cpu == best_cpu &&
		    cpumask_weight(&c->curr_mask) == 1)
			continue;

		if (!c->on_big) {
			if (c->cur_single_cpu < 0 ||
			    (unsigned int)c->cur_single_cpu >= tracked_cpus)
				continue;
			if (cpu_load[c->cur_single_cpu] <=
			    cpu_load[best_cpu] + rdiff)
				continue;
		}

		ret = irq_set_affinity(c->irq, cpumask_of(best_cpu));
		if (ret == 0) {
			moves_count++;
			if (c->cur_single_cpu >= 0 &&
			    (unsigned int)c->cur_single_cpu < tracked_cpus &&
			    cpu_load[c->cur_single_cpu] >= c->total_delta)
				cpu_load[c->cur_single_cpu] -= c->total_delta;
			cpu_load[best_cpu] += c->total_delta;
		}
	}
	cpus_read_unlock();

reschedule:
	queue_delayed_work(system_power_efficient_wq, &sbal_work,
			   msecs_to_jiffies(clamp_interval_ms()));
}

static int __init sbalance_init(void)
{
	if (nr_irqs <= 0 || nr_cpu_ids == 0 || nr_cpu_ids > SBAL_MAX_CPUS) {
		pr_err("unsupported IRQ/CPU geometry (nr_irqs=%d, nr_cpu_ids=%u)\n",
		       nr_irqs, nr_cpu_ids);
		return -EINVAL;
	}

	tracked_irqs = (unsigned int)nr_irqs;
	tracked_cpus = nr_cpu_ids;

	prev_counts = kvzalloc((size_t)tracked_irqs * tracked_cpus *
			       sizeof(*prev_counts), GFP_KERNEL);
	if (!prev_counts)
		return -ENOMEM;

	cands = kvzalloc((size_t)tracked_irqs * sizeof(*cands), GFP_KERNEL);
	if (!cands) {
		kvfree(prev_counts);
		prev_counts = NULL;
		return -ENOMEM;
	}

	INIT_DELAYED_WORK(&sbal_work, sbal_worker_fn);
	queue_delayed_work(system_power_efficient_wq, &sbal_work,
			   msecs_to_jiffies(clamp_interval_ms()));

	pr_info("initialized (irqs=%u, cpus=%u, interval=%ums, threshold=%u, big_start=%u)\n",
		tracked_irqs, tracked_cpus, interval_ms, irq_threshold,
		big_cpu_start);
	return 0;
}

static void __exit sbalance_exit(void)
{
	cancel_delayed_work_sync(&sbal_work);
	kvfree(cands);
	kvfree(prev_counts);
	cands = NULL;
	prev_counts = NULL;
	pr_info("unloaded (moves=%lu, scans=%lu)\n", moves_count, scans_count);
}

module_init(sbalance_init);
module_exit(sbalance_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Sol Kernel <ariseforge@local>");
MODULE_DESCRIPTION("Simple IRQ balancer for heterogeneous ARM64 SoCs");
