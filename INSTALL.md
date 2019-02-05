# QDDA installation

QDDA runs on Linux 64-bit.

## Requirements

Linux x86_64 system, glibc >= 2.12 (RHEL/CentOS 6.x)

# INSTALL

## From YUM repository (RPM)

```
# install outrun-extras repository
yum install http://yum.outrun.nl/outrun-extras/outrun-extras.rpm

# install qdda
yum install qdda
```

## Build QDDA from source

Build requirements:
  * standard build tools (make, gcc_
  * gcc-c++ compiler

## Instructions
```
# Clone or download QDDA from github:
wget https://github.com/outrunnl/qdda/archive/master.zip
# unzip
unzip master.zip
# change to src directory
cd qdda-master/src
# make
make
# (optional) install to /usr/local
sudo make install
# (alternative) install to $HOME/bin
```

