/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2007, 2008, 2011 Cavium Networks
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/delay.h>

#include <asm/octeon/octeon.h>
#include <asm/octeon/cvmx-npei-defs.h>
#include <asm/octeon/cvmx-pciercx-defs.h>
#include <asm/octeon/cvmx-pescx-defs.h>
#include <asm/octeon/cvmx-pexp-defs.h>
#include <asm/octeon/cvmx-sli-defs.h>
#include <asm/octeon/cvmx-sriox-defs.h>
#include <asm/octeon/cvmx-pcie.h>
#include <asm/octeon/cvmx-pemx-defs.h>
#include <asm/octeon/pci-octeon.h>

/* Module parameter to disable PCI probing */
int disable;
module_param(disable, int, S_IRUGO);

#include <dma-coherence.h>

static int enable_pcie_14459_war;
static int enable_pcie_bus_num_war[2];
#ifdef OCT_PCIE_DBG
#define oct_pcie_dbg(fmt, ...) printk(fmt, ##__VA_ARGS__)
#else
#define oct_pcie_dbg(fmt, ...)
#endif
/**
 * Map a PCI device to the appropriate interrupt line
 *
 * @dev:    The Linux PCI device structure for the device to map
 * @slot:   The slot number for this device on __BUS 0__. Linux
 *               enumerates through all the bridges and figures out the
 *               slot on Bus 0 where this device eventually hooks to.
 * @pin:    The PCI interrupt pin read from the device, then swizzled
 *               as it goes through each bridge.
 * Returns Interrupt number for the device
 */
int __init octeon_pcie_pcibios_map_irq(const struct pci_dev *dev,
				       u8 slot, u8 pin)
{
	/*
	 * The EBH5600 board with the PCI to PCIe bridge mistakenly
	 * wires the first slot for both device id 2 and interrupt
	 * A. According to the PCI spec, device id 2 should be C. The
	 * following kludge attempts to fix this.
	 */
	if (strstr(octeon_board_type_string(), "EBH5600") &&
	    dev->bus && dev->bus->parent) {
		/*
		 * Iterate all the way up the device chain and find
		 * the root bus.
		 */
		while (dev->bus && dev->bus->parent)
			dev = to_pci_dev(dev->bus->bridge);
		/*
		 * If the root bus is number 0 and the PEX 8114 is the
		 * root, assume we are behind the miswired bus. We
		 * need to correct the swizzle level by two. Yuck.
		 */
		if ((dev->bus->number == 1) &&
		    (dev->vendor == 0x10b5) && (dev->device == 0x8114)) {
			/*
			 * The pin field is one based, not zero. We
			 * need to swizzle it by minus two.
			 */
			pin = ((pin - 3) & 3) + 1;
		}
	}
	/*
	 * The -1 is because pin starts with one, not zero. It might
	 * be that this equation needs to include the slot number, but
	 * I don't have hardware to check that against.
	 */
	return pin - 1 + OCTEON_IRQ_PCI_INT0;
}

void show_pcie_errors(void)
{
	cvmx_pemx_dbg_info_t pemx_dbg;
	cvmx_pemx_int_sum_t pemx_int_sum;
	pemx_dbg.u64 = cvmx_read_csr(CVMX_PEMX_DBG_INFO(1));
	pemx_int_sum.u64 = cvmx_read_csr(CVMX_PEMX_INT_SUM(1));
	oct_pcie_dbg("ints=%llx  dbg=%llx ", pemx_int_sum.u64, pemx_dbg.u64);
}


void show_pemx_ctl_status(void)
{
	cvmx_pemx_ctl_status_t pemx_ctl;
	pemx_ctl.u64 = cvmx_read_csr(CVMX_PEMX_CTL_STATUS(1));
	oct_pcie_dbg("ctl= %llx ", pemx_ctl.u64);
}

static  void set_cfg_read_retry(u32 retry_cnt)
{
	cvmx_pemx_ctl_status_t pemx_ctl;
	pemx_ctl.u64 = cvmx_read_csr(CVMX_PEMX_CTL_STATUS(1));
	pemx_ctl.s.cfg_rtry = retry_cnt;
	cvmx_write_csr(CVMX_PEMX_CTL_STATUS(1), pemx_ctl.u64);
}


