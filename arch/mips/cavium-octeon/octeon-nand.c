/**
 * Driver for the Octeon NAND flash controller introduced in CN52XX pass 2.
 *
 * LICENSE:
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2008 - 2012 Cavium, Inc.
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>
#include <linux/of.h>

#include <asm/octeon/cvmx.h>
#include <asm/octeon/cvmx-nand.h>
#include <asm/octeon/octeon.h>

#define DRIVER_NAME "octeon-nand"

#define DEBUG_INIT		(1<<0)
#define DEBUG_READ		(1<<1)
#define DEBUG_READ_BUFFER	(1<<2)
#define DEBUG_WRITE		(1<<3)
#define DEBUG_WRITE_BUFFER	(1<<4)
#define DEBUG_CONTROL		(1<<5)
#define DEBUG_SELECT		(1<<6)
#define DEBUG_ALL		-1

#define MAX_NAND_NAME_LEN       20

static const char *part_probes[] = { "cmdlinepart", NULL };

#define DEV_DBG(_level, _dev, _format, _arg...)	do {			\
	if (unlikely(debug & (_level)))					\
		dev_info((_dev) , "%s " _format , __func__, ## _arg);	\
	} while (0)

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Debug bit field. -1 will turn on all debugging.");


struct octeon_nand {
	struct mtd_info mtd;
	struct nand_chip nand;
	/* Temporary location to store read data, must be 64 bit aligned */
	uint8_t data[NAND_MAX_PAGESIZE + NAND_MAX_OOBSIZE];
	int data_len;		/* Number of byte in the data buffer */
	int data_index;		/* Current read index. Equal to data_len when
					all data has been read */
	int selected_chip;	/* Currently selected NAND chip */
	int selected_page;	/* Last page chosen by SEQIN for PROGRAM */
	struct device *dev;	/* Pointer to the device */
};

static struct octeon_nand *octeon_nand_open_mtd[8];

/*
 * Read a single byte from the temporary buffer. Used after READID
 * to get the NAND information.
 */
static uint8_t octeon_nand_read_byte(struct mtd_info *mtd)
{
	struct octeon_nand *priv = container_of(mtd, struct octeon_nand, mtd);

	if (priv->data_index < priv->data_len) {
		DEV_DBG(DEBUG_READ, priv->dev, "read of 0x%02x\n",
			0xff & priv->data[priv->data_index]);
		return priv->data[priv->data_index++];
	} else {
		dev_err(priv->dev, "No data to read\n");
		return 0xff;
	}
}

/*
 * Read two bytes from the temporary buffer. Used after READID to
 * get the NAND information on 16 bit devices.
 *
 */
static uint16_t octeon_nand_read_word(struct mtd_info *mtd)
{
	struct octeon_nand *priv = container_of(mtd, struct octeon_nand, mtd);

	if (priv->data_index + 1 < priv->data_len) {
		uint16_t result = le16_to_cpup((uint16_t *)(priv->data +
			priv->data_index));
		priv->data_index += 2;
		DEV_DBG(DEBUG_READ, priv->dev, "read of 0x%04x\n",
			0xffff & result);
		return result;
	} else {
		dev_err(priv->dev, "No data to read\n");
		return 0xff;
	}
	return 0;
}

/*
 * Since we have a write page, I don't think this can ever be
 * called.
 */
static void octeon_nand_write_buf(struct mtd_info *mtd, const uint8_t *buf,
				int len)
{
	struct octeon_nand *priv = container_of(mtd, struct octeon_nand, mtd);

	DEV_DBG(DEBUG_WRITE_BUFFER, priv->dev, "len=%d\n", len);

	memcpy(priv->data + priv->data_index, buf, len);
	priv->data_index += len;
	priv->data_len += len;
	/* Linux sometimes thinks there is less OOB data than the chip really
		has. Make sure all OOB is set to 0xff */
	memset(priv->data + priv->data_index, 0xff,
		sizeof(priv->data) - priv->data_index);
}

/*
 * Read a number of pending bytes from the temporary buffer. Used
 * to get page and OOB data.
 */
static void octeon_nand_read_buf(struct mtd_info *mtd, uint8_t *buf, int len)
{
	struct octeon_nand *priv = container_of(mtd, struct octeon_nand, mtd);

	DEV_DBG(DEBUG_READ_BUFFER, priv->dev, "len=%d\n", len);

	if (len <= priv->data_len - priv->data_index) {
		memcpy(buf, priv->data + priv->data_index, len);
		priv->data_index += len;
	} else {
		dev_err(priv->dev,
			"Not enough data for read of %d bytes\n", len);
		priv->data_len = 0;
	}
}


/*
 * Verify the supplied buffer matches the data we last read
 */
static int octeon_nand_verify_buf(struct mtd_info *mtd, const uint8_t *buf,
				int len)
{
	struct octeon_nand *priv = container_of(mtd, struct octeon_nand, mtd);

	if (memcmp(buf, priv->data, len)) {
		dev_err(priv->dev, "Write verify failed\n");
		return -EFAULT;
	} else
		return 0;
}

