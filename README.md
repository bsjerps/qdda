QDDA - The Quick and Dirty Deduplication Analyzer
======================

![logo](https://github.com/outrunnl/qdda/blob/master/pics/qdda.png)

## Description
QDDA checks files, named pipes or block devices for duplicate blocks to estimate 
deduplication efficiency on dedupe capable All-Flash Arrays. 
It also estimates compression ratios.

QDDA uses MD5 hashing and LZ4 compression and stores hash values, hash counts and
compressed bytes counts in SQLite database format.

Other features:

- Does not scan just a sample - ALL data is scanned
- Can be executed as non-root (for security reasons)
- Non-Linux data (UNIX, Windows, VMware) can be scanned via IP (netcat/pipes)
- Can scan live environments (such as prod database servers)
- Blocksize adjustable between 1K and 128K
- Built-in IO throttle to avoid overloading production systems (default 200MB/s)
- Can merge (combine) results from different nodes (i.e. distributed storage environments)
- Scales to datasets of multiple terabytes (tested 3+TB) although it may take a while
- Can report compression and deduplication histograms
- Scan speed (observed) up to 7GB/s (multi-threaded)
- The SQLite database can be queried with standard SQLite tools

Wiki page: http://outrun.nl/wiki/qdda

Installation: qdda is built and packaged for EL6 (CentOS, RHEL etc). See wiki page for download
instructions.

## Download

[latest version](https://github.com/outrunnl/qdda/releases/latest)

## Installation

From ZIP file:
- Download the zipfile and place the binary qdda in $PATH

From YUM repository (RPM/YUM based):
```
yum install http://yum.outrun.nl/outrun-extras.rpm
yum install qdda
```

From SOURCE:

See [INSTALL](INSTALL.md)

## Usage

Run `qdda -h` for command line options, `qdda -m` for more extensive help.
Further documentation on the wiki page: http://outrun.nl/wiki/qdda

## Man page

See [MAN PAGE](doc/qdda.md)
you can run the embedded man page using `qdda -m` if your system supports it.
If qdda is installed from RPM or `make install` you can also do `man qdda`

## Example

The example shows QDDA run against 4 Oracle ASM devices
with Oracle 12c and ASM. The database on disk has about 
1GB Swingbench benchmark data loaded plus a 700M empty tablespace.
The database is running while we scan.

![example1](https://github.com/outrunnl/qdda-doc/blob/master/pics/qdda-demo.gif)

### Example output (Oracle ASM)

```
qdda 2.0.7 - The Quick & Dirty Dedupe Analyzer
Use for educational purposes only - actual array reduction results may vary

Database info (/home/jail/qdda.db):
database size       = 2.51 MiB
array id            = XtremIO X2
blocksize           = 16 KiB

Overview:
total               =     6144.00 MiB (    393216 blocks)
free (zero)         =     3596.94 MiB (    230204 blocks)
used                =     2547.06 MiB (    163012 blocks)
dedupe savings      =      435.56 MiB (     27876 blocks)
deduped             =     2111.50 MiB (    135136 blocks)
compressed          =      438.74 MiB (     79.22 %)
allocated           =      528.39 MiB (     33817 blocks)

Details:
used                =     2547.06 MiB (    163012 blocks)
compressed raw      =      442.53 MiB (     82.63 %)
unique data         =     1930.66 MiB (    123562 blocks)
non-unique data     =      616.41 MiB (     39450 blocks)

Summary:
percentage used     =       41.46 %
percentage free     =       58.54 %
deduplication ratio =        1.21
compression ratio   =        4.00
thin ratio          =        2.41
combined            =       11.63
raw capacity        =     6144.00 MiB
net capacity        =      528.39 MiB
```

raw capacity = total scanned disk space

net capacity = required space on an XtremIO X2

## Future
- More storage arrays
- More special database queries
- Multiple compression methods

## Licensing

QDDA is licensed under GPLv3. See "COPYING" for more info.

## Support

Please file bugs and issues at the Github issues page. 