static u32 disable_cfg_read_retry(void)
{
	u32 retry_cnt;

	cvmx_pemx_ctl_status_t pemx_ctl;
	pemx_ctl.u64 = cvmx_read_csr(CVMX_PEMX_CTL_STATUS(1));
	retry_cnt =  pemx_ctl.s.cfg_rtry;
	pemx_ctl.s.cfg_rtry = 0;
	cvmx_write_csr(CVMX_PEMX_CTL_STATUS(1), pemx_ctl.u64);
	return retry_cnt;
}

static int is_cfg_retry(void)
{
	cvmx_pemx_int_sum_t pemx_int_sum;
	pemx_int_sum.u64 = cvmx_read_csr(CVMX_PEMX_INT_SUM(1));
	if (pemx_int_sum.s.crs_dr)
		return 1;
	return 0;
}

/**
 * Read a value from configuration space
 *
 * @bus:
 * @devfn:
 * @reg:
 * @size:
 * @val:
 * Returns
 */
static int octeon_pcie_read_config(unsigned int pcie_port, struct pci_bus *bus,
				   unsigned int devfn, int reg, int size,
				   u32 *val)
{
	union octeon_cvmemctl cvmmemctl;
	union octeon_cvmemctl cvmmemctl_save;
	int bus_number = bus->number;
	int cfg_retry = 0;
	int retry_cnt = 0;
	int max_retry_cnt = 10;
	u32 cfg_retry_cnt = 0;


	BUG_ON(pcie_port >= ARRAY_SIZE(enable_pcie_bus_num_war));
	/*
	 * For the top level bus make sure our hardware bus number
	 * matches the software one
	 */
	if (bus->parent == NULL) {
		if (enable_pcie_bus_num_war[pcie_port])
			bus_number = 0;
		else {
			cvmx_pciercx_cfg006_t pciercx_cfg006;
			pciercx_cfg006.u32 = cvmx_pcie_cfgx_read(pcie_port,
					     CVMX_PCIERCX_CFG006(pcie_port));
			if (pciercx_cfg006.s.pbnum != bus_number) {
				pciercx_cfg006.s.pbnum = bus_number;
				pciercx_cfg006.s.sbnum = bus_number;
				pciercx_cfg006.s.subbnum = bus_number;
				cvmx_pcie_cfgx_write(pcie_port,
					    CVMX_PCIERCX_CFG006(pcie_port),
					    pciercx_cfg006.u32);
			}
		}
	}

	/*
	 * PCIe only has a single device connected to Octeon. It is
	 * always device ID 0. Don't bother doing reads for other
	 * device IDs on the first segment.
	 */
	if ((bus->parent == NULL) && (devfn >> 3 != 0))
		return PCIBIOS_FUNC_NOT_SUPPORTED;

	/*
	 * The following is a workaround for the CN57XX, CN56XX,
	 * CN55XX, and CN54XX errata with PCIe config reads from non
	 * existent devices.  These chips will hang the PCIe link if a
	 * config read is performed that causes a UR response.
	 */
	if (OCTEON_IS_MODEL(OCTEON_CN56XX_PASS1) ||
	    OCTEON_IS_MODEL(OCTEON_CN56XX_PASS1_1)) {
		/*
		 * For our EBH5600 board, port 0 has a bridge with two
		 * PCI-X slots. We need a new special checks to make
		 * sure we only probe valid stuff.  The PCIe->PCI-X
		 * bridge only respondes to device ID 0, function
		 * 0-1
		 */
		if ((bus->parent == NULL) && (devfn >= 2))
			return PCIBIOS_FUNC_NOT_SUPPORTED;
		/*
		 * The PCI-X slots are device ID 2,3. Choose one of
		 * the below "if" blocks based on what is plugged into
		 * the board.
		 */
#if 1
		/* Use this option if you aren't using either slot */
		if (bus_number == 2)
			return PCIBIOS_FUNC_NOT_SUPPORTED;
#elif 0
		/*
		 * Use this option if you are using the first slot but
		 * not the second.
		 */
		if ((bus_number == 2) && (devfn >> 3 != 2))
			return PCIBIOS_FUNC_NOT_SUPPORTED;
#elif 0
		/*
		 * Use this option if you are using the second slot
		 * but not the first.
		 */
		if ((bus_number == 2) && (devfn >> 3 != 3))
			return PCIBIOS_FUNC_NOT_SUPPORTED;
#elif 0
		/* Use this opion if you are using both slots */
		if ((bus_number == 2) &&
		    !((devfn == (2 << 3)) || (devfn == (3 << 3))))
			return PCIBIOS_FUNC_NOT_SUPPORTED;
#endif

		/* The following #if gives a more complicated example. This is
		   the required checks for running a Nitrox CN16XX-NHBX in the
		   slot of the EBH5600. This card has a PLX PCIe bridge with
		   four Nitrox PLX parts behind it */
#if 0
		/* PLX bridge with 4 ports */
		if ((bus_number == 4) &&
		    !((devfn >> 3 >= 1) && (devfn >> 3 <= 4)))
			return PCIBIOS_FUNC_NOT_SUPPORTED;
		/* Nitrox behind PLX 1 */
		if ((bus_number == 5) && (devfn >> 3 != 0))
			return PCIBIOS_FUNC_NOT_SUPPORTED;
		/* Nitrox behind PLX 2 */
		if ((bus_number == 6) && (devfn >> 3 != 0))
			return PCIBIOS_FUNC_NOT_SUPPORTED;
		/* Nitrox behind PLX 3 */
		if ((bus_number == 7) && (devfn >> 3 != 0))
			return PCIBIOS_FUNC_NOT_SUPPORTED;
		/* Nitrox behind PLX 4 */
		if ((bus_number == 8) && (devfn >> 3 != 0))
			return PCIBIOS_FUNC_NOT_SUPPORTED;
#endif

		/*
		 * Shorten the DID timeout so bus errors for PCIe
		 * config reads from non existent devices happen
		 * faster. This allows us to continue booting even if
		 * the above "if" checks are wrong.  Once one of these
		 * errors happens, the PCIe port is dead.
		 */
		cvmmemctl_save.u64 = __read_64bit_c0_register($11, 7);
		cvmmemctl.u64 = cvmmemctl_save.u64;
		cvmmemctl.s.didtto = 2;
		__write_64bit_c0_register($11, 7, cvmmemctl.u64);
	}

	if ((OCTEON_IS_MODEL(OCTEON_CN63XX)) && (enable_pcie_14459_war))
		cfg_retry_cnt = disable_cfg_read_retry();

	/*show_pemx_ctl_status(); show_pcie_errors();*/
	oct_pcie_dbg("pcie_cfg_rd port=%d b=%d devfn=0x%03x reg=0x%03x"
		" size=%d ", pcie_port, bus_number, devfn, reg, size);
	do {
		switch (size) {
		case 4:
			*val = cvmx_pcie_config_read32(pcie_port, bus_number,
				devfn >> 3, devfn & 0x7, reg);
		break;
		case 2:
			*val = cvmx_pcie_config_read16(pcie_port, bus_number,
				devfn >> 3, devfn & 0x7, reg);
		break;
		case 1:
			*val = cvmx_pcie_config_read8(pcie_port, bus_number,
				devfn >> 3, devfn & 0x7, reg);
		break;
		default:
			if (OCTEON_IS_MODEL(OCTEON_CN63XX))
				set_cfg_read_retry(cfg_retry_cnt);
			return PCIBIOS_FUNC_NOT_SUPPORTED;
		}
		if ((OCTEON_IS_MODEL(OCTEON_CN63XX)) &&
			(enable_pcie_14459_war)) {
			cfg_retry = is_cfg_retry();
			retry_cnt++;
			if (retry_cnt > max_retry_cnt) {
				printk(KERN_ERR " pcie cfg_read retries "
				"failed. retry_cnt=%d\n", retry_cnt);
				cfg_retry = 0;
			}
		}
	} while (cfg_retry);

	if ((OCTEON_IS_MODEL(OCTEON_CN63XX)) && (enable_pcie_14459_war))
		set_cfg_read_retry(cfg_retry_cnt);
	oct_pcie_dbg("val=%08x  : tries=%02d \n", *val, retry_cnt);
	if (OCTEON_IS_MODEL(OCTEON_CN56XX_PASS1) ||
	    OCTEON_IS_MODEL(OCTEON_CN56XX_PASS1_1))
		write_c0_cvmmemctl(cvmmemctl_save.u64);
	return PCIBIOS_SUCCESSFUL;
}

