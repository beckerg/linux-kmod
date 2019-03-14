# skel-xx
Simple kernel loadable module

The following code implements a skeleton character driver for Linux
built for the purpose of exploring the linux kernel and developing
code that runs in the kernel environment.

In its base configuration it presents a handful of character devices
(e.g., /dev/xx1, /dev/xx2, ...) that provide read, write, and mmap
interfaces to what is effectively an unbounded in-kernel RAM buffer.
(Not really a RAM disk as it does not implement partitioning nor
any of the disk ioctls).

Quick instructions to get going:

* make
* sudo make load
* sudo make check
* sudo dd if=/dev/urandom of=/dev/xx1 count=32768 bs=4096
* sudo make unload
* make clean

For kernel source versions < 4.18 you may need to tweak the
HAVE_* defines in xx.c depending upon if your struct vm_fault
has the vma field and whether or not you have a definition
for vm_fault_t.

Be careful with the size you give dd in step 4 as this driver
will happily consume all the RAM in your machine.
