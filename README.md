# :warning: The project webpage is at http://open-mx.gitlabpages.inria.fr

## :warning: For questions and problems, see 'Reporting Bugs' at the end of this file.


# Quick Start

Assuming you want to connect 2 nodes using their 'eth2' interface:

### Build and Install
Build and install Open-MX in `/opt/open-mx`:
```
$ ./configure
$ make
$ make install
```

If building from GIT, note that several files such as the
configure script and some .in files must be generated first.
autoconf, autoheader, automake and libtool 2 are required
to do so.
```
$ ./autogen.sh
$ ./configure
[...]
```

### Check Interfaces
Make sure both interfaces are up with a large MTU
```
$ ifconfig eth2 up mtu 9000
```

### Load the Kernel Module
Load the open-mx kernel module and tell it which interface to use
```
$ /path/to/open-mx/sbin/omx_init start ifnames=eth2
```
See 'Building and Installing' below for details.

### Check the Peers
Wait a couple of seconds and run `/path/to/open-mx/bin/omx_info` to
check that all peers are seeing each other.
See 'Peer Discovery' below for details.
```
$ omx_info
[...]
Peer table is ready, mapper is 01:02:03:04:05:06
================================================
  0) 01:02:03:04:05:06 node1:0
  1) a0:b0:c0:d0:e0:f0 node2:0
```

### Check Performance
Use `omx_perf` to test actual communications, on the first node:
```
node1 $ omx_perf
Successfully attached endpoint #0 on board #0 (hostname 'node1:0', name 'eth2', addr 01:02:03:04:05:06)
Starting receiver...
```
then on the second node:
```
node2 $ omx_perf -d node1:0
Successfully attached endpoint #0 on board #0 (hostname 'node2:0', name 'eth2', addr a0:b0:c0:d0:e0:f0)
Starting sender to node1:0...
```
You should get performance numbers such as
```
length         0:          7.970 us     0.00 MB/s         0.00 MiB/s
length         1:          7.950 us     0.00 MB/s         0.00 MiB/s
[...]
length   4194304:       8388.608 us   500.00 MB/s       476.83 MiB/s
```
See the the `omx_perf.1` manpage for more details.


# Building and Installing

### Basic Install

```
$ ./configure --prefix=...
$ make
$ make install
```

By default, Open-MX will be installed in `/opt/open-mx`. Use --prefix on
the configure line to change this.

Open-MX provides the omx_init initialization scripts which takes care of
loading/unloading the driver and managing the peer table.
```
$ sbin/omx_init start
```
To choose which interfaces have to be attached, some module parameters
may be given on the command line:
```
$ sbin/omx_init start ifnames=eth1
```

To simplify Open-MX startup, you might want to install the `omx_init`
script on each node:
```
$ sbin/omx_local_install
```
Then Open-MX may then be started with:
```
$ /etc/init.d/open-mx start
```
You might want to configure your system to auto-load this script at
startup.

The `/etc/open-mx/open-mx.conf` may be modified to configure which
interfaces should be attached at startup.

### Kernel Module

Open-MX is composed of a user-space library with passes communication
commands to a kernel module. This module `open-mx` will be installed
in `lib/modules/<kernel>/` with the Open-MX installation, i.e.
`/opt/open-mx/lib/modules/<kernel>/` by default.

During configure, Open-MX checks the running kernel with `uname -r` and
builds the open-mx module against it, using its headers and build tree
in `/lib/modules/\`uname -r\`/{source,build}`.

To build for another kernel, use
```
$ ./configure --with-linux-release=2.6.x-y
```
If needed, you may also specify your kernel header directory
```
$ ./configure --with-linux-release=2.6.x-y \
              --with-linux=/path/to/kernel/headers/
```
and maybe even your kernel build directory
```
$ ./configure --with-linux-release=2.6.16.60-0.34 \
              --with-linux=/usr/src/linux-2.6.16.60-0.34 \
              --with-linux-build=/usr/src/linux-2.6.16.60-0.34-obj/x86_64/smp/
```
This may be useful on distribution such as Suse where kernel headers
and build tools are not in the same directory.

The `open-mx` kernel module should be loaded prior to any user-space
usage of Open-MX. If not loaded, a "No device" error will be returned.
The `omx_init` script takes care of loading the module
```
$ /path/to/open-mx/sbin/omx_init start ifnames=eth2
```

This module works at least on Linux kernels >=2.6.15.


# Attaching interfaces

By default, all existing network interfaces in the system will be
attached to Open-MX (except those above 8 by default), except the
ones that are not Ethernet, are not up, or have a small MTU.

To change the order or select which interfaces to attach, you may
use the `ifnames` module parameter when loading:
```
$ /path/to/open-mx/sbin/omx_init start ifnames=eth2,eth3
$ insmod lib/modules/.../open-mx.ko ifnames=eth3,eth2
```

The current list of attached interfaces may be observed by reading
the `/sys/module/open_mx/parameters/ifnames` special file.
Writing `foo` or `+foo` in the file will attach interface `foo`.
Writing `-bar` will detach interface `bar`, except if some endpoints
are still using it. To force the removal of an interface even if some
endpoints are still using it, `--bar` should be written in the special
file. Multiple commands may be sent at once by separating them with
commas.

The list of currently open endpoints may be seen with:
```
$ omx_endpoint_info
```
The interfaces may also be observed with the `omx_info` user-space
tool.

