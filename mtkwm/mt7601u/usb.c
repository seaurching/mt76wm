// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2015 Jakub Kicinski <kubakici@wp.pl>
 *
 * usb.c - MT7601U USB transport for mt76wm (XNU).  The Linux usb.c is
 * re-hosted on IOUSBHostFamily: vendor control requests map to
 * IOUSBHostDevice::deviceRequest, bulk traffic to IOUSBHostPipe::io, and
 * the Linux URB model (alloc/submit/complete/kill/poison) is emulated on
 * top of IOUSBHostCompletion.  Register access (mt7601u_rr/wr/...) is
 * kept as in Linux: MMIO registers are reached through USB vendor
 * requests.
 */

#include <IOKit/IOLib.h>
#include <IOKit/usb/IOUSBHostDevice.h>
#include <IOKit/usb/IOUSBHostInterface.h>
#include <IOKit/usb/IOUSBHostPipe.h>

#include "mt7601u.h"
#include "usb.h"
#include "trace.h"

/* ================================================================== */
/* XNU transport state                                                 */
/* ================================================================== */

struct mt7601u_usb_ctx {
    IOUSBHostDevice    *device;
    IOUSBHostInterface *intf;
    IOService          *client;   /* owner of the open session          */
    IOUSBHostPipe      *pipes[256];   /* by endpoint address (retained) */
    bool                detached;
};

struct mt7601u_urb_ctx {
    struct urb            *urb;
    IOMemoryDescriptor    *md;
    IOUSBHostCompletion    cmpl;
    IOUSBHostPipe         *pipe;      /* retained while armed           */
    volatile bool          in_flight;
    bool                   poisoned;
};

static int xnu_err(IOReturn ret)
{
    switch (ret) {
    case kIOReturnSuccess:
        return 0;
    case kIOReturnNoDevice:
    case kIOReturnNotAttached:
    case kIOReturnOffline:
        return -ENODEV;
    case kIOReturnTimeout:
        return -ETIMEDOUT;
    case kIOReturnAborted:
    case kIOReturnUnderrun:
        return -ECONNRESET;
    case kIOReturnNoMemory:
        return -ENOMEM;
    case kIOReturnBadArgument:
        return -EINVAL;
    default:
        return -EIO;
    }
}

static struct mt7601u_usb_ctx *usb_ctx(struct usb_device *dev)
{
    return dev ? (struct mt7601u_usb_ctx *)dev->xnu_priv : NULL;
}

static IOUSBHostPipe *pipe_for(struct usb_device *dev, unsigned int pipe)
{
    struct mt7601u_usb_ctx *ctx = usb_ctx(dev);

    if (!ctx || ctx->detached)
        return NULL;
    return ctx->pipes[pipe & 0xff];
}

/* ================================================================== */
/* URB emulation                                                       */
/* ================================================================== */

static void mt7601u_urb_done(void *owner, void *parameter, IOReturn status,
                             uint32_t bytesTransferred)
{
    struct mt7601u_urb_ctx *ctx = (struct mt7601u_urb_ctx *)owner;
    struct urb *urb = ctx->urb;

    (void)parameter;
    if (ctx->md) {
        ctx->md->release();
        ctx->md = NULL;
    }
    urb->status = (status == kIOReturnSuccess) ? 0 : xnu_err(status);
    urb->actual_length = bytesTransferred;
    ctx->in_flight = false;
    if (urb->complete)
        urb->complete(urb);
}

struct urb *usb_alloc_urb(int iso_packets, gfp_t gfp)
{
    struct mt7601u_urb_ctx *ctx;
    struct urb *urb;

    (void)iso_packets;
    (void)gfp;
    urb = (struct urb *)kzalloc(sizeof(*urb), 0);
    if (!urb)
        return NULL;
    ctx = (struct mt7601u_urb_ctx *)kzalloc(sizeof(*ctx), 0);
    if (!ctx) {
        kfree(urb);
        return NULL;
    }
    ctx->urb = urb;
    urb->xnu_priv = ctx;
    return urb;
}

