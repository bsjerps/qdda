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

- Install the Outrun Extras repository
`yum install http://yum.outrun.nl/outrun-extras.rpm`

- Install qdda from repo
`yum install qdda`

Non-EL based:
Either copy the /usr/bin/qdda binary from the RPM package or build from source.

Building from source

Build prerequisites: sqlite-devel lz4-devel lz4-static

wget https://github.com/outrunnl/qdda/archive/master.zip
unzip master.zip 
cd qdda-master/
make install

## Usage

Run "qdda -h" for command line options, "qdda -H" for more extensive help.
Further documentation on the wiki page: http://outrun.nl/wiki/qdda

## Future
- Validate various compression methods (starting with VMAX all-flash)

## Licensing

QDDA is licensed under GPLv3. See "COPYING" for more info.

## Support

Please file bugs and issues at the Github issues page. 
