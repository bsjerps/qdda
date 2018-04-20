/*******************************************************************************
 * Title       : helptext.cpp
 * Description : C++ manpage and help text definitions for qdda
 * Author      : Bart Sjerps <bart@outrun.nl>
 * License     : GPLv3+
 * Disclaimer  : See https://www.gnu.org/licenses/gpl-3.0.txt
 * More info   : http://outrun.nl/wiki/qdda
 * -----------------------------------------------------------------------------
 ******************************************************************************/
 
const char* manpage_head = R"(.TH qdda 1 "2018-03-24" "QDDA" "QDDA User Manual"
.SH NAME
qdda \- the quick & dirty dedupe analyzer
.SH SYNOPSIS
.B qdda <options> [FILE...]

.SH DESCRIPTION
.B qdda
checks files, data streams or block devices for duplicate blocks to estimate deduplication
efficiency on dedupe capable storage systems, using key-value stores in SQLite,
MD5 hashing and LZ4 compression.  It also estimates compression ratios for all-flash arrays
XtremIO X1 and X2 as well as VMAX AFA (experimental).
.P

.SH IMPORTANT NOTES
.B qdda
can create very large database files and generate lots of read I/O and heavy CPU load. Check the 
.B RESOURCE REQUIREMENTS
section before you start.
.br
For additional safety, run
.B qdda
as non-root user. See the 
.B SECURITY AND SAFETY
section for details on how to do this.
.P
.SH OPTIONS
)";

const char* manpage_body = R"(
.P
.B FILE
description:
.P
FILE is usually a disk device such as /dev/sda but can also be a flat file, a named pipe, a disk partition or anything else that can be
read as a stream. It will also read from stdin if it is not connected to a tty, i.e. you can do 'cat <file> | qdda '.
.P
.B Modifiers
.P
Each file can have a modifier by adding a colon (:). Currently the modifier is in the format <maxmb[,dup]> where maxmb
is the maximum amount of mibibytes to read from the stream: /dev/sda:1024 will read the first 1024MiB and then stop. dup is the
number of times the data is processed (only for testing purposes) i.e. /dev/sda:1024,2 will generate 2048 MiB with dupcount=2 
(the first 1024 MiB of sda but processed twice).
.P
.B Special\ filenames
.P
Special filenames are zero (alias for /dev/zero), random (alias for /dev/urandom) and compress (same as random but make the
data compressible). This allows you to generate test data.
.P
.B Test data example
.P
qdda zero:512 random:512 random:256,2 compress:128,4
.br
Generates a test dataset with 512MiB zeroed, 512MiB uncompressible, unique data, 256MiB uncompressible data used twice, and 128MiB 
compressible data with 4 copies each.
.P

.SH STORAGE ARRAYS
Currently qdda supports 3 storage arrays:
.br
.IP XtremIO\ X1\ (--array=x1)
The first generation XtremIO with 8KB block size and compression bucket sizes 2K, 4K, 8K. As XtremIO performs all compression and dedupe
operations inline, the results of qdda for dedupe should match the array dedupe very closely. XtremIO uses a proprietary compression
algorithm which has a slightly lower compression ratio compared to LZ4, but claims to be much faster. 
This means the qdda results are slightly over-optimistic. The differences are too small however to be a major issue.
.IP XtremIO\ X2\ (--array=x2)\ (default)
With the X2, the internal blocksize was increased to 16KiB and many more (15) compression bucket sizes are available: 1K up to 16K with
1K increments, where 15K is missing because in XtremIO X2 architecture it would allocate the same capacity as 16K uncompressed. 
The larger block size and more variations in buckets makes XtremIO compression much more effective. 
There is still a slight difference in compression ratio due to LZ4 versus the native XtremIO algorithm.
.IP VMAX\ All-Flash\ (--array=vmax1)
VMAX data reduction estimates are currently 
.B EXPERIMENTAL.
.br
VMAX compresses data using 128K chunks which get compressed in bucket sizes from 8K up to 128K with 8K increments. Not all bucket sizes are
available at initial configuration and VMAX changes the compression layout dynamically and also avoids compression
for up to 20% of all data, based on workload and data profile. This makes it hard for qdda to give reasonable estimates which is why
it is marked as EXPERIMENTAL for now. The output of qdda shows the results at most ideal circumstances (all data gets deduped and
compressed always). Future versions may improve on accuracy.
.P
The compress and hash algorithms are slightly different from these actual arrays
and the results are a (close) approximation of the real array data reduction.
Currently qdda only uses LZ4 (default) compression,
.P
.IP custom\ (--array=<custom\ definition>)
Specify a string with array=name=<name>,bs=<blocksize_kb>,buckets=<bucketsize1+bucketsize2....>
.br
example: qdda --array=name=foo,bs=64,buckets=8+16+32+48+64
.SH ERRORS
.B qdda
has basic error handling. Most errors result in simply aborting with an error message and return code.

