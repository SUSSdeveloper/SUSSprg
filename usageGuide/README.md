# Working with SUSS

## Table of Contents
1. [Introduction](#introduction)
2. [Usage Example](#usage_example)

### Introduction

This tutorial provides an introduction to SUSS and demonstrates its application through a practical example.
While SUSS can handle TCP data transfers across any port number, we will focus on port 80 for this tutorial, utilizing the Apache2 web server. Please install Apache2 on the server where SUSS is already installed.

<pre>
   sudo apt-get install -y apache2
</pre>

Assuming you have a client capable of downloading files from the server, use the following command to download the file `index.html’:
<pre>
   wget http://IPaddress_of_the_server/index.html
</pre>

SUSS records TCP measurements in `/var/log/kern.log’ on the server, which allows for easy access to the measurements after downloading.
To filter the logs after a successful download, execute the following command on the server:
<pre>
   grep "SUSSmsg" /var/log/kern.log
</pre>
Note: If no output appears, either the file has not been downloaded, or SUSS has not been installed correctly. Verify the installation of the new kernel by executing `uname -a’ and ensure that the upgraded version is in use.


Given that `index.html’ is a very small file, we recommend using a larger file, such as `dummyfile.dat’, to better explore the capabilities of SUSS.
Create a dummy file and move it to the web root directory with these commands:

<pre>
   dd if=/dev/zero of=dummyfile.dat bs=1M count=16
   sudo mv dummyfile.dat /var/www/html/.
</pre>



### Usage Example
1. By default, SUSS is disabled. To enable it, you must change the value of the SUSS module parameter from 0 to 1.

<pre>
   cat  /sys/module/tcp_cubic/parameters/suss
   echo 1 | sudo tee /sys/module/tcp_cubic/parameters/suss
</pre>

2. To prevent previous logs from interfering with the current test, clear the contents of `/var/log/kern.log’:
<pre>
   sudo sh -c '> /var/log/kern.log'
</pre>

3. On the client, download the dummy file from the server. Then, on the server, copy the logs into a file:
<pre> 
   cat /var/log/kern.log > raw.suss1
</pre>
Use the name `raw.suss1’ if SUSS is enabled, and `raw.suss0’ if it is disabled. These filenames will be used in subsequent steps to extract useful information.
Disable SUSS and repeat the previous two steps to gather logs for when SUSS is disabled.

4. At this point, you should have two files, `raw.suss0’ and `raw.suss1’, representing the logs when SUSS is disabled and enabled, respectively.
If the output of the following command for each raw file indicates a message like "SUSSmsg cubic starts sending data. Follow id=143 for Sport=20480", it suggests that each raw file documents a single download.
<pre>
   grep -i starts raw.suss?
</pre> 

5. Each download is identified by its id. Use the bash script `extract.sh’ located [here](./example) to prepare the files `data.suss0’ and `data.suss1’, which will be used for plotting:
<pre>
   bash extract.sh raw.suss0
   bash extract.sh raw.suss1
</pre>

6. At this stage, you should have two data files, `data.suss0’ and `data.suss1’, in your working directory. Execute the following command to generate and display a plot that compares the total data delivered over time in both enabled and disabled scenarios:
<pre>
   gnuplot delivered.tr
   xdg-open delivered.eps
</pre>
