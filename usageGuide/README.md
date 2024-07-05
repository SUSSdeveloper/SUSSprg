### Working with SUSS
SUSS logs TCP measurements in the file `/var/log/kern.log` on servers where it is installed, making it convenient to access the measurements after downloading from such servers.
To filter the logs, use:
<pre>
	grep "SUSSmsg" /var/log/kern.log
</pre>