.SH EXAMPLE
.TP 
.B qdda compress:128,4 compress:256,2 compress:512 zero:512
Analyze a compressible reference test data set with 128Mx4, 256Mx2, 512x1 and 512M zeroed.
.P
.B Example output
.nf
Database info (/home/bart/qdda.db):
database size       = 1.12 MiB
array id            = XtremIO X2
blocksize           = 16 KiB

Overview:
total               =     2048.00 MiB (    131072 blocks)
free (zero)         =      512.00 MiB (     32768 blocks)
used                =     1536.00 MiB (     98304 blocks)
dedupe savings      =      640.00 MiB (     40960 blocks)
deduped             =      896.00 MiB (     57344 blocks)
compressed          =      451.93 MiB (     49.56 %)
allocated           =      483.25 MiB (     30928 blocks)

Details:
used                =     1536.00 MiB (     98304 blocks)
compressed raw      =      775.41 MiB (     49.52 %)
unique data         =      512.00 MiB (     32768 blocks)
non-unique data     =     1024.00 MiB (     65536 blocks)

Summary:
percentage used     =       75.00 %
percentage free     =       25.00 %
deduplication ratio =        1.71
compression ratio   =        1.85
thin ratio          =        1.33
combined            =        4.24
raw capacity        =     2048.00 MiB
net capacity        =      483.25 MiB
.fi
.PP
.P
.B Explanation
.IP Database\ size 20
Size of the primary SQLite databse on disk
.IP Array\ ID
Name of array for which dedupe and compress estimates are calculated. Can be a custom string.
.IP Blocksize
Blocksize on which hashes and compression sizes are calculated
.IP Total
Total scanned blocks
.IP Free
Free (zero) blocks
.IP used
Used (non-zero) blocks
.IP dedupe\ savings
Blocks saved by merging duplicate blocks
.IP deduped
Blocks required after dedupe
.IP compressed
Capacity after compressing (deduped) blocks i.e.sum of compressed size of all blocks after dedupe
.IP allocated
Capacity after allocating compressed blocks into buckets. This is the required capacity on an inline dedupe/compress capable storage array
.IP compressed\ raw
Capacity required for compressing all raw data (before dedupe) i.e. sum of compressed size of all scanned 
blocks
.IP unique\ data
Blocks that are unique (cannot be deduped)
.IP non-unique\ data
Blocks that appear at least 2x (can be deduped)
.IP percentage\ used
Percentage of all raw blocks that are non-zero
.IP percentage\ free
Percentage of all raw blocks that are zero
.IP deduplication\ ratio
capacity used divided by deduped
.IP compression\ ratio
capacity deduped divided by allocated
.IP thin\ ratio
capacity used divided by total
.IP combined
Overall data reduction (dedupe ratio * compress ratio * thin ratio)
.IP raw\ capacity
equal to total
.IP net\ capacity
equal to allocated
.P
.TP
.B qdda --detail
Show detailed histograms from the database
.P
.B Example output
.nf
File list:
file      blksz     blocks         MiB date               url                                                                             
1         16384      32768         512 20180420_0922      workstation:/dev/zero                                                           
2         16384       8192         128 20180420_0922      workstation:/dev/urandom                                                        
3         16384      16384         256 20180420_0922      workstation:/dev/urandom                                                        
4         16384      32768         512 20180420_0922      workstation:/dev/urandom                                                        

Dedupe histogram:
dup            blocks         perc          MiB
0               32768        25.00       512.00
1               32768        25.00       512.00
2               32768        25.00       512.00
4               32768        25.00       512.00
Total:         131072       100.00      2048.00

