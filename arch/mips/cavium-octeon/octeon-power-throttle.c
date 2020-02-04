/*
 * octeon-power-throttle.c - interface for controlling power
 * throttling on OCTEON II based platforms (6xxx and above).  OCTEON II
 * supports dynamic power control which aids to cut down power
 * consumption. The code exposes a "percentage" power throttling
 * limiter by means of /sys interface for each available cpu. Setting
 * this value to 0 will set power consumption to a minimum as it will
 * only execute a couple instructions every PERIOD as set in the
 * PowThrottle register.  If set to 100% for that particular cpu, it
 * may consume maximum power.
 *
 * Copyright (C) 2012 Cavium, Inc.
 *
 * Copyright (C) 2012 MontaVista LLC.
 * Author: Philby John <pjohn@mvista.com>
 * Credits: This driver is derived from Dmitriy Zavin's (dmitriyz@google.com)
 * thermal throttle event support code.
 */
#include <linux/kernel.h>
#include <linux/stat.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/cpu.h>

#include <asm/octeon/octeon.h>

union octeon_power_throttle_bits {
	u64 raw;
	struct {
#ifdef __BIG_ENDIAN_BITFIELD
		u64 maxpow:8;		/* 63:56 */
		u64 powe:8;		/* 55:48 */
		u64 thrott:8;		/* 47:40 */
		u64 hrmpowadj:8;	/* 39:32 reserved in cn63XX */
		u64 reserved:3;		/* 31:29 */
		u64 ovrrd:1;		/* 28  reserved in cn63XX */
		u64 distag:1;		/* 27 */
		u64 period:3;		/* 26:24 */
		u64 powlim:8;		/* 23:16 */
		u64 maxthr:8;		/* 15:8 */
		u64 minthr:8;		/* 7:0 */
#else
		u64 minthr:8;
		u64 maxthr:8;
		u64 powlim:8;
		u64 period:3;
		u64 distag:1;
		u64 ovrrd:1;
		u64 reserved:3;
		u64 hrmpowadj:8;
		u64 thrott:8;
		u64 powe:8;
		u64 maxpow:8;
#endif
	} s;
};

/*
 * Given a CPU, calculate its PowThrottle register's L2C_COP0_MAP CSR
 * address.
 */
static u64 octeon_power_throttle_csr_addr(int cpu)
{
	u64 csr_addr, reg_num, reg_reg, reg_sel;
	int ppid = octeon_coreid_for_cpu(cpu);

	/* register 11 select 6 */
	reg_reg = 11;
	reg_sel = 6;
	reg_num = (ppid << 8) + (reg_reg << 3) + reg_sel;
	csr_addr = CVMX_L2C_COP0_MAPX(0) + ((reg_num) << 3);
	return csr_addr;
}

static void octeon_power_throttle_init_cpu(unsigned int cpu)
{
	u64 csr_addr;
	union octeon_power_throttle_bits r;

	csr_addr = octeon_power_throttle_csr_addr(cpu);
	r.raw = cvmx_read_csr(csr_addr);
	r.s.minthr = 0;
	r.s.maxthr = 0xff;
	r.s.period = 2;		/* 256 cycles */
	cvmx_write_csr(csr_addr, r.raw);
}

/*
 * Get the POWLIM field as percentage% of the MAXPOW field in r.
 */
static int octeon_power_throttle_get_powlim(unsigned int cpu)
{
	u64 t, rv, csr_addr;
	union octeon_power_throttle_bits r;

	csr_addr = octeon_power_throttle_csr_addr(cpu);
	r.raw = cvmx_read_csr(csr_addr);
	t = r.s.maxpow;
	if (!OCTEON_IS_MODEL(OCTEON_CN63XX)) {
		if (t < r.s.hrmpowadj)
			return -EINVAL;
		t -= r.s.hrmpowadj;
	}
	rv = (r.s.powlim * 100) / t;
	return rv > 100 ? 100 : rv;
}

/*
 * Set the POWLIM field as percentage% of the MAXPOW field in r.
 */
static u64 octeon_power_throttle_set_powlim(int cpu, unsigned long percentage)
{
	u64 t, csr_addr;
	union octeon_power_throttle_bits r;

	if (percentage > 100)
		return -EINVAL;

	csr_addr = octeon_power_throttle_csr_addr(cpu);
	r.raw = cvmx_read_csr(csr_addr);
	t = r.s.maxpow;
	if (!OCTEON_IS_MODEL(OCTEON_CN63XX)) {
		if (t < r.s.hrmpowadj)
			return -EINVAL;
		t -= r.s.hrmpowadj;
	}
	r.s.powlim = percentage > 0 ? percentage * t / 100 : 0;
	cvmx_write_csr(csr_addr, r.raw);
	return 0;
}

static ssize_t octeon_power_throttle_show_percentage(
	struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	unsigned int cpu = dev->id;
	ssize_t ret;

	get_online_cpus();
	if (cpu_online(cpu))
		ret = sprintf(buf, "%d\n",
			      octeon_power_throttle_get_powlim(cpu));
	else
		ret = 0;
	put_online_cpus();

	return ret;
}

static ssize_t octeon_power_throttle_store_percentage(
	struct device *dev,
	struct device_attribute *attr,
	const char *buf,
	size_t size)
{
	unsigned int cpu = dev->id;
	unsigned long val;
	int error;

	error = kstrtoul(buf, 0, &val);
	if (error)
		return error;

	get_online_cpus();
	if (cpu_online(cpu))
		octeon_power_throttle_set_powlim(cpu, val);
	put_online_cpus();

	return size;
}

static DEVICE_ATTR(percentage, S_IRUGO | S_IWUSR,
		   octeon_power_throttle_show_percentage,
		   octeon_power_throttle_store_percentage);

static struct attribute *octeon_power_throttle_attrs[] = {
	&dev_attr_percentage.attr,
	NULL
};

static struct attribute_group octeon_power_throttle_attr_group = {
	.attrs	= octeon_power_throttle_attrs,
	.name	= "power_throttle"
};

/* Mutex protecting device creation against CPU hotplug: */
static DEFINE_MUTEX(octeon_power_throttle_lock);

static __cpuinit int octeon_power_throttle_add_dev(struct device *dev)
{
	return sysfs_create_group(&dev->kobj,
				  &octeon_power_throttle_attr_group);
}

static __init int octeon_power_throttle_init(void)
{
	unsigned int cpu = 0;
	int err = 0;

	if (!OCTEON_IS_OCTEON2())
		return 0;

	get_online_cpus();
	/* connect live CPUs to sysfs */
	for_each_present_cpu(cpu) {
		err = octeon_power_throttle_add_dev(get_cpu_device(cpu));
		if (err) {
			pr_err("Error: octeon_power_throttle_add_dev() failed\n");
			break;
		}
		octeon_power_throttle_init_cpu(cpu);
	}
	put_online_cpus();
	return err;
}
device_initcall(octeon_power_throttle_init);
