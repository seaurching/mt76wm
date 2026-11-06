/* SPDX-License-Identifier: GPL-2.0 */
/* Linux compat PCI stub for the mt76wm XNU port. Ported for mt76wm (XNU) */
#ifndef MTK_LC_PCI_H
#define MTK_LC_PCI_H

#include <linux/mtk_core.h>
#include <linux/slab.h>

#define PCI_VENDOR_ID_MEDIATEK  0x14c3
#define PCI_VENDOR_ID_ITTIM     0x0b48

/* PCIe link speed/width enums (Linux include/uapi/linux/pci_regs.h); used
 * by the mt7996 HIF bookkeeping only - values are placeholders. */
enum pci_bus_speed {
    PCI_SPEED_UNKNOWN = 0,
    PCIE_SPEED_2_5GT,
    PCIE_SPEED_5_0GT,
    PCIE_SPEED_8_0GT,
    PCIE_SPEED_16_0GT,
};

enum pcie_link_width {
    PCIE_LNK_WIDTH_UNKNOWN = 0,
    PCIE_LNK_X1,
    PCIE_LNK_X2,
    PCIE_LNK_X4,
    PCIE_LNK_X8,
    PCIE_LNK_X16,
};

#define PCI_COMMAND         0x04
#define PCI_COMMAND_MEMORY  0x02
#define PCI_COMMAND_MASTER  0x04

#define PCI_IRQ_ALL_TYPES   0x03
#define PCI_IRQ_MSIX        0x08
#define PCI_IRQ_MSI         0x04

struct pci_device_id {
    u32 vendor;
    u32 device;
    u32 subvendor;
    u32 subdevice;
    u32 dev_class;
    u32 class_mask;
    kernel_ulong_t driver_data;
};

#define PCI_DEVICE(vend, dev) \
    .vendor = (vend), .device = (dev), \
    .subvendor = PCI_ANY_ID, .subdevice = PCI_ANY_ID

#define PCI_ANY_ID (~0u)

struct pci_dev {
    struct device dev;
    u16 vendor;
    u16 device;
    u8 irq;
    u8 revision;
    void *regs;                 /* glue: BAR0 virtual address */
    unsigned long driver_flags;
};

static inline int pcim_enable_device(struct pci_dev *pdev)
{
    (void)pdev;
    return 0;
}

static inline int pci_enable_device_mem(struct pci_dev *pdev)
{
    (void)pdev;
    return 0;
}

static inline void pci_disable_device(struct pci_dev *pdev)
{
    (void)pdev;
}

static inline void pci_set_master(struct pci_dev *pdev)
{
    (void)pdev;
}

static inline void pci_clear_master(struct pci_dev *pdev)
{
    (void)pdev;
}

static inline int pci_read_config_word(struct pci_dev *pdev, int where,
                                       u16 *val)
{
    (void)pdev; (void)where;
    if (val)
        *val = 0;
    return 0;
}

static inline int pci_write_config_word(struct pci_dev *pdev, int where,
                                        u16 val)
{
    (void)pdev; (void)where; (void)val;
    return 0;
}

static inline int pci_read_config_dword(struct pci_dev *pdev, int where,
                                        u32 *val)
{
    (void)pdev; (void)where;
    if (val)
        *val = 0;
    return 0;
}

static inline int pci_write_config_dword(struct pci_dev *pdev, int where,
                                         u32 val)
{
    (void)pdev; (void)where; (void)val;
    return 0;
}

static inline void pci_save_state(struct pci_dev *pdev) { (void)pdev; }
static inline void pci_restore_state(struct pci_dev *pdev) { (void)pdev; }

static inline mtk_any_ptr pci_get_drvdata(struct pci_dev *pdev)
{
    return dev_get_drvdata(&pdev->dev);
}

static inline void pci_set_drvdata(struct pci_dev *pdev, void *data)
{
    dev_set_drvdata(&pdev->dev, data);
}

static inline const char *pci_name(const struct pci_dev *pdev)
{
    return dev_name(&((struct pci_dev *)pdev)->dev);
}

static inline int pci_alloc_irq_vectors(struct pci_dev *pdev, unsigned int min,
                                        unsigned int max, unsigned int flags)
{
    (void)pdev; (void)min; (void)max; (void)flags;
    return 1;
}

static inline void pci_free_irq_vectors(struct pci_dev *pdev)
{
    (void)pdev;
}

/* glue provides the already-mapped BAR0 pointer via pdev->regs */
void __iomem *mtk_pcim_iomap_region(struct pci_dev *pdev, int bar,
                                    const char *name);
#define pcim_iomap_region(pdev, bar, name) mtk_pcim_iomap_region((pdev), (bar), (name))

/* Multi-region variant: the HAL already mapped BAR0 (pdev->regs), so the
 * region set is considered managed and the table just exposes it. */
static inline int pcim_iomap_regions(struct pci_dev *pdev, unsigned int mask,
                                     const char *name)
{
    (void)pdev; (void)mask; (void)name;
    return 0;
}

static inline void __iomem * const *pcim_iomap_table(struct pci_dev *pdev)
{
    static void __iomem *bars[6];

    bars[0] = pdev->regs;
    return bars;
}

#define pci_iomap(pdev, bar, size) ((void __iomem *)(pdev)->regs)
#define pci_iounmap(pdev, addr) do { } while (0)

struct pci_error_handlers;

struct device_driver_stub {
    const void *pm;
};

struct pci_driver {
    const char *name;
    const struct pci_device_id *id_table;
    int (*probe)(struct pci_dev *pdev, const struct pci_device_id *id);
    void (*remove)(struct pci_dev *pdev);
    void (*shutdown)(struct pci_dev *pdev);
    struct device_driver_stub driver;
    const struct pci_error_handlers *err_handler;
};

/* PCI error recovery stubs (not supported) */
typedef int pci_ers_result_t;
typedef int pci_channel_state_t;

#define PCI_ERS_RESULT_NONE         0
#define PCI_ERS_RESULT_CAN_RECOVER  1
#define PCI_ERS_RESULT_NEED_RESET   2
#define PCI_ERS_RESULT_DISCONNECT   3
#define PCI_ERS_RESULT_RECOVERED    4

#define pci_channel_io_normal       0
#define pci_channel_io_frozen       1
#define pci_channel_io_perm_failure 2

struct pci_error_handlers {
    pci_ers_result_t (*error_detected)(struct pci_dev *dev,
                                       pci_channel_state_t state);
    pci_ers_result_t (*mmio_enabled)(struct pci_dev *dev);
    pci_ers_result_t (*slot_reset)(struct pci_dev *dev);
    void (*resume)(struct pci_dev *dev);
};

/* Power management stubs (mt76x2/pci.c suspend/resume paths). */
typedef int pm_message_t;

#define PCI_D0      0
#define PCI_D1      1
#define PCI_D2      2
#define PCI_D3hot   3
#define PCI_D3cold  4

static inline int pci_choose_state(struct pci_dev *pdev, pm_message_t state)
{
    (void)pdev;
    (void)state;
    return PCI_D0;
}

static inline int pci_set_power_state(struct pci_dev *pdev, int state)
{
    (void)pdev;
    (void)state;
    return 0;
}

static inline int pci_enable_wake(struct pci_dev *pdev, int state, bool enable)
{
    (void)pdev;
    (void)state;
    (void)enable;
    return 0;
}

#endif /* MTK_LC_PCI_H */