void usb_free_urb(struct urb *urb)
{
    struct mt7601u_urb_ctx *ctx;

    if (!urb)
        return;
    ctx = (struct mt7601u_urb_ctx *)urb->xnu_priv;
    if (ctx) {
        if (ctx->md)
            ctx->md->release();
        if (ctx->pipe)
            ctx->pipe->release();
        kfree(ctx);
    }
    kfree(urb);
}

int usb_submit_urb(struct urb *urb, gfp_t gfp)
{
    struct mt7601u_urb_ctx *ctx;
    IOUSBHostPipe *pipe;
    IODirection dir;
    IOReturn ret;

    (void)gfp;
    if (!urb || !urb->transfer_buffer)
        return -EINVAL;
    ctx = (struct mt7601u_urb_ctx *)urb->xnu_priv;
    if (!ctx)
        return -EINVAL;
    if (ctx->poisoned)
        return -EPERM;
    if (ctx->in_flight)
        return -EBUSY;

    pipe = pipe_for(urb->dev, urb->pipe);
    if (!pipe)
        return -ENODEV;

    dir = (urb->pipe & USB_DIR_IN) ? kIODirectionIn : kIODirectionOut;
    ctx->md = IOMemoryDescriptor::withAddress(urb->transfer_buffer,
                                              urb->transfer_buffer_length,
                                              dir);
    if (!ctx->md)
        return -ENOMEM;

    if (ctx->pipe)
        ctx->pipe->release();
    ctx->pipe = pipe;
    ctx->pipe->retain();

    ctx->cmpl.action = mt7601u_urb_done;
    ctx->cmpl.owner = ctx;
    ctx->cmpl.parameter = NULL;
    ctx->in_flight = true;

    ret = pipe->io(ctx->md, urb->transfer_buffer_length, &ctx->cmpl, 0);
    if (ret != kIOReturnSuccess) {
        ctx->in_flight = false;
        ctx->md->release();
        ctx->md = NULL;
        return xnu_err(ret);
    }
    return 0;
}

static void mt7601u_urb_abort(struct urb *urb, bool poison)
{
    struct mt7601u_urb_ctx *ctx;
    int i;

    if (!urb)
        return;
    ctx = (struct mt7601u_urb_ctx *)urb->xnu_priv;
    if (!ctx)
        return;
    if (poison)
        ctx->poisoned = true;

    if (ctx->in_flight && ctx->pipe) {
        ctx->pipe->abort(IOUSBHostIOSource::kAbortSynchronous);
        /* wait for the completion handler to run */
        for (i = 0; i < 1000 && ctx->in_flight; i++)
            IOSleep(1);
    }
}

void usb_kill_urb(struct urb *urb)
{
    mt7601u_urb_abort(urb, false);
}

void usb_poison_urb(struct urb *urb)
{
    mt7601u_urb_abort(urb, true);
}

void *usb_alloc_coherent(struct usb_device *dev, size_t size, gfp_t mem_flags,
                         u64 *dma)
{
    (void)dev;
    (void)mem_flags;
    if (dma)
        *dma = 0;   /* no DMA mapping: IOMemoryDescriptor handles it */
    return kzalloc(size, 0);
}

void usb_free_coherent(struct usb_device *dev, size_t size, void *addr,
                       u64 dma)
{
    (void)dev;
    (void)size;
    (void)dma;
    if (addr)
        kfree(addr);
}

/* ================================================================== */
/* Control / synchronous bulk                                          */
/* ================================================================== */

int usb_control_msg(struct usb_device *dev, unsigned int pipe,
                    unsigned char request, unsigned char requesttype,
                    unsigned short value, unsigned short index, void *data,
                    unsigned short size, int timeout)
{
    struct mt7601u_usb_ctx *ctx = usb_ctx(dev);
    StandardUSB::DeviceRequest req;
    uint32_t transferred = 0;
    IOReturn ret;

    (void)pipe;
    if (!ctx || ctx->detached || !ctx->device)
        return -ENODEV;

    memset(&req, 0, sizeof(req));
    req.bmRequestType = requesttype;
    req.bRequest = request;
    req.wValue = value;
    req.wIndex = index;
    req.wLength = size;

    ret = ctx->device->deviceRequest(ctx->client, req, data, transferred,
                                     timeout > 0 ? (uint32_t)timeout : 300);
    if (ret != kIOReturnSuccess)
        return xnu_err(ret);
    return (int)transferred;
}

