# QDDA installation

QDDA runs on Linux 64-bit.

## Requirements

Linux x86_64 system, glibc >= 2.12 (RHEL/CentOS 6.x)

# INSTALL

## From YUM repository (RPM)

```
# install dirty-cache repository
yum install https://yum.dirty-cache.com/dcrepo-release.rpm

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
wget https://github.com/bsjerps/qdda/archive/master.zip

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

