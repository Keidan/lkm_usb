/**
*******************************************************************************
* @file lkm_thread.c
* @author Keidan
* @par Project lkm_thread
* @copyright Copyright 2016 Keidan, all right reserved.
* @par License:
* This software is distributed in the hope that it will be useful, but WITHOUT
* ANY WARRANTY.
*
* Licence summary : 
*    You can modify and redistribute the sources code and binaries.
*    You can send me the bug-fix
*
* Term of the licence in in the file licence.txt.
*
* .____     ____  __.  _____     ____ ___  ___________________ 
* |    |   |    |/ _| /     \   |    |   \/   _____/\______   \
* |    |   |      <  /  \ /  \  |    |   /\_____  \  |    |  _/
* |    |___|    |  \/    Y    \ |    |  / /        \ |    |   \
* |_______ \____|__ \____|__  / |______/ /_______  / |______  /
*         \/       \/       \/                   \/         \/ 
*******************************************************************************
*/

#include <linux/kconfig.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kref.h>
#include <linux/usb.h>
#include <asm/uaccess.h>
#include <linux/mutex.h>

#define LKM_USB_CLASS_NAME  "lkm_usb_class"
#define LKM_USB_DRIVER_NAME "lkm_usb_driver"

/* usb_buffer_alloc is renamed to usb_alloc_coherent */
/* #define my_usb_buff_alloc(udev, length, mode, dma) usb_buffer_alloc(udev, length, mode, (dma)) */
#define my_usb_buff_alloc(udev, length, mode, dma) usb_alloc_coherent(udev, length, mode, (dma))
/* usb_buffer_free is renamed to usb_free_coherent() */
/* #define my_usb_buff_free(udev, length, buf, dma) usb_buffer_free(udev, length, buf, dma) */
#define my_usb_buff_free(udev, length, buf, dma) usb_free_coherent(udev, length, buf, dma)

