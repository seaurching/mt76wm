/* SPDX-License-Identifier: GPL-2.0 */
/* Linux USB compat for the mt76wm XNU port (mt7601u USB transport).
 * The transfer plumbing is implemented over IOUSBHostFamily in
 * mtkwm/mt7601u/usb.c; this header only carries the Linux-facing types.
 */
#ifndef MTK_LC_USB_H
#define MTK_LC_USB_H

#include <linux/mtk_core.h>

#define USB_TYPE_VENDOR     0x40
#define USB_DIR_IN          0x80
#define USB_DIR_OUT         0
#define USB_RECIP_DEVICE    0x00
#define USB_ENDPOINT_XFER_BULK 2

#define URB_NO_TRANSFER_DMA_MAP 0x0004

struct urb;

typedef void (*usb_complete_t)(struct urb *);

struct usb_device {
    int dummy;
    void *xnu_priv;         /* mt7601u_usb_ctx (mtkwm/mt7601u/usb.c) */
};

struct usb_interface {
    struct device dev;
    struct usb_device *usb_dev;
};

struct urb {
    int status;
    unsigned int pipe;
    void *transfer_buffer;
    unsigned int transfer_buffer_length;
    unsigned int actual_length;
    void *context;

    /* XNU port extensions */
    usb_complete_t complete;
    unsigned int transfer_flags;
    unsigned int transfer_dma;      /* ignored: no DMA mapping needed */
    struct usb_device *dev;
    void *xnu_priv;                 /* struct mt7601u_urb_ctx * */
};

struct usb_device_id {
    unsigned short idVendor;
    unsigned short idProduct;
};

#define USB_DEVICE(vend, prod)  { .idVendor = (vend), .idProduct = (prod) }

#define to_usb_interface(d) container_of(d, struct usb_interface, dev)
static inline struct usb_device *interface_to_usbdev(struct usb_interface *intf)
{
    return intf->usb_dev;
}

#define usb_rcvbulkpipe(dev, ep)   ((unsigned int)(ep) | USB_DIR_IN)
#define usb_sndbulkpipe(dev, ep)   ((unsigned int)(ep))
#define usb_rcvctrlpipe(dev, ep)   ((unsigned int)(ep) | USB_DIR_IN)
#define usb_sndctrlpipe(dev, ep)   ((unsigned int)(ep))

struct urb *usb_alloc_urb(int iso_packets, gfp_t gfp);
void usb_free_urb(struct urb *urb);
int usb_submit_urb(struct urb *urb, gfp_t gfp);
void usb_kill_urb(struct urb *urb);
void usb_poison_urb(struct urb *urb);

void *usb_alloc_coherent(struct usb_device *dev, size_t size, gfp_t mem_flags,
                         u64 *dma);
void usb_free_coherent(struct usb_device *dev, size_t size, void *addr,
                       u64 dma);

static inline void usb_fill_bulk_urb(struct urb *urb, struct usb_device *dev,
                                     unsigned int pipe, void *buffer,
                                     int length, usb_complete_t complete,
                                     void *context)
{
    urb->dev = dev;
    urb->pipe = pipe;
    urb->transfer_buffer = buffer;
    urb->transfer_buffer_length = length;
    urb->complete = complete;
    urb->context = context;
}

int usb_bulk_msg(struct usb_device *dev, unsigned int pipe, void *data,
                 int len, int *actual_len, int timeout);
int usb_control_msg(struct usb_device *dev, unsigned int pipe,
                    unsigned char request, unsigned char requesttype,
                    unsigned short value, unsigned short index, void *data,
                    unsigned short size, int timeout);
int usb_reset_device(struct usb_device *dev);

#endif