static int octeon_pcie0_read_config(struct pci_bus *bus, unsigned int devfn,
				    int reg, int size, u32 *val)
{
	return octeon_pcie_read_config(0, bus, devfn, reg, size, val);
}

static int octeon_pcie1_read_config(struct pci_bus *bus, unsigned int devfn,
				    int reg, int size, u32 *val)
{
	return octeon_pcie_read_config(1, bus, devfn, reg, size, val);
}

static int octeon_dummy_read_config(struct pci_bus *bus, unsigned int devfn,
				    int reg, int size, u32 *val)
{
	return PCIBIOS_FUNC_NOT_SUPPORTED;
}

/**
 * Write a value to PCI configuration space
 *
 * @bus:
 * @devfn:
 * @reg:
 * @size:
 * @val:
 * Returns
 */
static int octeon_pcie_write_config(unsigned int pcie_port, struct pci_bus *bus,
				    unsigned int devfn, int reg,
				    int size, u32 val)
{
	int bus_number = bus->number;

	BUG_ON(pcie_port >= ARRAY_SIZE(enable_pcie_bus_num_war));

	if ((bus->parent == NULL) && (enable_pcie_bus_num_war[pcie_port]))
		bus_number = 0;

	oct_pcie_dbg("pcie_cfg_wr port=%d b=%d devfn=0x%03x"
		" reg=0x%03x size=%d val=%08x\n", pcie_port, bus_number, devfn,
		reg, size, val);


	switch (size) {
	case 4:
		cvmx_pcie_config_write32(pcie_port, bus_number, devfn >> 3,
					 devfn & 0x7, reg, val);
		break;
	case 2:
		cvmx_pcie_config_write16(pcie_port, bus_number, devfn >> 3,
					 devfn & 0x7, reg, val);
		break;
	case 1:
		cvmx_pcie_config_write8(pcie_port, bus_number, devfn >> 3,
					devfn & 0x7, reg, val);
		break;
	default:
		return PCIBIOS_FUNC_NOT_SUPPORTED;
	}
#if PCI_CONFIG_SPACE_DELAY
	/*
	 * Delay on writes so that devices have time to come up. Some
	 * bridges need this to allow time for the secondary busses to
	 * work
	 */
	udelay(PCI_CONFIG_SPACE_DELAY);
#endif
	return PCIBIOS_SUCCESSFUL;
}