Compression Histogram (XtremIO X2): 
size          buckets         perc       blocks          MiB
1                3360         5.86          210         3.28
2                3670         6.40          459         7.17
3                3526         6.15          662        10.34
4                3601         6.28          901        14.08
5                3629         6.33         1135        17.73
6                3621         6.31         1358        21.22
7                3498         6.10         1531        23.92
8                3474         6.06         1737        27.14
9                3530         6.16         1986        31.03
10               3582         6.25         2239        34.98
11               3582         6.25         2463        38.48
12               3533         6.16         2650        41.41
13               3651         6.37         2967        46.36
15               7319        12.76         6862       107.22
16               3768         6.57         3768        58.88
Total:          57344       100.00        30928       483.25
.fi
.P
.B Explanation
.P
.B File\ list
shows info on the files that were scanned.
.P
.B Dedupe\ histogram
.P
shows the distribution of duplicate counts of the scanned blocks. The first row (0) is a special case and shows
how many blocks were blank (zeroed). Each other row shows dupcount (how many copies of each block were found), the amount of blocks,
the percentage (from all scanned blocks), and Mibibytes (after dedupe). For example, the row with dupcount 4 has 32768 blocks
which means qdda found 4 blocks to be the same (dupcount 4), 4 more blocks being the same and so on with a total of 32768 blocks 
(8192 sets of 4 similar blocks each).
The row with dup=1 means these are unique blocks in the dataset. A very high dupcount usually is the result of some special blocks such as 
filled with ones (0xFFFFFFFF...) or other common data structures such as metadata or padding blocks.
In our reference test set the dupcounts are distributed evenly.
.P
.B Compression\ histogram
.P
qdda will calculate the compressed size for each (deduped) block and sort it into 1KiB multiples. Then it will sort the amounts into
the defined bucket sizes for the array. For example XtremIO X1 has bucket sizes 2K,4K, 8K. A block with a compressed size between 1 and
2048 bytes will go into the 2K bucket, sizes between 2049 and 4096 will go into bucket 4K and everything else into 8K.
.br
The compression histogram shows the distribution of bucket sizes. In this case for XtremIO X2 it shows that 3360 blocks were compressed
into 1K buckets. The array has a blocksize of 16K so in order to store 3360 1K buckets we need 210 16K blocks (3360*1/16).
.br
3474 blocks were compressed into the 8K bucket, and this requires 1737 blocks to be allocated (3474*8/16).
.br
3768 blocks could not be compressed in less than 16K so these are stored 1:1.
.P
.B qdda --tophash 5
.P
Shows the 5 most common hash values in the database. Note that these are the 60-bit truncated MD5 hashes of each block.
.P
.B Example output
.br
(from a scan of a Linux bootdisk i.e. /dev/sda)
.nf
hash                 blocks    
452707908990536042   402       
110182122676868616   146       
356086100205023294   16        
918237101795036785   9         
941619753194353532   9      
.fi
.P
.B Explanation
.P
We see that 452707908990536042 is the most common hash in the database with a dupcount of 402. To find out what the contents are of a
block that has this hash value, we can scan the data again but keep the staging database with the --nomerge option
as the staging database keeps all the offsets of the block hashes (if we only scan one file). We can then query the staging database for
the offsets (will look for the 2nd most common hash, 110182122676868616):
.P
sqlite3 qdda-staging.db "select * from offsets where hash=110182122676868616 limit 2"
.br
.nf
hash                hexhash             offset      bytes     
------------------  ------------------  ----------  ----------
110182122676868616  0x0187720e8ac0d608  181         2965504   
110182122676868616  0x0187720e8ac0d608  182         2981888   
.fi
.P
We see that the hash appears on block offsets 181 and 182 (and 144 more but we limit the query to the first 2).
We can hexdump the contents of this particular block to see what's in there:
.P
dd bs=16K status=none count=1 if=/dev/sda skip=181 | hexdump -Cv|head
.P
.nf
00000000  ff ff ff ff ff ff ff ff  ff ff ff ff ff ff ff ff  |................|
00000010  ff ff ff ff ff ff ff ff  ff ff ff ff ff ff ff ff  |................|
00000020  ff ff ff ff ff ff ff ff  ff ff ff ff ff ff ff ff  |................|
00000030  ff ff ff ff ff ff ff ff  ff ff ff ff ff ff ff ff  |................|
etc...
.fi
We can verify the MD5 hash:
.P
dd bs=16K status=none count=1 if=/dev/sda skip=181 | md5sum 
.P
92ab673d915a94dcf187720e8ac0d608  -
                 |-------------| --> Note that the last 15 hex digits (equal to 60 bits) match the hexadecimal hash value in the database.


