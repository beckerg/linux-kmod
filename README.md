# skel-xx
Simple kernel loadable module

The following code implements a skeleton character driver for Linux
built for the purpose of exploring the linux kernel and developing
code that runs in the kernel environment.

In its base configuration it presents a handful of character devices
(e.g., /dev/xx1, /dev/xx2, ...) that provide read, write, and mmap
interfaces to what is effectively an unbounded in-kernel RAM buffer.
(Not really a RAM disk as it does not implement partitioning nor
and of the disk ioctls).

Quick instructions to get going:

* cd skel-xx/linux
* make
* sudo make load
* dd if=/dev/urandom of=/dev/xx1 count=8192

Be careful with the size you give dd in step 4 as this driver
will happily use up all the memory in your machine.
