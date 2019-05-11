# subsystem

Subsystem is a tooling that to convientently allow to run and maintain multiple root filesystems on a single linux machine. On the contrary to usual container such as Docker or LXC, subsystem does not isolate the different filesystems. Instead it only implements the abstraction that is neccessary to have multiple root filesystem. In particular, subsystems uses mount namespaces to  create containers for the additional root filesystem. These containers then have a differnt root filesystem than the host system. 

To conviniently execute binaries of a subsystem, the host root filesystem is exended by links to a program that automatically changes into the appropriate mount namespace. The listing below shows how the program `id` can be executed:
```
$ blackarch:id
Executing /usr/bin/id in blackarch

uid=1000(user) gid=1000(user) groups=1000(user),90(network),98(power),986(video),988(storage),991(lp),995(audio),998(wheel)

$ sudo blackarch:id
Executing /usr/bin/id in blackarch

uid=0(root) gid=0(root) groups=0(root)
```

To install packets from the packet repository, one can execute a bash within the container or call the packet manger directly:
```
$sudo blackarch:bash
Executing /usr/bin/bash in blackarch

[root@manjaro /]#  pacman  -Sy
```
or
```
$ sudo blackarch:pacman -Sy
Executing /usr/bin/pacman in blackarch
...
```

## Build Instructions
Subsystem can be built using the provided CMakeLists.txt. The build process generates two binaries 

* subsystem
* subsystemExecutor

The subsystemExecutor binary is designed to be a root owned setuid binary. This is because in order to enter the mount namespaces of the container  requires the CAP_SYS_ADMIN capability. The subsystemExecutor wil, however, drop back to the crendentials of the user after the mount namespace has been entered.

The CMakeLists.txt provides the following options to customize subsystem:

* `MNTDIR` Specifies the directory that shall contain the file the mount namepsaces are bind mounted to. Default: /tmp/subsys
* `LINKSDIR`Specifies the directory (in the host system) that shall contain the links to the binaries in the containers. Default: /subsysbin
* `CONFIGPATH` Specifies the path to the configuration file. Default: /etc/subsys.conf
* `INSTALLDIR` Specifies the location the binaries shall be installed to. Default: /bin

Example:
```
$ mkdir build
$ cd build
$ cmake ..  -DINSTALLDIR=/usr/bin
$ make
$ sudo make install
$ < add /subsysbin to  PATH environemnt variable>
```


## Configuration File
The containers to be setup by subsystem are specified using a configuration file in the INI-format:
```
[container-name]
path=<path to the new root directory>
mnt=<;-separated list of files or directories to be mounted>
bins=<;-separated list of files or directories that binaries are searched in>
envPath=<new PATH environment variable (optional)>
```
If the mountpoint within the container shall be different to the location in the root filesystem the new mount point can be specified by adding `:<new mount point>`  to the path, e.g. `/etc/file:/etc/other/file`

Example:
```
[debian]
path=/opt/subsystems/debian
mnt=/home/;/proc/;/etc/resolv.conf
bins=/bin;/usr/bin/
envPath=/usr/local/bin:/usr/bin:/bin:/usr/local/sbin:/usr/local/sbin:/usr/sbin:/sbin

[blackarch]
path=/opt/subsystems/blackarch
mnt=/etc/passwd;/etc/shadow;/etc/group;/etc/resolv.conf;/etc/sudoers;/home/;/proc/
bins=/usr/bin
```

## Container-Images

Images for containers can be obtained from <https://images.linuxcontainers.org/images/>. Please note that images should be extracted as root in order to preserve file permissions of the container.

Example:
```
$ mkdir arch
$ cd arch
$ wget https://images.linuxcontainers.org/images/archlinux/current/amd64/default/<BUILDTIME>/rootfs.tar.xz
$ unxz rootfs.tar.xz
$ sudo tar xvf rootfs.tar
```