int usb_bulk_msg(struct usb_device *dev, unsigned int pipe, void *data,
                 int len, int *actual_len, int timeout)
{
    IOUSBHostPipe *p;
    IOMemoryDescriptor *md;
    uint32_t transferred = 0;
    IOReturn ret;

    if (len < 0)
        return -EINVAL;
    p = pipe_for(dev, pipe);
    if (!p)
        return -ENODEV;
    md = IOMemoryDescriptor::withAddress(data, len,
            (pipe & USB_DIR_IN) ? kIODirectionIn : kIODirectionOut);
    if (!md)
        return -ENOMEM;

    ret = p->io(md, (uint32_t)len, transferred,
                timeout > 0 ? (uint32_t)timeout : 500);
    md->release();
    if (actual_len)
        *actual_len = (int)transferred;
    return ret == kIOReturnSuccess ? 0 : xnu_err(ret);
}

int usb_reset_device(struct usb_device *dev)
{
    /* IOUSBHostDevice keeps the configuration; nothing to do here.
     * TODO: implement a real reset if bring-up needs it. */
    (void)dev;
    return 0;
}

/* ================================================================== */
/* Attach / detach                                                     */
/* ================================================================== */

int mt7601u_usb_attach(void *io_usb_device, void *io_usb_interface,
                       void *io_client, struct mt7601u_dev *dev)
{
    IOUSBHostDevice *device = (IOUSBHostDevice *)io_usb_device;
    IOUSBHostInterface *intf = (IOUSBHostInterface *)io_usb_interface;
    IOService *client = (IOService *)io_client;
    const StandardUSB::ConfigurationDescriptor *confDesc;
    const StandardUSB::InterfaceDescriptor *intfDesc;
    const StandardUSB::Descriptor *epDesc = NULL;
    struct mt7601u_usb_ctx *ctx;
    struct usb_device *usb_dev;
    unsigned int ep_i = 0, ep_o = 0;

    if (!device || !intf)
        return -EINVAL;

    ctx = (struct mt7601u_usb_ctx *)kzalloc(sizeof(*ctx), 0);
    usb_dev = (struct usb_device *)kzalloc(sizeof(*usb_dev), 0);
    if (!ctx || !usb_dev) {
        kfree(ctx);
        kfree(usb_dev);
        return -ENOMEM;
    }
    ctx->device = device;
    ctx->intf = intf;
    ctx->client = client;
    usb_dev->xnu_priv = ctx;
    dev->usb_dev = usb_dev;

    confDesc = device->getConfigurationDescriptor();
    intfDesc = intf->getInterfaceDescriptor();
    if (!confDesc || !intfDesc) {
        mt7601u_usb_detach(dev);
        return -EINVAL;
    }

    while ((epDesc = StandardUSB::getNextAssociatedDescriptorWithType(
                confDesc, intfDesc, epDesc,
                kDescriptorTypeEndpoint)) != NULL) {
        const StandardUSB::EndpointDescriptor *ep =
            (const StandardUSB::EndpointDescriptor *)epDesc;
        uint8_t addr = StandardUSB::getEndpointAddress(ep);
        IOUSBHostPipe *pipe;

        if ((ep->bmAttributes & 0x03) != USB_ENDPOINT_XFER_BULK)
            continue;

        if (addr & USB_DIR_IN) {
            if (ep_i >= __MT_EP_IN_MAX)
                continue;
            dev->in_eps[ep_i++] = addr;
            dev->in_max_packet = ep->wMaxPacketSize;
        } else {
            if (ep_o >= __MT_EP_OUT_MAX)
                continue;
            dev->out_eps[ep_o++] = addr;
            dev->out_max_packet = ep->wMaxPacketSize;
        }

        pipe = intf->copyPipe(addr);
        if (pipe)
            ctx->pipes[addr] = pipe;    /* keep the reference */
    }

    if (ep_i != __MT_EP_IN_MAX || ep_o != __MT_EP_OUT_MAX) {
        dev_err(dev->dev, "Error: wrong pipe number in:%d out:%d\n",
            ep_i, ep_o);
        mt7601u_usb_detach(dev);
        return -EINVAL;
    }

    return 0;
}