.SH RESOURCE REQUIREMENTS
.B Storage capacity
.P
During scans, data is stored in a SQLite staging database table with 2 columns (hash and bytes).
The hash is usually a large integer requiring 8 bytes, bytes is another int which is usually 2 bytes (sometimes 3 when
using block sizes larger than 64K). The database also stores the rowid which is up to 4 bytes and a b-tree iternal index which usually
gets about 7 bytes per row.
.br
So the amount of bytes per row equals b-tree(7) + rowid(4) + hash(8) + val (2) = 21
.br
Scanning a terabyte disk at 16K blocksize requires 67,108,864 rows. The database capacity required for
the staging table is then 67108864 * 21 = 1,409,286,144 bytes = 1344 MiB (not including a little bit extra capacity for SQLite internals).
.br
So at 16K blocksize the database capacity for scanning is roughly 0.11% of the data size.
.P
After scanning the primary database will be updated from the staging database (merge process). During merge the required capacity is double
the size or 0.22% for both databases, however, SQLite also creates hidden temporary tables which require another 0.22%.
.P
Summary for a 1TiB random dataset:
.br
Primary database - 1400MiB (will be smaller if the data has zero blocks or can be deduped)
.br
Staging database - 1400MiB (deleted after merging)
.br
Temp tables      - 2800MiB (hidden, deleted after merging)
.br
Total            - 5600MiB (file system free space required, or 0.56%)
.P
A (very) safe assumption for reserved space for qdda is 1% of data size for a blocksize of 16kb.
.br
After merging the data, the staging database is deleted and the database size is about 0.12% of the original data size (at 16K blocksize).
.P
.B Default SQLite storage locations:
.br
Primary database: $HOME/qdda.db
.br
Staging database: $HOME/qdda-staging.db
.br
Temp storage: /var/tmp
.P
Note that you may change the locations of primary and staging database (--db <file> option) and for the hidden temp tables
(--tmpdir <path> option or by setting SQLITE_TMPDIR).
.P
.B I/O requirements
.P
qdda will scan data at 200MB/s throttled using large blocks, concurrently reading all files. If throttling is disabled, qdda will scan
as fast as possible until CPU power for processing the data or writing to the database becomes the bottleneck.
.P
.B CPU and memory requirements
.P
.B qdda
starts a separate reader thread for each given file or stream (max 32), and a number of worker threads equivalent to the number of CPU cores
unless the amount of workers and readers is changed via command line options.
.br
If the amount of readers is less than the number of files, each reader will process one file at a time so some files will be on the wait
queue until another file is completed. If the amount of workers is set to less than the amount of CPU cores, hashing and compression
will be limited to those threads.
.P
.B Memory required
.P
.B qdda
allocates a number of read buffers which are 1 MiB each. The amount of buffers is set to #workers + #readers + 8. So on a
system with 8 cores reading 2 files, the amount of buffers = 2 + 8 + 8 = 18 MiB.
.br
qdda also requires additional memory for SQLite, etc. but the total required memory usually fits in less than 100MiB.

.SH EXPLANATION
How qdda works:
.P
Each stream (device, file or pipe) is scanned where each block is hashed and compressed.
.P
The results (hash,compressed_bytes) go into a staging table. At the end of processing, the
staging data is merged into the main kv table (which actually holds 3 columns: hash - blocks - compressed_bytes).
.br
The report is then generated by querying the kv table.
.P
.B Hashing:

The hash value is a 60-bit truncated MD5 sum (tradeoff between database limits, efficiency
and low chance of hash collisions when scanning very large data sets).
Although storage arrays typically use the SHA algorithm with higher number of bits, MD5 has
better performance, and a very low amount of collisions will not impact the results or cause data corruption.
See also the
.B ACCURACY
section.
.P
.B Compression:

Some All-Flash arrays use "bucket" compression to achieve high throughput, low overhead and good compression. 
qdda simulates compression uzing LZ4 compression. LZ4 has very high throughput and the compression ratios
are very close to what All-Flash Arrays can achieve.

.P
.B Bucket Compression:
If an array would store compressed blocks by just concatenation of the blocks (such as with file compression tools like ZIP or GZIP),
random access would be very poor as the overhead for finding block offsets would be very high. Also, modification of a compressed block
would cause severe fragmentation and other issues. For this reason, AFA's like XtremIO use "bucket compression". For example,
XtremIO has bucket sizes of 1K to 16K with 1K steps. Say an incoming 16K block compresses to a size of 4444 bytes. The smallest bucket
where this would fit into is the 5K bucket which means the remaining 676 bytes in the bucket are not used. This causes a slightly lower
compress ratio but vastly improves performance and reduces fragmentation and partial write issues.

