#
# Makefile for the Cavium Octeon specific kernel interface routines
# under Linux.
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 2005-2008 Cavium Networks
#

obj-y += cvmx-bootmem.o cvmx-l2c.o cvmx-sysinfo.o octeon-model.o
obj-y += cvmx-pko.o cvmx-spi.o cvmx-spi4000.o cvmx-cmd-queue.o cvmx-ipd.o \
	cvmx-ilk.o cvmx-srio.o cvmx-qlm.o cvmx-qlm-tables.o cvmx-twsi.o \
	cvmx-fpa-resource.o

obj-y += cvmx-helper-board.o cvmx-helper.o cvmx-helper-xaui.o \
	cvmx-helper-rgmii.o cvmx-helper-sgmii.o cvmx-helper-npi.o \
	cvmx-helper-loop.o cvmx-helper-spi.o cvmx-helper-util.o \
	cvmx-helper-errata.o cvmx-helper-jtag.o cvmx-helper-cfg.o \
	cvmx-helper-srio.o cvmx-helper-ilk.o

obj-y += cvmx-helper-errata.o cvmx-helper-jtag.o

obj-y += cvmx-helper-errata.o cvmx-helper-jtag.o cvmx-pcie.o

obj-y += cvmx-global-resources.o cvmx-range.o \
	cvmx-pko-internal-ports-range.o cvmx-appcfg-transport.o

obj-y += cvmx-clock.o octeon-feature.o

obj-$(CONFIG_USB_OCTEON_HCD) += cvmx-usb.o
obj-$(CONFIG_CAVIUM_OCTEON_RAPIDIO) += cvmx-dma-engine.o
obj-$(CONFIG_CAVIUM_OCTEON_NAND)	+= cvmx-nand.o
obj-$(CONFIG_CAVIUM_OCTEON_ERROR_TREE)	+= cvmx-error-trees.o
