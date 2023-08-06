# SUSS: Improving TCP Performance by Speeding Up Slow Start


## Table of Contents
1. [Overview](#overview)
2. [List of Modifications in the Linux Source Code](#list-of-modifications-in-the-linux-source-code)
3. [Installation Roadmap](#installation-roadmap)
4. [Working with SUSS](#working-with-suss)
5. [Contact Us](#contact-us)
6. [References](#references)

### Overview
Welcome to SUSS (Speeding Up Slow Start), an open-source project aimed at tackling the issue of bandwidth under-utilization during the TCP slow-start phase. Our lightweight sender-side add-on, compatible with CUBIC [1] and implemented in Linux kernel 5.19.10, focuses on reducing flow completion time (FCT), a vital performance metric for the Internet end-users [2].
With SUSS, users simply need to apply the changes to the Linux kernel's source code and recompile it for seamless integration. This README includes an installation roadmap and excerpts from a more comprehensive academic paper that delve into detailed information about SUSS, covering its theory and performance analysis. For the full in-depth analysis, please refer to the academic paper available at [will_be_published](http://will_be_published/).
We value community contributions and will share the contributing guidelines upon successful paper acceptance. Join us in optimizing TCP connections for faster, more efficient data transfer.


### List of Modifications in the Linux Source Code
SUSS introduces multiple modifications to the Linux TCP source code. <b>We also added lines of code for log and performance tracking purposes, which are not part of the final product.</b> The original Linux files can be found in the `src/linux-5.19.10` directory, while the altered versions are stored in the `src/suss` directory within the project.
These changes are as follows:

- a) In the file `tcp.h`, SUSS defines a set of global variables within the `struct tcp_sock`.
- b) The slow-start mechanism of CUBIC has been altered by SUSS in the file `tcp_cubic.c`.
- c) SUSS has made a small modification to the file `tcp_output.c` to enable data transmission during the pacing period.
- d) In the file `tcp_input.c`, which deals with incoming acknowledgments (ACKs), SUSS adds a few lines of code.
- e) To assign a random ID for each test, a few lines of code have been added to the file `tcp_cong.c`.

You can identify SUSS's specific modifications in the Linux source code by searching for the "/* suss" comments in the files located in the `src` directory.


### Installation Roadmap
SUSS is currently implemented on Ubuntu.
The installation process in the Linux command-line interface (CLI) involves the following steps:
1. Download the kernel source code:
   <pre>
	   wget https://mirrors.edge.kernel.org/pub/linux/kernel/v5.x/linux-5.19.10.tar.xz
   </pre>
  
2. Extract the contents of a tarball archive named "linux-5.19.10.tar.xz.":
   <pre>
	   tar xvf linux-5.19.10.tar.xz
   </pre>   
3. Update the local package index and Install necessary build tools:
   <pre>
	   sudo apt-get update
	   sudo apt-get install build-essential
   </pre>
4. Configure the kernel:
   <br>Change to the kernel source directory and configure the kernel based on your system's current configuration
   <pre>
	   cd linux-5.19.10
	   make oldconfig
   </pre>
   This will create a `.config` file with the configuration options.
	
5. Prior to compiling the kernel, download the src directory from the project and replace the corresponding files with the modified ones:
      <pre>
	   cp  src/suss/tcp_cubic.c	linux-5.19.10/net/ipv4/tcp_cubic.c
	   cp  src/suss/tcp_input.c	linux-5.19.10/net/ipv4/tcp_input.c
	   cp  src/suss/tcp_output.c	linux-5.19.10/net/ipv4/tcp_output.c
	   cp  src/suss/tcp_cong.c	linux-5.19.10/net/ipv4/tcp_cong.c
	   cp  src/suss/tcp.h		linux-5.19.10/include/linux/tcp.h
   </pre>
   Open the `.config` file with a text editor and find keys of CONFIG_SYSTEM_TRUSTED_KEYS and CONFIG_SYSTEM_REVOCATION_KEYS and empty their values.
      
6. Change to the kernel source directory and compile the kernel:
   <pre>
	   make
   </pre>
   <br>The compilation process may take some time, depending on your system's hardware.
7. After successful compilation, install the new kernel using:
   <pre>
	   sudo make modules_install
	   sudo make install
   </pre>
   <br>This will install the kernel image, kernel modules, and update the bootloader configuration.
8. Update GRUB Configuration:
   <pre>
	   sudo update-grub
   </pre>
9. After the installation and GRUB configuration update, reboot your system to use the newly compiled kernel:
   <pre>
	   sudo reboot
   </pre>


### Working with SUSS
SUSS logs TCP measurements in the file `/var/log/kern.log` on servers where it is installed, making it convenient to access the measurements after downloading from such servers.
To filter the logs, use:
<pre>
	grep "SUSSmsg" /var/log/kern.log
</pre>

### Contact Us
This section will be updated after the publication of the paper.

### References
1. S. Ha, I. Rhee, and L. Xu, “CUBIC: a new TCP-friendly high-speed TCP variant,” ACM SIGOPS operating systems review, vol. 42, no. 5, pp. 64–74, 2008.
2. N. Dukkipati and N. McKeown, “Why Flow-Completion Time is the Right Metric for Congestion Control,” ACM SIGCOMM Computer Communication Review, vol. 36, no. 1, pp. 59–63, 2006.