.P
.B Throttling:

qdda processes a number of blocks per read IO and measures the service time. 
If the service time is too low it means the throughput is
higher than the bandwidth limit. The CPU is put to sleep for a number of microseconds to match the overall bandwidth limit.
This prevents accidentally starving IO on a production host. Disable with '-b 0' option or set different bandwidth

.P
.B Blocksize:

The default blocksize is 16KiB to match modern All flash Arrays. The block size is stored in metadata and only datasets
with matching blocksizes can be merged. qdda reports X1 compression if a blocksize of 8K is detected, X2 if
the default 16K is detected, or VMAX AFA compression for 128K blocksize. I will not report compression ratios with other
blocksizes unless you load the bucketsizes and descriptions in the bucket tables after creating the database.


.SH ACCURACY
.B Notes on hash algorithm
.P
.B qdda
uses an integer field to store the hash value in SQLite. 
An SQLite integer (used for the hash in the primary key-value table) has max 8 bytes and is
signed integer with a MAX value of 9 223 372 036 854 775 807 (2^64/2-1).
.br
The hashing algorithm of qdda is MD5 which is 128 bits - which would not fit in SQLite
integers and would be converted to another datatype (TXT or blob) resulting in poor performance.
Therefore qdda only uses the 7.5 least significant bytes (60 bits) of the MD5 hash.
The number of rows with a 50% chance of a hash collision is roughly
.br
.B rows = 0.5 + sqrt(0.25 + 2 * ln(2) * 2^bits)
.br
See 
.I https://en.wikipedia.org/wiki/Birthday_problem#Probability_table
for more info on hash collisions.
.br
A hash of (60 bits) is a tradeoff between DB space consumed, performance and
accuracy and has a 50% chance of a single collision with ~ 1.2 billion rows (At 16K blocksize this equals about 19 TB)
which is fine for datasets up to many terabytes.
A 64-bit hash would get roughly 1 collision every 77TB@16K. A collision would be a serious problem for a deduplicating storage array
but for an analysis tool a few collisions are not a serious problem so we can get away with using truncated hashes.
.P
.B Notes on compression algorithm
.P
.B qdda
uses LZ4 with default parameters for compression. Some storage arrays (including XtremIO) use a proprietary compression algorithm
usually for performance reasons or to achieve higher compression ratios. Also some arrays (such as VMAX) don't compress 
.B ALL
data but keep frequently accessed data in uncompressed storage pools.
.br
Some arrays do post-processing which also results in not all data being compressed or deduped all the time.
.br
.B qdda
currently ignores these effects and produces results for all data as if it was compressed and deduped.

.SH PERFORMANCE
.B qdda
is multi-threaded during disk scans so the read process can go as fast as possible while the worker processes handle the compression and
hashing calculations. An updater process is dedicated to update the SQLite staging database.
.br
Giving accurate numbers for performance is almost impossible due to differences in IO speed, CPU power and other factors. You can get
a rough idea of your system's capabilities by running the --cputest option which gives the estimates for a single thread:
.PP
.in +4n
.EX
*** Synthetic performance test, 1 thread ***
Initializing:          65536 blocks, 16k (1024 MiB)
Hashing:             1799584 usec,     596.66 MB/s,    36417.30 rows/s
Compressing:         2412561 usec,     445.06 MB/s,    27164.49 rows/s
DB insert:             52301 usec,   20530.04 MB/s,  1253054.38 rows/s
.EE
.in
.P
The overview shows how fast a single core can hash, compress and update a dataset of the given size (this is on an
Intel Core i5-4440 CPU @ 3.10GHz). The reference dataset is a random(ish) block of data and the numbers are an indication only. 
Note that the compress rate is inaccurate but repeatable. A real dataset is usually less random and may show higher or lower speeds.
.P
A data scan by default will allocate 1 thread per file, 1 thread for database updates and the number of worker threads equal to the
amount of cpu cures. Experience shows that the bottleneck is usually read IO bandwidth until the database updater is maxed out (on a 
fast reference system this happened at about 3000MB/s). Future versions may use multiple updater threads to avoid this bottleneck.
.P
After data scan the staging data has to be merged with the primary database. This is done by joining existing data with staging data
and running an 'insert or replace' job in SQLite. Testing the speed can be done with the --dbtest option. Output of a merge of
1TB data @16K on i5-4440 CPU @ 3.10GHz:
.PP
.in +4n
.EX
Merging 67108864 blocks (1048576 MiB) with 0 blocks (0 MiB) in 157.28 sec (426686 blocks/s, 6666 MiB/s)
.EE
.in
.P
Tuning - You may speed up I/O by altering the default database location from $HOME/qdda.db to another path with the '-d' option,
to a faster file system (such as SSD based). You can also set the SQLite TEMP dir to an alternative location with
 '--tmpdir' or setting SQLITE_TMPDIR (also helps if you run out of diskspace).