static int octeon_pcie0_write_config(struct pci_bus *bus, unsigned int devfn,
				     int reg, int size, u32 val)
{
	return octeon_pcie_write_config(0, bus, devfn, reg, size, val);
}

static int octeon_pcie1_write_config(struct pci_bus *bus, unsigned int devfn,
				     int reg, int size, u32 val)
{
	return octeon_pcie_write_config(1, bus, devfn, reg, size, val);
}

static int octeon_dummy_write_config(struct pci_bus *bus, unsigned int devfn,
				     int reg, int size, u32 val)
{
	return PCIBIOS_FUNC_NOT_SUPPORTED;
}

static struct pci_ops octeon_pcie0_ops = {
	octeon_pcie0_read_config,
	octeon_pcie0_write_config,
};

static struct resource octeon_pcie0_mem_resource = {
	.name = "Octeon PCIe0 MEM",
	.flags = IORESOURCE_MEM,
};

static struct resource octeon_pcie0_io_resource = {
	.name = "Octeon PCIe0 IO",
	.flags = IORESOURCE_IO,
};

static struct pci_controller octeon_pcie0_controller = {
	.pci_ops = &octeon_pcie0_ops,
	.mem_resource = &octeon_pcie0_mem_resource,
	.io_resource = &octeon_pcie0_io_resource,
};

static struct pci_ops octeon_pcie1_ops = {
	octeon_pcie1_read_config,
	octeon_pcie1_write_config,
};

