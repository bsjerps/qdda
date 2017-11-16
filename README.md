QDDA - The Quick and Dirty Deduplication Analyzer
======================


## Description
QDDA checks files, named pipes or block devices for duplicate blocks to estimate 
deduplication efficiency on dedupe capable All-Flash Arrays. 
It also estimates compression ratios.

QDDA uses MD5 hashing and LZ4 compression and stores hash values, hash counts and
compressed bytes counts in SQLite database format.

Other features:

- Can scan live environments (such as prod database servers)
- Can read from named/unnamed pipes - which allows qdda to run as non-root user
- Blocksize adjustable between 1K and 128K
- Built-in IO throttle to avoid overloading production systems (default 200MB/s)
- Can merge (combine) results from different nodes (i.e. distributed storage environments)
- Scales to datasets of multiple terabytes (tested 3+TB) although it may take a while
- Can report compression and deduplication histograms
- Scan speed on Intel i5-4440 is about 400MB/s. (single threaded)
- Data processing speed (DB index+merge) ~ 2000MB/s but will slow down a bit with large datasets
- The SQLite database can be queried with standard SQLite tools

Wiki page: http://outrun.nl/wiki/qdda

Installation: qdda is built and packaged for EL6 (CentOS, RHEL etc). See wiki page for download
instructions.

## Installation

Enterprise Linux systems (RHEL-based):

- Install the Outrun Extras repository: `yum install http://yum.outrun.nl/outrun-extras.rpm`

- Install qdda from repo: `yum install qdda`

Non-EL based:
Either copy the /usr/bin/qdda binary from the RPM package or build from source.

Building from source

Build prerequisites: sqlite-devel lz4-devel lz4-static
```
wget https://github.com/outrunnl/qdda/archive/master.zip
unzip master.zip 
cd qdda-master/
make install
```

## Usage

Run `qdda -h` for command line options, `qdda -H` for more extensive help.
Further documentation on the wiki page: http://outrun.nl/wiki/qdda

## Example

The example shows QDDA run against 3 Oracle ASM devices
with Oracle 12c and ASM. The database on disk has about 
1.2GB Swingbench benchmark data loaded plus a 700M empty tablespace.
The database is running while we scan.

![example1](https://github.com/outrunnl/qdda/blob/master/pics/example1.gif)

### Example output (Oracle ASM)

```
[root@outrun01 ~](-) # qdda /dev/oracleasm/*
qdda 1.9.2 - The Quick & Dirty Dedupe Analyzer
Use for educational purposes only - actual array reduction results may vary

                      *** Overview ***
total               =     5120.00 MiB (    327680 blocks)
used                =     3294.03 MiB (    210818 blocks)
deduped             =     2869.67 MiB (    183659 blocks)
allocated           =      831.61 MiB (     53223 blocks)
                      *** Details ***
Compression method  =      XtremIO X2
blocksize           =          16 KiB
free (zero)         =     1825.97 MiB (    116862 blocks)
compress pre dedup  =      716.86 MiB (     78.24 %)
merged by dedupe    =      424.36 MiB (     27159 blocks)
compress post dedup =      713.56 MiB (     75.13 %)
unique data         =     2590.61 MiB (    165799 blocks)
duped 2:1           =      141.78 MiB (      9074 blocks)
duped >2:1          =      137.28 MiB (      8786 blocks)
duped total         =      279.06 MiB (     17860 blocks)
                      *** Summary ***
percentage used     =       64.34 %
percentage free     =       35.66 %
deduplication ratio =        1.15
compression ratio   =        3.45
thin ratio          =        1.55
combined            =        6.16
raw capacity        =     5120.00 MiB
net capacity        =      831.61 MiB
```

raw capacity = total scanned disk space

net capacity = required space on an XtremIO X2

## Future
- Validate various compression methods (starting with VMAX all-flash)

## Licensing

QDDA is licensed under GPLv3. See "COPYING" for more info.

## Support

Please file bugs and issues at the Github issues page. 