void mt7601u_usb_detach(struct mt7601u_dev *dev)
{
    struct usb_device *usb_dev = dev->usb_dev;
    struct mt7601u_usb_ctx *ctx;
    int i;

    if (!usb_dev)
        return;
    ctx = usb_ctx(usb_dev);
    if (ctx) {
        ctx->detached = true;
        for (i = 0; i < 256; i++) {
            if (ctx->pipes[i]) {
                ctx->pipes[i]->abort(IOUSBHostIOSource::kAbortSynchronous);
                ctx->pipes[i]->release();
                ctx->pipes[i] = NULL;
            }
        }
        kfree(ctx);
    }
    kfree(usb_dev);
    dev->usb_dev = NULL;
}

/* ================================================================== */
/* Vendor requests + register access (Linux usb.c, mostly verbatim)    */
/* ================================================================== */

bool mt7601u_usb_alloc_buf(struct mt7601u_dev *dev, size_t len,
                           struct mt7601u_dma_buf *buf)
{
    struct usb_device *usb_dev = mt7601u_to_usb_dev(dev);

    buf->len = len;
    buf->urb = usb_alloc_urb(0, GFP_KERNEL);
    buf->buf = usb_alloc_coherent(usb_dev, buf->len, GFP_KERNEL, &buf->dma);

    return !buf->urb || !buf->buf;
}

void mt7601u_usb_free_buf(struct mt7601u_dev *dev, struct mt7601u_dma_buf *buf)
{
    struct usb_device *usb_dev = mt7601u_to_usb_dev(dev);

    usb_free_coherent(usb_dev, buf->len, buf->buf, buf->dma);
    usb_free_urb(buf->urb);
}

int mt7601u_usb_submit_buf(struct mt7601u_dev *dev, int dir, int ep_idx,
                           struct mt7601u_dma_buf *buf, gfp_t gfp,
                           usb_complete_t complete_fn, void *context)
{
    struct usb_device *usb_dev = mt7601u_to_usb_dev(dev);
    unsigned pipe;
    int ret;

    if (dir == USB_DIR_IN)
        pipe = usb_rcvbulkpipe(usb_dev, dev->in_eps[ep_idx]);
    else
        pipe = usb_sndbulkpipe(usb_dev, dev->out_eps[ep_idx]);

    usb_fill_bulk_urb(buf->urb, usb_dev, pipe, buf->buf, buf->len,
                      complete_fn, context);
    buf->urb->transfer_dma = buf->dma;
    buf->urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

    trace_mt_submit_urb(dev, buf->urb);
    ret = usb_submit_urb(buf->urb, gfp);
    if (ret)
        dev_err(dev->dev, "Error: submit URB dir:%d ep:%d failed:%d\n",
            dir, ep_idx, ret);
    return ret;
}

void mt7601u_complete_urb(struct urb *urb)
{
    struct completion *cmpl = (struct completion *)urb->context;

    complete(cmpl);
}

int mt7601u_vendor_request(struct mt7601u_dev *dev, const u8 req,
                           const u8 direction, const u16 val, const u16 offset,
                           void *buf, const size_t buflen)
{
    int i, ret;
    struct usb_device *usb_dev = mt7601u_to_usb_dev(dev);
    const u8 req_type = direction | USB_TYPE_VENDOR | USB_RECIP_DEVICE;
    const unsigned int pipe = (direction == USB_DIR_IN) ?
        usb_rcvctrlpipe(usb_dev, 0) : usb_sndctrlpipe(usb_dev, 0);

    for (i = 0; i < MT_VEND_REQ_MAX_RETRY; i++) {
        ret = usb_control_msg(usb_dev, pipe, req, req_type,
                              val, offset, buf, buflen,
                              MT_VEND_REQ_TOUT_MS);
        trace_mt_vend_req(dev, pipe, req, req_type, val, offset,
                  buf, buflen, ret);

        if (ret == -ENODEV)
            set_bit(MT7601U_STATE_REMOVED, &dev->state);
        if (ret >= 0 || ret == -ENODEV)
            return ret;

        msleep(5);
    }

    dev_err(dev->dev, "Vendor request req:%02x off:%04x failed:%d\n",
        req, offset, ret);

    return ret;
}