/*
 * Select which NAND chip we are working on. A chip of -1
 * represents that no chip should be selected.
 */
static void octeon_nand_select_chip(struct mtd_info *mtd, int chip)
{
	/* We don't need to do anything here */
}

/*
 * Issue a NAND command to the chip. Almost all work is done here.
 */
static void octeon_nand_cmdfunc(struct mtd_info *mtd, unsigned command,
				int column, int page_addr)
{
	struct octeon_nand *priv = container_of(mtd, struct octeon_nand, mtd);
	struct nand_chip *nand = &priv->nand;
	int status;

	switch (command) {
	case NAND_CMD_READID:
		DEV_DBG(DEBUG_CONTROL, priv->dev, "READID\n");
		priv->data_index = 0;
		/*
		 * Read length must be a multiple of 8, so read a
		 * little more than we require.
		 */
		priv->data_len = cvmx_nand_read_id(priv->selected_chip, 0,
						virt_to_phys(priv->data), 16);
		if (priv->data_len < 16) {
			dev_err(priv->dev, "READID failed with %d\n",
				priv->data_len);
			priv->data_len = 0;
		}
		break;

	case NAND_CMD_READOOB:
		DEV_DBG(DEBUG_CONTROL, priv->dev,
			"READOOB page_addr=0x%x\n", page_addr);
		priv->data_index = 8;
		/*
		 * Read length must be a multiple of 8, so we start
		 * reading 8 bytes from the end of page.
		 */
		priv->data_len = cvmx_nand_page_read(priv->selected_chip,
					(page_addr << nand->page_shift) +
					(1 << nand->page_shift) - priv->data_index,
					virt_to_phys(priv->data),
					mtd->oobsize + priv->data_index);
		if (priv->data_len < mtd->oobsize + priv->data_index) {
			dev_err(priv->dev, "READOOB failed with %d\n",
				priv->data_len);
			priv->data_len = 0;
		}
		break;

	case NAND_CMD_READ0:
		DEV_DBG(DEBUG_CONTROL, priv->dev,
			"READ0 page_addr=0x%x\n", page_addr);
		priv->data_index = 0;
		/* Here mtd->oobsize _must_ already be a multiple of 8 */
		priv->data_len = cvmx_nand_page_read(priv->selected_chip,
					column +
					(page_addr << nand->page_shift),
					virt_to_phys(priv->data),
					(1 << nand->page_shift) +
					mtd->oobsize);
		if (priv->data_len < (1 << nand->page_shift) + mtd->oobsize) {
			dev_err(priv->dev, "READ0 failed with %d\n",
				priv->data_len);
			priv->data_len = 0;
		}
		break;

	case NAND_CMD_ERASE1:
		DEV_DBG(DEBUG_CONTROL, priv->dev,
			"ERASE1 page_addr=0x%x\n", page_addr);
		if (cvmx_nand_block_erase(priv->selected_chip,
			page_addr << nand->page_shift)) {
			dev_err(priv->dev, "ERASE1 failed\n");
		}
		break;

	case NAND_CMD_ERASE2:
		/* We do all erase processing in the first command, so ignore
			this one */
		break;

	case NAND_CMD_STATUS:
		DEV_DBG(DEBUG_CONTROL, priv->dev, "STATUS\n");
		priv->data_index = 0;
		priv->data_len = 2;
		priv->data[0] = cvmx_nand_get_status(priv->selected_chip);
		priv->data[1] = priv->data[0];
		break;

	case NAND_CMD_SEQIN:
		DEV_DBG(DEBUG_CONTROL, priv->dev,
			"SEQIN column=%d page_addr=0x%x\n", column, page_addr);
		/* If we don't seem to be doing sequential writes then erase
			all data assuming it is old */
		if (priv->data_index != column)
			memset(priv->data, 0xff, sizeof(priv->data));
		priv->data_index = column;
		priv->data_len = column;
		priv->selected_page = page_addr;
		break;

	case NAND_CMD_PAGEPROG:
		DEV_DBG(DEBUG_CONTROL, priv->dev, "PAGEPROG\n");
		status = cvmx_nand_page_write(priv->selected_chip,
			priv->selected_page << nand->page_shift,
			virt_to_phys(priv->data));
		if (status)
			dev_err(priv->dev, "PAGEPROG failed with %d\n",	status);
		break;

	case NAND_CMD_RESET:
		DEV_DBG(DEBUG_CONTROL, priv->dev, "RESET\n");
		priv->data_index = 0;
		priv->data_len = 0;
		memset(priv->data, 0xff, sizeof(priv->data));
		status = cvmx_nand_reset(priv->selected_chip);
		if (status)
			dev_err(priv->dev, "RESET failed with %d\n", status);
		break;

	default:
		dev_err(priv->dev, "Unsupported command 0x%x\n", command);
		break;
	}
}

/*
 * Determine what NAND devices are available
 */
