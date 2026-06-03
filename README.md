# SUSS: Improving TCP Performance by Speeding Up Slow Start


## Table of Contents
1. [Overview](#overview)
2. [List of Modifications in the Linux Source Code](#list-of-modifications-in-the-linux-source-code)
3. [Installation Guide](#installation-guide)
4. [Working with SUSS](#working-with-suss)
5. [Contact Us](#contact-us)
6. [References](#references)

### Overview
Welcome to SUSS (Speeding Up Slow Start), an open-source project aimed at tackling the issue of bandwidth under-utilization during the TCP slow-start phase. Our lightweight sender-side add-on, compatible with CUBIC [1] and implemented in Linux kernel 5.19.10, focuses on reducing flow completion time (FCT), a vital performance metric for the Internet end-users [2].
With SUSS, users simply need to apply the changes to the Linux kernel's source code and recompile it for seamless integration. This README provides an installation guide and excerpts from our paper, _SUSS: Improving TCP Performance by Speeding Up Slow-Start_, published in SIGCOMM 2024. For a comprehensive understanding of SUSS, including its theoretical foundation and performance evaluation, please refer to the [paper](./SUSS_SIGCOMM2024.pdf).
We value community contributions and will share the contributing guidelines shortly. Join us in optimizing TCP connections for faster, more efficient data transfer.


### List of Modifications in the Linux Source Code
SUSS introduces multiple modifications to the Linux TCP source code. <b>We also added lines of code for logging and performance tracking, which can be removed in the final product.</b> The original Linux files can be found in the `sourceCode/linux-VER/orig` directory, while the altered versions are stored in the `sourceCode/linux-VER/suss` directory within the project.
These changes are as follows:

- a) In the file `tcp.h`, SUSS defines a set of global variables within the `struct tcp_sock`.
- b) The slow-start mechanism of CUBIC has been altered by SUSS in the file `tcp_cubic.c`.
- c) SUSS has made a small modification to the file `tcp_output.c` to enable data transmission during the pacing period.
- d) In the file `tcp_input.c`, which deals with incoming acknowledgments (ACKs), SUSS adds a few lines of code.
- e) To assign a random ID for each test, a few lines of code have been added to the file `tcp_cong.c`.

You can identify SUSS's specific modifications in the Linux source code by searching for the "`/* suss`" comments in the files located in the `sourceCode/linux-VER/suss` directory.


### Installation Guide
SUSS is currently implemented in Linux kernel 5.19.10 and 6.8.4. We recommend using a Debian-based Linux distribution: we have been using Ubuntu. Since SUSS is a server-side approach, no changes need to be applied at the client side.
The simple installation process involves the following steps:
1. Prepare a Linux server. We recommend Ubuntu Server 24.04 LTS; you can download the ISO file from [here](https://old-releases.ubuntu.com/releases/24.04/ubuntu-24.04-live-server-amd64.iso).

2. Update the local package index and install necessary build tools:
   <pre>
   sudo apt-get update
   sudo apt-get install -y build-essential libncurses-dev libssl-dev make gcc gawk flex  \
			   openssl dkms libelf-dev libudev-dev  libpci-dev libiberty-dev \
			   bison autoconf llvm zstd dwarves bc
   </pre>

3. Install the Linux source package from the repository, then navigate to the source directory and extract the files.
   <pre>
   sudo apt-get install linux-source-6.8.0=6.8.0-51.52
   cd /usr/src
   sudo tar -xvf linux-source-6.8.0.tar.bz2
   </pre>

4. Copy the current kernel configuration file from the boot directory to the extracted directory, renaming it as `.config`. This step ensures that the existing kernel settings are preserved and used as a baseline for further configuration. 
   <pre>
   cd /usr/src/linux-source-6.8.0/
   sudo cp /boot/config-$(uname -r) .config
   sudo make oldconfig
   </pre>

5. Open the `.config` file located at `/usr/src/linux-source-6.8.0` with a text editor and find keys of `CONFIG_SYSTEM_TRUSTED_KEYS` and `CONFIG_SYSTEM_REVOCATION_KEYS` and empty their values.


6. Prior to compiling the kernel, download the `sourceCode` directory from the project and replace the corresponding files with the modified ones. In this example, run:
   <pre>
   sudo cp  sourceCode/linux-6.8/suss/tcp_cubic.c   /usr/src/linux-source-6.8.0/net/ipv4/tcp_cubic.c
   sudo cp  sourceCode/linux-6.8/suss/tcp_input.c   /usr/src/linux-source-6.8.0/net/ipv4/tcp_input.c
   sudo cp  sourceCode/linux-6.8/suss/tcp_output.c  /usr/src/linux-source-6.8.0/net/ipv4/tcp_output.c
   sudo cp  sourceCode/linux-6.8/suss/tcp_cong.c    /usr/src/linux-source-6.8.0/net/ipv4/tcp_cong.c
   sudo cp  sourceCode/linux-6.8/suss/tcp.h         /usr/src/linux-source-6.8.0/include/linux/tcp.h
   </pre>
      
7. Compile the kernel:
   <pre>
   cd /usr/src/linux-source-6.8.0/
   sudo make -j $(nproc)
   </pre>
   <br>The compilation process may take some time, depending on your system's hardware.

8. After successful compilation, install the new kernel using:
   <pre>
   sudo make modules_install -j $(nproc)
   sudo make install
   </pre>
   <br>This will install the kernel image, kernel modules, and update the bootloader configuration.

9. Update GRUB Configuration:
   <pre>
   sudo update-grub
   </pre>

10. After the installation and GRUB configuration update, reboot your system to use the newly compiled kernel:
   <pre>
   sudo reboot
   </pre>
If the server fails to boot with the new kernel, enter the BIOS settings and disable the Secure Boot option.

11. To verify a successful installation, confirm that `suss` appears in the output of:
   <pre>
   ls /sys/module/tcp_cubic/parameters
   </pre>
Please note that the value of the module parameter `suss` indicates whether SUSS is enabled (`1`) or disabled (`0`).

### Working with SUSS
Please see [usageGuide](./usageGuide).

### Contact Us
For questions, feedback, or issues related to this project, feel free to reach out to **Mahdi Arghavani** via email: ma.arghavani[at]yahoo.com

### References
1. S. Ha, I. Rhee, and L. Xu, “CUBIC: a new TCP-friendly high-speed TCP variant,” ACM SIGOPS operating systems review, vol. 42, no. 5, pp. 64–74, 2008.
2. N. Dukkipati and N. McKeown, “Why Flow-Completion Time is the Right Metric for Congestion Control,” ACM SIGCOMM Computer Communication Review, vol. 36, no. 1, pp. 59–63, 2006.