/* table of devices that work with this driver (I have added some USB devices) */
static struct usb_device_id lkm_usb_table [] = {
  { USB_DEVICE_INFO(USB_CLASS_HUB, 0, 0), },
  { USB_DEVICE_INFO(USB_CLASS_HUB, 0, 1), },
  /* HID keyboard, mouse*/
  { USB_DEVICE_INFO(USB_CLASS_HID, 1, 1) },
  { USB_DEVICE_INFO(USB_CLASS_HID, 1, 2) },
  { USB_INTERFACE_INFO(USB_CLASS_HID, 1, 1) },
  { USB_INTERFACE_INFO(USB_CLASS_HID, 1, 2) },
  { USB_INTERFACE_INFO(USB_CLASS_HID, 0, 0) },
  /* USB storage*/
  { USB_DEVICE_INFO(USB_CLASS_MASS_STORAGE, 6, 50) },
  { USB_INTERFACE_INFO(USB_CLASS_MASS_STORAGE, 6, 50) },
  /* Id Product: Voyager Mini, Manufacturer: Corsair -> USB Key */
  { USB_DEVICE(0x1b1c, 0x0b29) },
  /* Id Product: MotoG3, Manufacturer: motorola -> Smartphone*/
  { USB_DEVICE(0x22b8, 0x2e76) },
  { }					/* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, lkm_usb_table);


/* Get a minor range for your devices from the usb maintainer */
#define LKM_USB_MINOR_BASE	0

/* Structure to hold all of our device specific stuff */
struct lkm_usb_s {
    struct usb_device           *udev;                  /* the usb device for this device */
    struct usb_interface        *iface;                 /* the interface for this device */
    __u8                        isopen;	                /* Device is open */
    __u8                        ispresent;              /* Device is present on the bus */
    struct kref                 kref;
};

#define to_lkm_usb_dev(d) container_of(d, struct lkm_usb_s, kref)

/* forward */
static int lkm_usb_probe(struct usb_interface *, const struct usb_device_id *);
static void lkm_usb_disconnect(struct usb_interface *);

/* Global variable to the usb_driver structure */
static struct usb_driver lkm_usb_driver = {
  .name = LKM_USB_DRIVER_NAME,
  .id_table = lkm_usb_table,
  .probe = lkm_usb_probe,
  .disconnect = lkm_usb_disconnect,
};

/**
 * @brief Open function of the file_operations structure
 * @param inode The inode used to retrieve the minor number value.
 * @param file The file structure used to store the dev pointer.
 * @return 0 on success, else the error code.
 */
static int lkm_usb_open(struct inode *inode, struct file *file) {
  struct lkm_usb_s *dev;
  struct usb_interface *iface;


  printk(KERN_INFO "[LKM_USB] USB OPEN\n");
  /* get the minor number of the desired device and  find usb_interface pointer for driver and device */
  iface = usb_find_interface(&lkm_usb_driver, iminor(inode));
  if (!iface) {
    printk(KERN_ALERT "[LKM_USB] Can't find device for minor number %d\n", iminor(inode));
    return -ENODEV;
  }

  /* usb_get_intfdata is usually called in the open function of the USB driver and again in the disconnect function. */
  dev = usb_get_intfdata(iface);
  if (!dev) {
    printk(KERN_ALERT "[LKM_USB] Can't get the dev data\n");
    return -ENODEV;
  }

  if(dev->isopen || !dev->ispresent) {
    return -EBUSY;
  }
  dev->isopen = 1;
  /* increments the usage counter for the device. */
  kref_get(&dev->kref);

  /* save the pointer into the file's private structure */
  file->private_data = dev;

  return 0;
}

/**
 * @brief Function called by the counter decrement and releases the allocated resources.
 * @param kref The reference counter.
 */
static void lkm_usb_ref_delete(struct kref *kref) {	
  struct lkm_usb_s *dev = to_lkm_usb_dev(kref);
  printk(KERN_INFO "[LKM_USB] USB REF DELETE\n");
  /* release a use of the usb device structure */
  usb_put_dev(dev->udev);
  kfree(dev);
}

/**
 * @brief Release function of the file_operations structure
 * @param inode Not used
 * @param file The file structure used to retrieve the dev pointer.
 * @return 0 on success, else the error code.
 */
static int lkm_usb_release(struct inode *inode, struct file *file) {
  struct lkm_usb_s *dev;
  (void)inode; /* warning */
  printk(KERN_INFO "[LKM_USB] USB RELEASE\n");
  /* get the stored pointer (see open function) */
  dev = (struct lkm_usb_s *)file->private_data;
  if (dev == NULL)
    return -ENODEV;

  dev->isopen = 0;
  /* decrement the count on our device */
  kref_put(&dev->kref, lkm_usb_ref_delete);
  return 0;
}

/**
 * @brief Read function of the file_operations structure
 * @param file The file structure used to retrieve the dev pointer.
 * @param buffer The userspace buffer to fill with data .
 * @param length Length of the buffer.
 * @param offset Not used.
 * @return < 0 on error, else the number of bytes read.
 */
static ssize_t lkm_usb_read(struct file *file, char __user *buffer, size_t length, loff_t *offset) {
  (void)file; /* warning */
  (void)buffer; /* warning */
  (void)length; /* warning */
  (void)offset; /* warning */

  printk(KERN_INFO "[LKM_USB] USB READ\n");
  return -ENOSYS;
}

/**
 * @brief Write function of the file_operations structure
 * @param file The file structure used to retrieve the dev pointer.
 * @param user_buffer The userspace buffer.
 * @param length Length of the buffer.
 * @param offset Not used.
 * @return < 0 on error, else the number of bytes written.
 */
static ssize_t lkm_usb_write(struct file *file, const char __user *user_buffer, size_t length, loff_t *offset) {

  (void)file; /* warning */
  (void)user_buffer; /* warning */
  (void)length; /* warning */
  (void)offset; /* warning */

  printk(KERN_INFO "[LKM_USB] USB WRITE\n");
  return -ENOSYS;
}

/*
 * USB file_operations defines
 */
static struct file_operations lkm_usb_fops = {
  .owner =    THIS_MODULE,
  .read =     lkm_usb_read,
  .write =    lkm_usb_write,
  .open =     lkm_usb_open,
  .release =  lkm_usb_release,
};

/* 
 * usb class driver info in order to get a minor number from the usb core,
 * and to have the device registered with devfs and the driver core
 */
static struct usb_class_driver lkm_usb_class = {
  .name = LKM_USB_CLASS_NAME "%d",
  .fops = &lkm_usb_fops,
  /* if CONFIG_USB_DYNAMIC_MINORS is enabled the field below is ignored and all minor numbers for the device are allocated on a first-come, first-served manner.*/
#if IS_ENABLED(CONFIG_USB_DYNAMIC_MINORS)
  .minor_base = 0,
#else
  .minor_base = LKM_USB_MINOR_BASE,
#endif
};

/**
 * @brief Called to see if the driver is willing to manage a particular interface on a device.
 * If it is, probe returns zero and uses usb_set_intfdata to associate driver-specific data with the interface.
 * It may also use usb_set_interface to specify the appropriate altsetting.
 * If unwilling to manage the interface, return -ENODEV, if genuine IO errors occurred, an appropriate negative errno value. 
 * @param iface The current USB interface.
 * @param id Not used.
 * @return 0 on success, else the error code.
 */
static int lkm_usb_probe(struct usb_interface *iface, const struct usb_device_id *id) {
  struct lkm_usb_s *dev = NULL;
  struct usb_host_interface *iface_desc;
  struct usb_endpoint_descriptor *endpoint;
  int i, retval = -ENOMEM, type, dir_in;
  
  /* (void)id;*/ /* warning */

  printk(KERN_INFO "[LKM_USB] -----------------\n");
  printk(KERN_INFO "[LKM_USB] USB PROBE\n");
  printk(KERN_INFO "[LKM_USB] PRODUCT SPECIFICS\n");
  printk(KERN_INFO "[LKM_USB] -----------------\n");
  printk(KERN_INFO "[LKM_USB] idVendor:            %#04x\n", id->idVendor);
  printk(KERN_INFO "[LKM_USB] idProduct:           %#04x\n", id->idProduct);
  printk(KERN_INFO "[LKM_USB] bcdDevice_lo:        %hu\n", id->bcdDevice_lo);
  printk(KERN_INFO "[LKM_USB] bcdDevice_hi:        %hu\n", id->bcdDevice_hi);
  printk(KERN_INFO "[LKM_USB] DEVICE CLASS INFO\n");
  printk(KERN_INFO "[LKM_USB] -----------------");
  printk(KERN_INFO "[LKM_USB] bDeviceClass:        %#02x\n", id->bDeviceClass);
  printk(KERN_INFO "[LKM_USB] bDeviceSubClass:     %#02x\n", id->bDeviceSubClass);
  printk(KERN_INFO "[LKM_USB] bDeviceProtocol:     %#02x\n", id->bDeviceProtocol);
  printk(KERN_INFO "[LKM_USB] INTERFACE CLASS INFO\n");
  printk(KERN_INFO "[LKM_USB] -----------------\n");
  printk(KERN_INFO "[LKM_USB] bInterfaceClass:     %x\n", id->bInterfaceClass);
  printk(KERN_INFO "[LKM_USB] bInterfaceSubClass:  %x\n", id->bInterfaceSubClass);
  printk(KERN_INFO "[LKM_USB] bInterfaceProtocol:  %x\n", id->bInterfaceProtocol);
  printk(KERN_INFO "[LKM_USB] VENDOR-SPECIFIC INFO\n");
  printk(KERN_INFO "[LKM_USB] -----------------\n");
  printk(KERN_INFO "[LKM_USB] bInterfaceNumber:    %x\n", id->bInterfaceNumber);
  printk(KERN_INFO "[LKM_USB] USB_INTERFACE STRUCT\n");
  printk(KERN_INFO "[LKM_USB] Operational Info\n");
  printk(KERN_INFO "[LKM_USB] ----------------\n");
  printk(KERN_INFO "[LKM_USB] minor:               %d\n", iface->minor);
  printk(KERN_INFO "[LKM_USB] condition:           %d\n", iface->condition);
  printk(KERN_INFO "[LKM_USB] sysfs_files_created: %u\n", iface->sysfs_files_created);
  printk(KERN_INFO "[LKM_USB] ep_devs_created:     %u\n", iface->ep_devs_created);
  printk(KERN_INFO "[LKM_USB] unregistering:       %u\n", iface->unregistering);
  printk(KERN_INFO "[LKM_USB] needs_remote_wakeup: %u\n", iface->needs_remote_wakeup);
  printk(KERN_INFO "[LKM_USB] needs_altsetting0:   %u\n", iface->needs_altsetting0);
  printk(KERN_INFO "[LKM_USB] needs_binding:       %u\n", iface->needs_binding);
  printk(KERN_INFO "[LKM_USB] resetting_device:    %u\n", iface->resetting_device);
  printk(KERN_INFO "[LKM_USB] Driver Model's View of the device\n");
  printk(KERN_INFO "[LKM_USB] ---------------------------------\n");
  /* Parent usually bus or host controller */
  printk(KERN_INFO "[LKM_USB] parent:              %p\n", iface->dev.parent);
  /* Storage space utilized by the USB core */
  printk(KERN_INFO "[LKM_USB] device_private:      %p\n", iface->dev.p);
  printk(KERN_INFO "[LKM_USB] kobj.name:           %s\n", (iface->dev).kobj.name);
  printk(KERN_INFO "[LKM_USB] init_name:           %s\n", (iface->dev).init_name);
  printk(KERN_INFO "[LKM_USB] device_type.name:    %s\n", ((iface->dev).type)->name);
  printk(KERN_INFO "[LKM_USB] bus_type.name:       %s\n", ((iface->dev).bus)->name);
  printk(KERN_INFO "[LKM_USB] bus_type.dev_name:   %s\n", ((iface->dev).bus)->dev_name);
  printk(KERN_INFO "[LKM_USB] driver:              %p\n", (iface->dev).driver);

  if(iface->dev.driver) {
    printk(KERN_INFO "[LKM_USB] Device Driver Info\n");
    printk(KERN_INFO "[LKM_USB] ------------------\n");
    printk(KERN_INFO "[LKM_USB] name:                %s\n", ((iface->dev).driver)->name);
    printk(KERN_INFO "[LKM_USB] bus_type.name:       %s\n", (((iface->dev).driver)->bus)->name);
    printk(KERN_INFO "[LKM_USB] bus_type.dev_name:   %s\n", (((iface->dev).driver)->bus)->dev_name);
    printk(KERN_INFO "[LKM_USB] owner:               %p\n", ((iface->dev).driver)->owner);
    if(((iface->dev).driver)->mod_name)
      printk(KERN_INFO "[LKM_USB] owner.name           %s\n", (((iface->dev).driver)->owner)->name);
    printk(KERN_INFO "[LKM_USB] mod_name:            %s\n", ((iface->dev).driver)->mod_name);
  }


  /* allocate memory for the device state and initialize it */
  dev = kzalloc(sizeof(struct lkm_usb_s), GFP_KERNEL);
  if (dev == NULL) {
    printk(KERN_ALERT "[LKM_USB] Unable to allocate memory for the USB device structure.\n");
    return -ENOMEM;
  }
  kref_init(&dev->kref);
  dev->udev = usb_get_dev(interface_to_usbdev(iface));
  dev->iface = iface;

  /* set up the endpoint information */
  /* use only the first bulk-in and bulk-out endpoints */
  iface_desc = iface->cur_altsetting;
  for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
    endpoint = &iface_desc->endpoint[i].desc;
    type = (endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK);
    dir_in = (endpoint->bEndpointAddress & USB_DIR_IN) ? 1 : 0;

    printk(KERN_INFO "[LKM_USB] Endpoint descriptor\n");
    printk(KERN_INFO "[LKM_USB] ------------------\n");
    printk(KERN_INFO "[LKM_USB] bLength:             %d\n", endpoint->bLength);
    printk(KERN_INFO "[LKM_USB] bDescriptorType:     %d\n", endpoint->bDescriptorType);
    printk(KERN_INFO "[LKM_USB] bEndpointAddress:    %#02x\n", endpoint->bEndpointAddress);
    printk(KERN_INFO "[LKM_USB] bmAttributes:        %d\n", endpoint->bmAttributes);
    printk(KERN_INFO "[LKM_USB] wMaxPacketSize:      %d\n", endpoint->wMaxPacketSize);
    printk(KERN_INFO "[LKM_USB] bInterval:           %d\n", endpoint->bInterval);
    printk(KERN_INFO "[LKM_USB] direction:           %s\n", dir_in ? "INPUT" : "OUTPUT");
    printk(KERN_INFO "[LKM_USB] type:                %s\n", ((type == USB_ENDPOINT_XFER_CONTROL) ? "CONTROL" : (type == USB_ENDPOINT_XFER_ISOC) ? "ISOC" : (type == USB_ENDPOINT_XFER_BULK) ? "BULK" : "INT"));
    printk(KERN_INFO "[LKM_USB] ------------------\n");

  }
  /* save the pointer in this interface device */
  usb_set_intfdata(iface, dev);