void mt7601u_vendor_reset(struct mt7601u_dev *dev)
{
    mt7601u_vendor_request(dev, MT_VEND_DEV_MODE, USB_DIR_OUT,
                   MT_VEND_DEV_MODE_RESET, 0, NULL, 0);
}

/* should be called with vendor_req_mutex held */
static u32 __mt7601u_rr(struct mt7601u_dev *dev, u32 offset)
{
    int ret;
    u32 val = ~0;

    WARN_ONCE(offset > USHRT_MAX, "read high off:%08x", offset);

    ret = mt7601u_vendor_request(dev, MT_VEND_MULTI_READ, USB_DIR_IN,
                     0, offset, dev->vend_buf, MT_VEND_BUF);
    if (ret == MT_VEND_BUF)
        val = get_unaligned_le32(dev->vend_buf);
    else if (ret > 0)
        dev_err(dev->dev, "Error: wrong size read:%d off:%08x\n",
            ret, offset);

    trace_reg_read(dev, offset, val);
    return val;
}

u32 mt7601u_rr(struct mt7601u_dev *dev, u32 offset)
{
    u32 ret;

    mutex_lock(&dev->vendor_req_mutex);
    ret = __mt7601u_rr(dev, offset);
    mutex_unlock(&dev->vendor_req_mutex);

    return ret;
}

/* should be called with vendor_req_mutex held */
static int __mt7601u_vendor_single_wr(struct mt7601u_dev *dev, const u8 req,
                      const u16 offset, const u32 val)
{
    int ret = mt7601u_vendor_request(dev, req, USB_DIR_OUT,
                     val & 0xffff, offset, NULL, 0);
    if (!ret)
        ret = mt7601u_vendor_request(dev, req, USB_DIR_OUT,
                         val >> 16, offset + 2, NULL, 0);
    trace_reg_write(dev, offset, val);
    return ret;
}

int mt7601u_vendor_single_wr(struct mt7601u_dev *dev, const u8 req,
                 const u16 offset, const u32 val)
{
    int ret;

    mutex_lock(&dev->vendor_req_mutex);
    ret = __mt7601u_vendor_single_wr(dev, req, offset, val);
    mutex_unlock(&dev->vendor_req_mutex);

    return ret;
}

void mt7601u_wr(struct mt7601u_dev *dev, u32 offset, u32 val)
{
    WARN_ONCE(offset > USHRT_MAX, "write high off:%08x", offset);

    mt7601u_vendor_single_wr(dev, MT_VEND_WRITE, offset, val);
}

u32 mt7601u_rmw(struct mt7601u_dev *dev, u32 offset, u32 mask, u32 val)
{
    mutex_lock(&dev->vendor_req_mutex);
    val |= __mt7601u_rr(dev, offset) & ~mask;
    __mt7601u_vendor_single_wr(dev, MT_VEND_WRITE, offset, val);
    mutex_unlock(&dev->vendor_req_mutex);

    return val;
}

u32 mt7601u_rmc(struct mt7601u_dev *dev, u32 offset, u32 mask, u32 val)
{
    u32 reg;

    mutex_lock(&dev->vendor_req_mutex);
    reg = __mt7601u_rr(dev, offset);
    val |= reg & ~mask;
    if (reg != val)
        __mt7601u_vendor_single_wr(dev, MT_VEND_WRITE,
                       offset, val);
    mutex_unlock(&dev->vendor_req_mutex);

    return val;
}

void mt7601u_wr_copy(struct mt7601u_dev *dev, u32 offset,
                     const void *data, int len)
{
    WARN_ONCE(offset & 3, "unaligned write copy off:%08x", offset);
    WARN_ONCE(len & 3, "short write copy off:%08x", offset);

    mt7601u_burst_write_regs(dev, offset, (const u32 *)data, len / 4);
}

void mt7601u_addr_wr(struct mt7601u_dev *dev, const u32 offset, const u8 *addr)
{
    mt7601u_wr(dev, offset, get_unaligned_le32(addr));
    mt7601u_wr(dev, offset + 4, addr[4] | addr[5] << 8);
}