static struct resource octeon_pcie1_mem_resource = {
	.name = "Octeon PCIe1 MEM",
	.flags = IORESOURCE_MEM,
};

static struct resource octeon_pcie1_io_resource = {
	.name = "Octeon PCIe1 IO",
	.flags = IORESOURCE_IO,
};

static struct pci_controller octeon_pcie1_controller = {
	.pci_ops = &octeon_pcie1_ops,
	.mem_resource = &octeon_pcie1_mem_resource,
	.io_resource = &octeon_pcie1_io_resource,
};

static struct pci_ops octeon_dummy_ops = {
	octeon_dummy_read_config,
	octeon_dummy_write_config,
};

static struct resource octeon_dummy_mem_resource = {
	.name = "Virtual PCIe MEM",
	.flags = IORESOURCE_MEM,
};

static struct resource octeon_dummy_io_resource = {
	.name = "Virtual PCIe IO",
	.flags = IORESOURCE_IO,
};

static struct pci_controller octeon_dummy_controller = {
	.pci_ops = &octeon_dummy_ops,
	.mem_resource = &octeon_dummy_mem_resource,
	.io_resource = &octeon_dummy_io_resource,
};

static int device_needs_bus_num_war(uint32_t deviceid)
{
#define IDT_VENDOR_ID 0x111d

	if ((deviceid  & 0xffff) == IDT_VENDOR_ID)
		return 1;
	return 0;
}

/**
 * Initialize the Octeon PCIe controllers
 *
 * Returns
 */