  /* we can register the device now, as it is ready */
  retval = usb_register_dev(iface, &lkm_usb_class);
  if (retval) {
    /* something prevented us from registering this driver */
    printk(KERN_ALERT "[LKM_USB] Not able to get a minor for this device.\n");
    usb_set_intfdata(iface, NULL);
    kref_put(&dev->kref, lkm_usb_ref_delete);
    return retval;
  }

  /* let the user know what node this device is now attached to */
  printk(KERN_INFO "[LKM_USB] USB device now attached to /dev/" LKM_USB_CLASS_NAME "%d\n", iface->minor);

  printk(KERN_INFO "[LKM_USB] ------------------\n");
  dev->ispresent = 1;
  return 0;
}

/**
 * @brief Called when the interface is no longer accessible, usually because its device has been (or is being)
 * disconnected or the driver module is being unloaded. 
 * @param iface The current USB interface.
 */
static void lkm_usb_disconnect(struct usb_interface *iface) {
  struct lkm_usb_s *dev;
  int minor = iface->minor;

  printk(KERN_INFO "[LKM_USB] USB DISCONNECT\n");
  dev = usb_get_intfdata(iface);
  usb_set_intfdata(iface, NULL);
  if(dev) {
    /* unregister the usb device */
    usb_deregister_dev(iface, &lkm_usb_class);
    dev->isopen = 0;
    dev->ispresent = 0;
    /* decrement our usage count */
    kref_put(&dev->kref, lkm_usb_ref_delete);
  }
  printk(KERN_INFO "[LKM_USB] USB number #%d is now diconnected\n", minor);
}

