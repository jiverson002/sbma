Storage Backed Memory Allocation (SBMA) is a dynamic memory allocation library
designed for developing out-of-core computing applications. SBMA uses
interposition to serve as a drop-in replacement for the standard C library
dynamic memory functionality. At a high level, SBMA is a user-space virtual
memory management system. As such, the primary purpose of SBMA is to coordinate
the exchange of memory between secondary storage and main memory. The advantage
of SBMA over the standard operating system virtual memory management is that
processes and threads spawned within an instance of SBMA are treated as
cooperating in order to reduce the amount of thrashing.

Overview
========
At its foundation, SBMA is a user-space virtual memory management (VMM) library.
As such, SBMA has two primary responsibilities: 1) to obtain memory from the
operating system (OS) on behalf of an application and 2) coordinating the
exchange of said memory between main memory and secondary storage when main
memory is over-committed. Unlike a kernel-space VMM library, SBMA is not
responsible for translating virtual addresses to physical addresses. By its very
nature as a user-space library, SBMA is implemented as a layer on top of the OS
VMM. As such, all addresses internal to SBMA are already in their virtual form.
Thus, address translation is left to the OS VMM.

At a higher level, SBMA is simply a dynamic memory allocation library. With the
proper interposition, it can be used unmodified as a drop-in replacement for the
standard C dynamic memory allocation functionality. While SBMA can be used in
any program, its intended purpose is to provide a dynamic memory allocation
library optimized for out-of-core (OOC) applications. Typical OOC applications
require explicit data movement between main memory and secondary storage to
avoid involvement from the OS VMM system. Transforming an in-core algorithm to
be OOC can be time consuming due to irregular data access patterns and
complicated coordination between cooperating processes.

Even for a single process application, a dynamic memory allocation library
optimized for OOC applications can be beneficial. Consider an application which
has mapped a large file into memory. After swapping a sufficient number of pages
into main memory so that it becomes over-committed, the OS VMM will begin
choosing resident pages to evict in order to reclaim space for new pages. If the
application exhibits some degree of spatial locality, then it may be beneficial
to evict a large number of pages which will not be required for some non-trivial
amount of time and load the same number of pages from secondary storage in bulk.
Bulk interaction with secondary storage is often more efficient than small
interactions due to system call overheads and secondary storage latencies. This
bulk exchange behavior is not possible using only the facilities provided by the
Linux OS and is a key characteristic of the SBMA library.

Key ideas
=========
  * It uses mmap to satisfy an allocation request and creates a file that will
be used to persist the data of the associated memory pages when the slave is
blocked.
  * It relies on memory protection and signal handling to determine if the
program is accessing any of the allocated pages and if so, the access mode (read
or write).
  * It saves any pages that have been modified to its associated file when the
slave process blocks due to a communication operation and informs the OS that
the associated address space does not need to be saved in the swap file, and
modifies the memory protection of the associated pages to remove read/write
permissions.
