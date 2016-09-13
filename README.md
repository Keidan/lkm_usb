lkm_usb
===

(GPL) LKM USB.


This module demonstrate how to use the USB device in the kernel space.

This module is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY.

For the moment this module allow:
     - The device driver registering.
     - The creation of the device class (/dev/lkm_usb_classN).
     - The detection of the USB device (add/remove).



Instructions
============


Make targets:

     all: Build the module.
     clean: Clean the generated files.


Download the software :

     mkdir devel
     cd devel
     git clone git://github.com/Keidan/lkm_usb.git
     cd lkm_usb
     make


Insert/Remove the module:

     insmod ./lkm_usb.ko
     rmmod lkm_usb
	


License (like GPL)
==================

	You can:
		- Redistribute the sources code and binaries.
		- Modify the Sources code.
		- Use a part of the sources (less than 50%) in an other software, just write somewhere "lkm_usb is great" visible by the user (on your product or on your website with a link to my page).
		- Redistribute the modification only if you want.
		- Send me the bug-fix (it could be great).
		- Pay me a beer or some other things.
		- Print the source code on WC paper ...
	You can NOT:
		- Earn money with this Software (But I can).
		- Add malware in the Sources.
		- Do something bad with the sources.
		- Use it to travel in the space with a toaster.
	
	I reserve the right to change this licence. If it change the version of the copy you have keep its own license