.br
You can avoid the merge (join) phase and delay it to a later moment using the "--nomerge" (no report) option. 
Ideal if you scan on a slow server with limited space and you want to do the heavy lifting on a faster host later.



.SH CONFIG FILES
None, everything is contained in the SQLite database and command line options
.SH ENVIRONMENT VARIABLES
.IP SQLITE_TMPDIR
.br
if set, is used for the temporary tables such as used for sorting and joining
.br
.IP TMPDIR
.br
if SQLITE_TMPDIR is not set, TMPDIR is used for temp tables
.SH SECURITY AND SAFETY
For added safety you may run qdda as a non-privileged user. Non-root users usually do not have access to block devices.
To run QDDA in a safe way, there are various methods (assuming the "qdda" user without root privileges) you need to provide read
access to the disk devices you need to scan.
.P
.B qdda
is safe to run even on files/devices that are in use. It opens streams read-only and cannot modify any files except 
SQLite3 database files.
It writes to a database file that needs to be either newly created or a pre-existing SQLite3 database.
It can remove the database file but ONLY if it is an SQLite3 file.
.br
Changing the group/permissions on /dev/<disk> is problematic as it either gives all users read access (chmod 664) or alters permissions
for other applications such as Oracle ASM. 
.br
The best solution I have found is to use extended ACLs on Linux:
.PP
.br
.EX
setfacl -m u:<user>:r /dev/<disk>
.EE
.PP
.br
This gives qdda read-only access without altering any of the existing ownerships/permissions. The permissions will be reset at next reboot.
You need to have ACL enabled on the root file system (holding /dev/) and the setfacl tool installed (RPM package acl).

.SH OTHER PLATFORMS
.B qdda
currently only runs on 64-bit linux. Disks from other platforms can be processed by using qdda over a named or unnamed pipe.
.br
You can do this using netcat (nc)

target host: (as qdda)
.br
nc -l 19000 | qdda
.br
source host: (as root) 
.br
cat /dev/<disk> | nc targethost 19000
.P

.SH KNOWN ISSUES
Database journaling and synchronous mode are disabled for performance reasons. This means the database may be corrupted if qdda is ended
in an abnormal way (killed, file system full, etc).
.br
Accessing the SQLite database requires recent versions of the sqlite3 tools. Older versions are not compatible with the database
schema and abort with an error upon opening.
.SH SEE ALSO
lz4(1), md5(1), sqlite3(1), mkfifo(1), nc(1)
.SH AUTHOR
Written by Bart Sjerps \fIhttp://bartsjerps.wordpress.com\fR
.br
If you have suggestions for improvements in this tool, please send them
along via the above address.
.SH COPYRIGHT
Copyright © 2018 Bart Sjerps,  License GPLv3+: GNU GPL version 3 or later <http://gnu.org/licenses/gpl.html>.
.br
This is free software: you are free to change and redistribute it.  There is NO WARRANTY, to the extent permitted by law.

.SH DISCLAIMER
This software is provided "as is" and follows the licensing and warranty guidelines 
of the GPL. In normal language that means I will not be held 
responsible for any problems you may encounter with this software.
)";

const char* qdda_longhelp = R"(

Adding new streams/files to an existing database:

Use the "-a" (append) option to avoid overwriting (deleting) the existing database.

Custom queries:

The database is standard SQLite and if you want to run your own queries, you can. Just type "sqlite /var/tmp/qdda.db" and you can run any SQLite
statement you like.

Troubleshooting:

If you don't get the dedupe results you expect, make sure you have the correct block alignment. Especially with (unnamed) pipes this can be tricky.
If blocks are shifted by just one byte, then all hashes will be different and analyzing two streams will result zero deduplication effect.

)";


