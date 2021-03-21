# Linux Subsystem for Linux (LSL)

Subsystem is a tool that I wrote a while ago because I needed to run specific application in specific versions. However, I wanted it to be easily callable from my host system, so non of the existing container solutions were sufficient to that (at least I couldn't find one). One of my favorite use-cases is to use a subsystem to install pen-testing utilities from one of the well known distributions that ship such tools without polluting my host system. The problem is, that most of the applications depend on shared libraries in specific version, so just copying the executable wasn't really an option because this would also require to handle the dependencies manually.

LSL conveniently allows to run and maintain multiple root filesystems on a single linux machine. On the contrary to usual container approaches such as Docker or LXC, LSL does not isolate the different filesystems (It has never be intended to provide additional security to you system). Instead it only implements the abstraction that is necessary to have multiple root filesystem. In particular, subsystems uses mount namespaces to create containers for the additional root filesystem. These containers then have a different root filesystem than the host system.

To conveniently execute binaries of a subsystem, the host root filesystem is extended by links to a program that automatically changes into the appropriate mount namespace. The listing below shows how the program `id` can be executed:
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

## Build Instructions & Binaries
Subsystem can be built using the provided CMakeLists.txt. The build process generates two binaries

* lsl: Initializes the mount namespaces:
	* To initialize the containers: `sudo lsl start`
	* To stop containers: `sudo lsl stop`
	* To recreate links (e.g. after installation of additional binaries): `sudo lsl relink`
* lslExecutor: Executes applications inside the contained (usually does not have to be  manually invoked)

### Security Considerations
The lslExecutor application is designed to be a root owned setuid binary. This is a bit dangerous, but required because to enter the mount namespaces of the subsystem requires the CAP_SYS_ADMIN and CAP_SYS_CHROOT capabilities are required. Literally the first thing the lslExecutor does is dropping any other capabilities from the effective and permitted set (although CAP_SYS_ADMIN will probably be quite easy to escape...). lslExecutor will then drop back to the real user id (which is an unprivileged user if the user executing lslExecutor wasn't already root before) after the mount namespace of the subsystem has been entered. This will drop the remaining capabilities in case the real user id is not root. Alternatively you can add the required capabilties using file capabilties.

The lsl application on the other hand requires CAP_SYS_ADMIN to setup the mount namespace, all other capabilites will be dropped. In addition, a seccomp filter is applied by the application by default. This can be disabled by passing the --disable-seccomp option (which should however only be used in case of problems).
### CMake

The CMakeLists.txt provides the following options to customize lsl:

* `MNTDIR` Specifies the directory that shall contain the file the mount namespaces are bind mounted to. Default: /tmp/subsys
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
The containers to be setup by lsl are specified using a configuration file in the INI-format:
```
[container-name]
path=<path to the new root directory>
mnt=<;-separated list of files or directories to be mounted>
bins=<;-separated list of files or directories that binaries are searched in>
envPath=<new PATH environment variable (optional)>
interpreter=<interperter for binaries, e.g. /usr/bin/qemu-aarch64-static. THIS A BETA FEATURE. YOU HAVE BEEN WARNED :-D>
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
mnt=/etc/passwd;/etc/shadow;/etc/group;/etc/resolv.conf;/etc/sudoers;/home/
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

## Default Mounts
The following directories are mounted by default into all containers:

* /dev
Within /dev the following virtual filesystem are mounted:
	* /dev/pts
	* /dev/shm
	* dev/mqueue
	* dev/hugepages
* /run

## Usage Example:
#### 1 Download and extract root filesystem

```
$ cd /opt/
$ mkdir subsystems
$ cd subsystems
$ mkdir arch
$ cd arch
$ wget https://images.linuxcontainers.org/images/archlinux/current/amd64/default/<BUILDTIME>/rootfs.tar.xz
$ unxz rootfs.tar.xz
$ sudo tar xvf rootfs.tar
```
#### 2 Add Entry to config file (default /etc/subsys.conf):
```
[arch]
path=/opt/subsystems/arch
mnt=/etc/passwd;/etc/shadow;/etc/group;/etc/resolv.conf;/etc/sudoers;/home/
bins=/usr/bin
```

#### 3 Setup container
```
$ sudo subsystem start
```
If subsystem is already setup it needs to be stopped and than started again

#### 4 Install additional application into container
```
$ sudo arch:pacman -Sy && sudo arch:pacman -S <paket-name>
$ sudo subsystem relink
```

#### 5 Use applications:
```
$ arch:<binary-name> [args...]
```
A major advantage is that you can use files of you hosts rootfs directly, for instance
```
# This happens in the host system
$ touch ./123-test
# Access to this file is pretty natural even with application of the subsystem:
$ arch:ls -al ./123-test
Executing /usr/bin/ls in arch

-rw-r--r-- 1 someuser someuser 0 Mar 20 00:29 ./123-test
```

You can also get a shell in the subsystem by doing the following (assuming bash is installed there).
```
$ arch:bash
```
In that case the root of host system can be found at `/oldRoot`. Sometime its required to get a root shell to configure the container:
```
$ sudo arch:bash
```
If configured correctly, you should be able to even use sudo inside the container. Please note that this does however not work out of the box for most containers.

## Todos
 * Code needs some refactoring