/**
 * @brief Hook function called by the kernel.
 * @param self Not used.
 * @param action used to determine the current action.
 * @param dev Not used.
 * @return NOTIFY_OK
 */
static int usb_nfy_hook_fct(struct notifier_block *self, unsigned long action, void *dev) {
  printk(KERN_INFO "[LKM_USB] ## usb_nfy_hook_fct called\n");
  switch (action) {
    case USB_DEVICE_ADD:
      printk(KERN_INFO "[LKM_USB] #USB device added\n");
      break;
    case USB_DEVICE_REMOVE:
      printk(KERN_INFO "[LKM_USB] #USB device removed\n");
      break;
    case USB_BUS_ADD:
      printk(KERN_INFO "[LKM_USB] #USB Bus added\n");
      break;
    case USB_BUS_REMOVE:
      printk(KERN_INFO "[LKM_USB] #USB Bus removed\n");
  }
  return NOTIFY_OK;
}

static struct notifier_block usb_nfy_hook = {
  .notifier_call = usb_nfy_hook_fct,
};

/**
 * @brief The LKM initialization function - register the USB device.
 * @return returns 0 if successful
 */
static int __init lkm_usb_init(void) {
  int result;
  /* register this driver with the USB subsystem */
  result = usb_register(&lkm_usb_driver);
  if (result)
    printk(KERN_ALERT "[LKM_USB] usb_register failed. Error number %d\n", result);
  /* Hook to the USB core to get notification on any addition or removal of USB devices */
  usb_register_notify(&usb_nfy_hook);  
  printk(KERN_INFO "[LKM_USB] Module loaded\n");
  return result;
}