Note that these interfaces must be `up` in order to work.
```
$ ifconfig eth2 up
```
However, having an IP address is not required.

Also, the MTU should be large enough for Open-MX packets to transit.
9000 will always be enough. Look in dmesg for the actual minimal MTU
size, which may depend on the configuration.
```
$ ifconfig eth2 mtu 9000
```
It is possible to force Open-MX to use a 1500bytes MTU by passing
`--enable-mtu-1500` to the configure script, but it may reduce large
message throughput a lot.


# Peer Discovery

Each Open-MX node has to be aware of the hostnames and MAC addresses
of all the other peers. By default, a dynamic peer discovery is performed
but it is also possible to enter a static list of peers manually.

The `--enable-static-peers` option may be used on the configure command
line to switch from dynamic to static peer table. It is also possible
to switch later by passing `--dynamic-peers` or `--static-peers` to the
`omx_init` startup script.

It is possible to restart the peer table management process without
restarting the whole Open-MX driver with:
```
$ omx_init restart-discovery
```
This is especially important when attaching or detaching interfaces at
runtime while using dynamic peer discovery. But it may also for instance
be used to switch between static and dynamic peer table.


### Dynamic Peer Discovery

By default, Open-MX uses the omxoed program to dynamically discover
all peers connected to the fabric, including the ones added later.
The only requirement is that the `omxoed` program runs on each peer.

If Myricom's FMA source directory is unpacked within the Open-MX
source (as the `fma` subdirectory), Open-MX will automatically switch
(at configure time) to using FMA instead of `omxoed` as a peer discovery
program. Using FMA is especially important when talking to native MX
hosts since they will use FMA by default as well.

The discovery program is started automatically by the `omx_init` startup
script. If Open-MX has been configured to use a static peer table
by default, it is still possible to switch to dynamic discovery
by passing `--dynamic-peers` to omx_init.

### Static Peer Table

Dynamic discovery may sometimes take several seconds before all nodes
become aware of each others. If the fabric is always the same, it is
possible to setup a static peer table using a file. To do so, Open-MX
should be configured with `--enable-static-peers`.

A file listing peers must be provided to store the list of hostnames
and MAX addresses in the driver. The `omx_init_peers` tool may be used
to setup this list. The `omx_init` startup script takes care of running
`omx_init_peers` automatically using `/etc/open-mx/peers` when it exists.

The contents of the file is one line per peer, each containing
2 fields (separated by spaces or tabs):
* a MAC address (6 colon-separated numbers)
* a board hostname (`<hostname>:<ifacenumber>`)


# Native MX Compatibility

Open-MX provides API, ABI and wire-compatibility with the native MX stack.

### Wire-Compatibility

If you need some Open-MX hosts to talk to some MX hosts, you should
enable wire-compatibility (it is disabled by default).
If you only have Open-MX hosts talking on the network, you can keep it
disabled to improve performance.

Once Open-MX is configured in wire compatible mode, you need to make
sure that the nodes running in native MX mode are using MX >= 1.2.5
configured in Ethernet mode.

##### If using dynamic peer discovery
You have to make sure that the same peer discovery program (or `mapper`)
is used on both sides. by default, MX uses the FMA by default. So the
FMA source should be unpacked as a `fma` subdirectory of the Open-MX
source so that the configure script will enable FMA by default instead
of `omxoed` for dynamic peer discovery.

Under some circumstances, MX may also rely on `mxoed`, which is
compatible with Open-MX' `omxoed`.

##### If using a static peer table
The peer table should be setup on the Open-MX nodes as usual with
`omx_init_peers`, with a single entry for each Open-MX peer and each
MX peer.

On the MX nodes, each Open-MX peer with name `myhostname:0` and MAC
address `00:11:22:33:44:55` should be added with:
```
$ mx_init_ether_peer 00:11:22:33:44:55 00:00:00:00:00:00 myhostname:0
```

Note that it is possible to let the regular MX dynamic discovery
map the MX-only fabric and then manually add the Open-MX peers.
To do so, the regular discovery should first be stopped with:
```
$ /etc/init.d/mx stop-mapper
```

Once peers are setup on both MX and Open-MX nodes, the fabric is ready.

### ABI and API Compatibility

The Open-MX API is slightly different from the MX one, but Open-MX provides
a compatibility layer which enables:
* To link applications that were compiled against MX
* To build applications that were written for the MX API
This compatibility is enabled by default and has a very low overhead since
it only involves going across basic conversion routines.


# Reporting Bugs

In case of problem, make sure you read the README first :)
Please also look at the FAQ in the doc/ directory or online at
http://open-mx.gitlabpages.inria.fr/FAQ/

Bugs should be reported on http://gitlab.inria.fr/open-mx/open-mx
or sent to open-mx@inria.fr .
Questions may be asked there too.

When reporting a problem, make sure you include:
* the version number of Open-MX
  (or the output of `git show` if you checked out the GIT repository)
* the output (as root) of
  + `cat /dev/open-mx`
  + `omx_info`
* a description of the ethernet interfaces that you are trying
  to use, with the whole outputs (as root) of:
  + `lspci`
  + `lsmod`
  + `dmesg`
  + `ifconfig -a`
  + `ethtool -c <iface>`
* a description of your network topology:
  + how are the interfaces connected?
  + through what switch(es)? is the MTU configured there too?
* the whole output of the program if a program did not work
  as expected
* the whole output of `configure`, `config.log` and `omx_checks.h`,
  and of the compilation if reporting a build problem