static int __init octeon_pcie_setup(void)
{
	int result;
	int host_mode;
	int srio_war15205 = 0, port;
	cvmx_sli_ctl_portx_t sli_ctl_portx;
	cvmx_sriox_status_reg_t sriox_status_reg;

	/* These chips don't have PCIe */
	if (!octeon_has_feature(OCTEON_FEATURE_PCIE))
		return 0;

	/* No PCIe simulation */
	if (octeon_is_simulation())
		return 0;

	/* Disable PCI if instructed on the command line */
	if (disable)
		return 0;

	/* Point pcibios_map_irq() to the PCIe version of it */
	octeon_pcibios_map_irq = octeon_pcie_pcibios_map_irq;

	/*
	 * PCIe I/O range. It is based on port 0 but includes up until
	 * port 1's end.
	 */
	set_io_port_base(CVMX_ADD_IO_SEG(cvmx_pcie_get_io_base_address(0)));
	ioport_resource.start = 0;
	ioport_resource.end =
		cvmx_pcie_get_io_base_address(1) -
		cvmx_pcie_get_io_base_address(0) + cvmx_pcie_get_io_size(1) - 1;

	/*
	 * Create a dummy PCIe controller to swallow up bus 0. IDT bridges
	 * don't work if the primary bus number is zero. Here we add a fake
	 * PCIe controller that the kernel will give bus 0. This allows
	 * us to not change the normal kernel bus enumeration
	 */
	octeon_dummy_controller.io_map_base = -1;
	octeon_dummy_controller.mem_resource->start = (1ull<<48);
	octeon_dummy_controller.mem_resource->end = (1ull<<48);
	register_pci_controller(&octeon_dummy_controller);

	if (octeon_has_feature(OCTEON_FEATURE_NPEI)) {
		union cvmx_npei_ctl_status npei_ctl_status;
		npei_ctl_status.u64 = cvmx_read_csr(CVMX_PEXP_NPEI_CTL_STATUS);
		host_mode = npei_ctl_status.s.host_mode;
		octeon_dma_bar_type = OCTEON_DMA_BAR_TYPE_PCIE;
	} else {
		union cvmx_mio_rst_ctlx mio_rst_ctl;
		mio_rst_ctl.u64 = cvmx_read_csr(CVMX_MIO_RST_CTLX(0));
		host_mode = mio_rst_ctl.s.host_mode;
		octeon_dma_bar_type = OCTEON_DMA_BAR_TYPE_PCIE2;
	}

	if (host_mode) {
		pr_notice("PCIe: Initializing port 0\n");
		/* CN63XX pass 1_x/2.0 errata PCIe-15205 */
		if (OCTEON_IS_MODEL(OCTEON_CN63XX_PASS1_X) ||
			OCTEON_IS_MODEL(OCTEON_CN63XX_PASS2_0)) {
			sriox_status_reg.u64 = cvmx_read_csr(CVMX_SRIOX_STATUS_REG(0));
			if (sriox_status_reg.s.srio) {
				srio_war15205 += 1;      /* Port is SRIO */
				port = 0;
			}
		}
		result = cvmx_pcie_rc_initialize(0);
		if (result == 0) {
			uint32_t device0;
			/* Memory offsets are physical addresses */
			octeon_pcie0_controller.mem_offset =
				cvmx_pcie_get_mem_base_address(0);
			/* IO offsets are Mips virtual addresses */
			octeon_pcie0_controller.io_map_base =
				CVMX_ADD_IO_SEG(cvmx_pcie_get_io_base_address
						(0));
			octeon_pcie0_controller.io_offset = 0;
			/*
			 * To keep things similar to PCI, we start
			 * device addresses at the same place as PCI
			 * uisng big bar support. This normally
			 * translates to 4GB-256MB, which is the same
			 * as most x86 PCs.
			 */
			octeon_pcie0_controller.mem_resource->start =
				cvmx_pcie_get_mem_base_address(0) +
				(4ul << 30) - (OCTEON_PCI_BAR1_HOLE_SIZE << 20);
			octeon_pcie0_controller.mem_resource->end =
				cvmx_pcie_get_mem_base_address(0) +
				cvmx_pcie_get_mem_size(0) - 1;
			/*
			 * Ports must be above 16KB for the ISA bus
			 * filtering in the PCI-X to PCI bridge.
			 */
			octeon_pcie0_controller.io_resource->start = 4 << 10;
			octeon_pcie0_controller.io_resource->end =
				cvmx_pcie_get_io_size(0) - 1;
			msleep(100); /* Some devices need extra time */
			register_pci_controller(&octeon_pcie0_controller);
			device0 = cvmx_pcie_config_read32(0, 0, 0, 0, 0);
			enable_pcie_bus_num_war[0] =
				device_needs_bus_num_war(device0);
		}
	} else {
		pr_notice("PCIe: Port 0 in endpoint mode, skipping.\n");
		/* CN63XX pass 1_x/2.0 errata PCIe-15205 */
		if (OCTEON_IS_MODEL(OCTEON_CN63XX_PASS1_X) ||
			OCTEON_IS_MODEL(OCTEON_CN63XX_PASS2_0)) {
			srio_war15205 += 1;
			port = 0;
		}
	}

	if (octeon_has_feature(OCTEON_FEATURE_NPEI)) {
		host_mode = 1;
		/* Skip the 2nd port on CN52XX if port 0 is in 4 lane mode */
		if (OCTEON_IS_MODEL(OCTEON_CN52XX)) {
			cvmx_npei_dbg_data_t dbg_data;
			dbg_data.u64 = cvmx_read_csr(CVMX_PEXP_NPEI_DBG_DATA);
			if (dbg_data.cn52xx.qlm0_link_width)
				host_mode = 0;
		}
	} else {
		cvmx_mio_rst_ctlx_t mio_rst_ctl;
		mio_rst_ctl.u64 = cvmx_read_csr(CVMX_MIO_RST_CTLX(1));
		host_mode = mio_rst_ctl.s.host_mode;
	}

	if (host_mode) {
		pr_notice("PCIe: Initializing port 1\n");
		/* CN63XX pass 1_x/2.0 errata PCIe-15205 */
		if (OCTEON_IS_MODEL(OCTEON_CN63XX_PASS1_X) ||
			OCTEON_IS_MODEL(OCTEON_CN63XX_PASS2_0)) {
			sriox_status_reg.u64 = cvmx_read_csr(CVMX_SRIOX_STATUS_REG(1));
			if (sriox_status_reg.s.srio) {
				srio_war15205 += 1;      /* Port is SRIO */
				port = 1;
			}
		}
		result = cvmx_pcie_rc_initialize(1);
		if (result == 0) {
			uint32_t device0;
			/* Memory offsets are physical addresses */
			octeon_pcie1_controller.mem_offset =
				cvmx_pcie_get_mem_base_address(1);
			/*
			 * To calculate the address for accessing the 2nd PCIe device,
			 * either 'io_map_base' (pci_iomap()), or 'mips_io_port_base'
			 * (ioport_map()) value is added to
			 * pci_resource_start(dev,bar)). The 'mips_io_port_base' is set
			 * only once based on first PCIe. Also changing 'io_map_base'
			 * based on first slot's value so that both the routines will
			 * work properly.
			 */
			octeon_pcie1_controller.io_map_base =
				CVMX_ADD_IO_SEG(cvmx_pcie_get_io_base_address(0));
			/* IO offsets are Mips virtual addresses */
			octeon_pcie1_controller.io_offset =
				cvmx_pcie_get_io_base_address(1) -
				cvmx_pcie_get_io_base_address(0);
			/*
			 * To keep things similar to PCI, we start device
			 * addresses at the same place as PCI uisng big bar
			 * support. This normally translates to 4GB-256MB,
			 * which is the same as most x86 PCs.
			 */
			octeon_pcie1_controller.mem_resource->start =
				cvmx_pcie_get_mem_base_address(1) + (4ul << 30) -
				(OCTEON_PCI_BAR1_HOLE_SIZE << 20);
			octeon_pcie1_controller.mem_resource->end =
				cvmx_pcie_get_mem_base_address(1) +
				cvmx_pcie_get_mem_size(1) - 1;
			/*
			 * Ports must be above 16KB for the ISA bus filtering
			 * in the PCI-X to PCI bridge.
			 */
			octeon_pcie1_controller.io_resource->start =
				cvmx_pcie_get_io_base_address(1) -
				cvmx_pcie_get_io_base_address(0);
			octeon_pcie1_controller.io_resource->end =
				octeon_pcie1_controller.io_resource->start +
				cvmx_pcie_get_io_size(1) - 1;
			msleep(100); /* Some devices need extra time */
			register_pci_controller(&octeon_pcie1_controller);
			device0 = cvmx_pcie_config_read32(1, 0, 0, 0, 0);
			enable_pcie_bus_num_war[1] =
				device_needs_bus_num_war(device0);
		}
//		octeon_pci_dma_init();
	} else {
		pr_notice("PCIe: Port 1 not in root complex mode, skipping.\n");
		/* CN63XX pass 1_x/2.0 errata PCIe-15205  */
		if (OCTEON_IS_MODEL(OCTEON_CN63XX_PASS1_X) ||
			OCTEON_IS_MODEL(OCTEON_CN63XX_PASS2_0)) {
			srio_war15205 += 1;
			port = 1;
		}
	}

	/*
	 * CN63XX pass 1_x/2.0 errata PCIe-15205 requires setting all
	 * of SRIO MACs SLI_CTL_PORT*[INT*_MAP] to similar value and
	 * all of PCIe Macs SLI_CTL_PORT*[INT*_MAP] to different value
	 * from the previous set values
	 */
	if (OCTEON_IS_MODEL(OCTEON_CN63XX_PASS1_X) ||
		OCTEON_IS_MODEL(OCTEON_CN63XX_PASS2_0)) {
		if (srio_war15205 == 1) {
			sli_ctl_portx.u64 = cvmx_read_csr(CVMX_PEXP_SLI_CTL_PORTX(port));
			sli_ctl_portx.s.inta_map = 1;
			sli_ctl_portx.s.intb_map = 1;
			sli_ctl_portx.s.intc_map = 1;
			sli_ctl_portx.s.intd_map = 1;
			cvmx_write_csr(CVMX_PEXP_SLI_CTL_PORTX(port), sli_ctl_portx.u64);

			sli_ctl_portx.u64 = cvmx_read_csr(CVMX_PEXP_SLI_CTL_PORTX(!port));
			sli_ctl_portx.s.inta_map = 0;
			sli_ctl_portx.s.intb_map = 0;
			sli_ctl_portx.s.intc_map = 0;
			sli_ctl_portx.s.intd_map = 0;
			cvmx_write_csr(CVMX_PEXP_SLI_CTL_PORTX(!port), sli_ctl_portx.u64);
		}
	}

	return 0;
}
arch_initcall(octeon_pcie_setup);
