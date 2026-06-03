# Working with SUSS

## Table of Contents
1. [Introduction](#introduction)
2. [Usage Example](#usage-example)

### Introduction

This tutorial provides an introduction to SUSS and demonstrates its application through a practical example.
While SUSS can handle TCP data transfers across a big range of port numbers, we will focus on port 80 for this tutorial, utilizing the Apache2 web server. Please install Apache2 on the server where SUSS is already installed.

<pre>
   sudo apt-get install -y apache2
</pre>

Assuming you have a client capable of downloading files from the server, use the following command to download the file `index.html`. To avoid any optimizations made by web browsers, download the file using either either `wget` or `curl` from a command line interface.
We recommend setting up a client-server connection with a round-trip time (RTT) exceeding 50ms and a bottleneck bandwidth greater than 50Mbps, as SUSS provides significant benefits in networks with large Bandwidth-Delay Products (BDP).

<pre>
   wget http://IPaddress_of_the_server/index.html
</pre>

SUSS records TCP measurements in `/var/log/kern.log` on the server, which allows for easy access to the measurements after downloading.
To filter the logs after a successful download, execute the following command on the server:
<pre>
   grep "SUSSmsg" /var/log/kern.log
</pre>
Note: If no output appears, either the file has not been downloaded, or SUSS has not been installed correctly. Verify the installation of the new kernel by executing `uname -a` and ensure that the upgraded version is in use.


Given that `index.html` is a very small file, we recommend using a larger file, such as `dummyfile.dat`, to better explore the capabilities of SUSS.
Create a dummy file and move it to the web root directory with these commands:

<pre>
   dd if=/dev/zero of=dummyfile.dat bs=1M count=16
   sudo mv dummyfile.dat /var/www/html/.
</pre>


### Usage Example
1. By default, SUSS is disabled. To enable it, you must change the value of the suss module parameter from 0 to 1, and set the TCP congestion control algorithm to Cubic.

<pre>
   cat  /sys/module/tcp_cubic/parameters/suss
   echo 1 | sudo tee /sys/module/tcp_cubic/parameters/suss
   sudo sysctl net.ipv4.tcp_congestion_control=cubic
</pre>

2. To prevent previous logs from interfering with the current test, clear the contents of `/var/log/kern.log`:
<pre>
   sudo sh -c '> /var/log/kern.log'
</pre>

3. Using either `wget` or `curl` on the client, download the dummy file from the server. After a few minutes, copy the logs into a file on the server:
<pre> 
   cat /var/log/kern.log > raw.suss1
</pre>
Use the name `raw.suss1` if SUSS is enabled, and `raw.suss0` if it is disabled. These filenames will be used in subsequent steps to extract useful information.
Disable SUSS and repeat the previous two steps to gather logs for when SUSS is disabled.

_Note: On newer operating systems, such as Ubuntu 24.04, you may use journalctl to read the kernel logs._

4. As shown in the [example](./example), you should now have two files, `raw.suss0` and `raw.suss1`, representing the logs when SUSS is disabled and enabled, respectively.
If the output of the following command for each raw file indicates a message like "SUSSmsg cubic starts sending data. Follow id=143 for Sport=20480", it suggests that each raw file documents a single download.
<pre>
   grep "SUSSmsg cubic starts sending data" raw.suss?
</pre> 

5. Each download is identified by its id. Use the bash script `extract.sh` located [here](./example) to prepare the files `data.suss0` and `data.suss1`, which will be used for plotting:
<pre>
   bash extract.sh raw.suss0
   bash extract.sh raw.suss1
</pre>

6. At this stage, you should have two data files, `data.suss0` and `data.suss1`, in your working directory.
Using the provided Gnuplot script `delivered.tr`, located [here](./example), generate a plot that compares the total data delivered over time in both the enabled and disabled tested scenarios.
<pre>
   gnuplot delivered.tr
   xdg-open delivered.eps
</pre>

7. To also plot the improvement curve, execute the following commands:
<pre>
   bash improvement.sh
   gnuplot improvement.tr
   xdg-open improvement.eps
</pre>