static int octeon_nand_probe(struct platform_device *pdev)
{
	struct octeon_nand *priv;
	struct device_node *child_node;
	int rv;
	int chip;
	int active_chips = 0;
	char *name;
	int chip_num = 0; /* Count of detected chips, used for device naming */

	DEV_DBG(DEBUG_INIT, &pdev->dev, "called\n");

	for_each_child_of_node(pdev->dev.of_node, child_node) {
		u32 reg;
		rv = of_property_read_u32(child_node, "reg", &reg);
		if (rv)
			continue;
		active_chips |= (1 << reg);
	}
	if (!active_chips)
		return -ENODEV;

#if 0
	/*
	 * Optionally set defaults to be used for NAND chips that aren't
	 * recognized by cvmx_nand_initialize()
	 */
	cvmx_nand_set_defaults(2048, 64, 64, 2048, 2);
#endif
	cvmx_nand_initialize(0 /*CVMX_NAND_INITIALIZE_FLAGS_DONT_PROBE */,
			     active_chips);

	for (chip = 0; chip < 8; chip++) {
		/* Skip chip selects that don't have NAND */
		if ((active_chips & (1 << chip)) == 0)
			continue;

		/*
		 * Allocate and initialize mtd_info, nand_chip and private
		 * structures
		 */
		priv = devm_kzalloc(&pdev->dev,
				    sizeof(struct octeon_nand), GFP_KERNEL);
		if (!priv) {
			dev_err(&pdev->dev, "Unable to allocate structures\n");
			return -ENOMEM;
		}
		name = devm_kzalloc(&pdev->dev, MAX_NAND_NAME_LEN, GFP_KERNEL);
		if (!name) {
			dev_err(&pdev->dev, "Unable to allocate structures\n");
			return -ENOMEM;
		}

		priv->mtd.owner = THIS_MODULE;
		priv->mtd.priv = &priv->nand;
		memset(priv->data, 0xff, sizeof(priv->data));
		priv->dev = &pdev->dev;
		priv->selected_chip = chip;

		priv->nand.ecc.mode = NAND_ECC_SOFT;

		/* We always identify chips as 8 bit, as the Octeon NAND
		 * layer makes both 8 and 16 bit look the same.
		 * We never set the 16 bit buswidth option.
		 */

		priv->nand.read_byte = octeon_nand_read_byte;
		priv->nand.read_word = octeon_nand_read_word;
		priv->nand.write_buf = octeon_nand_write_buf;
		priv->nand.read_buf = octeon_nand_read_buf;
		priv->nand.verify_buf = octeon_nand_verify_buf;
		priv->nand.select_chip = octeon_nand_select_chip;
		priv->nand.cmdfunc = octeon_nand_cmdfunc;

		if (nand_scan(&priv->mtd, 1) != 0) {
			dev_err(&pdev->dev, "NAND scan failed\n");
			return -ENXIO;
		}

		/* We need to override the name, as the default names
		 * have spaces in them, and this prevents the passing
		 * of partitioning information on the kernel command line.
		 */
		snprintf(name, MAX_NAND_NAME_LEN, "octeon_nand%d", chip_num);
		priv->mtd.name = name;
		priv->mtd.dev.parent = &pdev->dev;

		mtd_device_parse_register(&priv->mtd, part_probes,
					  NULL, NULL, 0);

		octeon_nand_open_mtd[chip] = priv;
		chip_num++;
	}
	return 0;
}

/*
 * Called when the driver is unloaded. It must clean up all
 * created devices.
 */
static int octeon_nand_remove(struct platform_device *pdev)
{
	struct octeon_nand *priv;
	int chip;

	DEV_DBG(DEBUG_INIT, &pdev->dev, "called\n");
	for (chip = 0; chip < 8; chip++) {
		priv = octeon_nand_open_mtd[chip];
		if (priv) {
			mtd_device_unregister(&priv->mtd);
			octeon_nand_open_mtd[chip] = NULL;
		}
	}
	return 0;
}

static struct of_device_id octeon_nand_match[] = {
	{
		.compatible = "cavium,octeon-5230-nand",
	},
	{},
};

static struct platform_driver octeon_nand_driver = {
	.probe = octeon_nand_probe,
	.remove = __devexit_p(octeon_nand_remove),
	.driver = {
		.owner = THIS_MODULE,
		.name = DRIVER_NAME,
		.of_match_table = octeon_nand_match,
	},
};

static int __init octeon_nand_driver_init(void)
{
	return platform_driver_register(&octeon_nand_driver);
}
/*
 * We need to call octeon_nand_driver_init late enough that the MTD
 * core is already registered.  If built into the kernel , use a late
 * initcall.
 */
late_initcall(octeon_nand_driver_init);

static void __exit octeon_nand_driver_exit(void)
{
	platform_driver_unregister(&octeon_nand_driver);
}
module_exit(octeon_nand_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Cavium Inc. <support@cavium.com>");
MODULE_DESCRIPTION("Cavium Inc. OCTEON NAND driver.");