/**
 * @brief The LKM cleanup function.
 */
static void __exit lkm_usb_exit(void) {  
  /* Remove the hook */
  usb_unregister_notify(&usb_nfy_hook);
  /* deregister this driver with the USB subsystem */
  usb_deregister(&lkm_usb_driver);
  printk(KERN_INFO "[LKM_USB] Module unloaded\n");
}

/****************************************************
 *    _____             .___    .__           .__       .__  __   
 *   /     \   ____   __| _/_ __|  |   ____   |__| ____ |__|/  |_ 
 *  /  \ /  \ /  _ \ / __ |  |  \  | _/ __ \  |  |/    \|  \   __\
 * /    Y    (  <_> ) /_/ |  |  /  |_\  ___/  |  |   |  \  ||  |  
 * \____|__  /\____/\____ |____/|____/\___  > |__|___|  /__||__|  
 *         \/            \/               \/          \/    
 *****************************************************/
/** 
 * @brief A module must use the module_init() module_exit() macros from linux/init.h, which 
 * identify the initialization function at insertion time and the cleanup function (as listed above)
 */
module_init(lkm_usb_init);
module_exit(lkm_usb_exit);

/* module infos */
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0");
MODULE_AUTHOR("Keidan (Kevin Billonneau)");
MODULE_DESCRIPTION("Simple LKM USB howto.");
