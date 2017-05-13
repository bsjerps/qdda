# qdda
The Quick and Dirty Deduplication Analyzer

QDDA checks files, named pipes or block devices for duplicate blocks to estimate 
deduplication efficiency on dedupe capable storage systems such as DellEMC XtremIO. 
It also estimates compression ratios.

QDDA uses MD5 hashing and LZ4 compression and stores hash values, hash counts and
compressed bytes counts in SQLite database format.

Although written for XtremIO, deduplication estimates should also be valid for any
storage platform that has block deduplication, for example ZFS.

Other features:

- Can scan live environments (such as prod database servers)
- Can read from named/unnamed pipes - which allows qdda to run as non-root user
- Blocksize adjustable between 1KiB and 64KiB
- Built-in IO throttle to avoid overloading production systems (default 200MB/s)
- Can merge (combine) results from different nodes (i.e. distributed storage environments)
- Scales to datasets of multiple terabytes (tested 3+TB) although it may take a while
- Can report compression and deduplication histograms
- Scan speed on Intel i5-4440 is about 500MB/s. (single threaded)
- Data processing speed (DB index+merge) ~ 2000MB/s but will slow down a bit with large datasets
- The SQLite database can be queried with standard SQLite tools

Space requirements for database: Expect 16GiB for 4TiB scanned data @ 8K blocksize (1:250). 
You also need additional temp space for SQLite (worst case is vacuum requiring another 16GiB).

Wiki page: http://outrun.nl/wiki/qdda

Installation: qdda is built and packaged for EL6 (CentOS, RHEL etc). See wiki page for download
instructions.
