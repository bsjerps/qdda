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
.B qdda <options> [FILE]...
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
.P
For additional safety, run
.B qdda
as non-root user. See the 
.B SECURITY AND SAFETY
section for details on how to do this.
.P
.SH OPTIONS
)";

const char* manpage_body = R"(
.SH EXAMPLE
.TP 
.B qdda /dev/sda
scan the first SCSI disk (usually the bootdisk) and show results.
.P
.B Example output
.EX
Overview:
total               =    16384.00 MiB (   1048576 blocks)
free (zero)         =     2048.00 MiB (    131072 blocks)
used                =    14336.00 MiB (    917504 blocks)
dedupe savings      =     4096.00 MiB (    262144 blocks)
deduped             =    10240.00 MiB (    655360 blocks)
compressed          =     5124.51 MiB (     49.96 %)
allocated capacity  =     5485.09 MiB (    351046 blocks)

Details:
used                =    14336.00 MiB (    917504 blocks)
compressed raw      =     7172.75 MiB (     49.97 %)
unique data         =     7168.00 MiB (    458752 blocks)
non-unique data     =     7168.00 MiB (    458752 blocks)

Summary:
percentage used     =       87.50 %
percentage free     =       12.50 %
deduplication ratio =        1.40
compression ratio   =        1.87
thin ratio          =        1.14
combined            =        2.99
raw capacity        =    16384.00 MiB
net capacity        =     5485.09 MiB
.EE
.P
.B Explanation
.IP Total 20
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
Capacity after compressing blocks
.IP allocated
Capacity after allocating compressed blocks into buckets
.IP compressed\ raw
Capacity required for compressing all raw data (ignoring dedupe and block sizes)
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
.P
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
the size or 0.22% for both databases, however, SQLite also creates hidden temporary tables which require another 0.11%.
.P
A safe assumption for reserved space for qdda is 8% of data size divided by the blocksize in kb.
.br
So 1TiB data (1024GB) at 16K blocksize requires 1024*0.08/16 = 5.12 GB
.br
After merging the data, the staging database is deleted and the database size is about 0.12% of the original data size (at 16K blocksize).
.P
.B Default SQLite storage locations:
.br
Primary database: $HOME/qdda.db
.br
Staging database: /var/tmp/qdda-staging.db
.br
Temp storage: /var/tmp
.P
.B I/O requirements
qdda will scan data at 200MB/s throttled using large blocks, concurrently reading all files. If throttling is disabled, qdda will scan
as fast as possible unless CPU power for processing the data is limited.
.P
.B CPU and memory requirements
.P
.B qdda
starts a separate reader thread for each given file or stream, and a number of worker threads equivalent to the number of CPU cores.
.P
.B Memory required
.P
TBD

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

QDDA knows 3 bucket compression scenarios and selects bucket compression based on the blocksize used:

8K blocksize   - available buckets are 2K, 4K and 8K (simulates XtremIO v1)
16K blocksize  - available buckets are 1K through 16K with 1K increments except 15K (XtremIO v2)
128K blocksize - available buckets are 8K through 128K with 8K increments (VMAX All-Flash - experimental, unverified!)

List available methods using option '-c l'
.P
.B Throttling:

qdda processes a number of blocks per cycle (usually 64) and measures the service time. 
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

.SH CONFIG FILES
None.
.SH ENVIRONMENT VARIABLES
.IP SQLITE_TMPDIR
.br
if set, is used for the temporary tables such as used for sorting and joining
.br
.IP TMPDIR
.br
if SQLITE_TMPDIR is not set, TMPDIR is used for temp tables
.SH BEHAVIOUR
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
.br
.EX
setfacl -m u:qdda:r /dev/<disk>
.EE
.br
This gives qdda read-only access without altering any of the existing ownerships/permissions. The permissions will be reset at next reboot.
You need to have ACL enabled on the root file system (holding /dev/) and the setfacl tool installed (RPM package acl).

.SH OTHER PLATFORMS
.B qdda
currently only runs on 64-bit linux. Disks from other platforms can be processed by using qdda over a named or unnamed pipe.
.br
The simple way:

Run qdda over a network pipe:

You can do this using netcat (nc)

target host: (as qdda) nc -l 19000 | qdda
source host: (as root) cat /dev/<disk> | nc targethost 19000

.SH FILES
.SH KNOWN ISSUES
.SH SEE ALSO
lz4(1), dgst(1), sqlite3(1)
.SH AUTHOR
Compiled by Bart Sjerps - contact me via my blog "Dirty Cache" - \fIhttp://bartsjerps.wordpress.com\fR
.br
If you have suggestions for improvements in this tool, please send them
along via the above address.
.SH COPYRIGHT
This software is Open Source and may be distributed "as is", modified, reproduced
and passed to others under the GPLv3 license - \fIhttp://www.gnu.org/licenses/gpl-3.0.txt\fR
.SH DISCLAIMER
This software is provided "as is" and because it is mostly based
on CentOS linux, it follows the licensing and warranty guidelines 
of the GPL. In normal language that means I will not be held 
responsible for any problems you may encounter with this software.

)";

const char* qdda_longhelp = R"(
If you need more space than available, you can change the locations of the files:

- Primary database with -f option (choose a filename for the database)
- Staging database and hidden directories with -T option (choose a temp directory)

Performance:

QDDA is mostly single threaded. The limit for reading data is defined by the CPU time it takes to calculate hashes and compression
sizes for each block, and insert speeds. Test scan speeds with the '-t 0' option. On my Intel i5-4440 I get a synthetic 
speed of about 500MB/s. During real scanning this drops to about 350 MB/s (due to the overhead of really reading data).

Merge performance - the database internally has to count, sort and join a lot of rows (each block is a row in the staging database).
Performance depends on CPU and I/O speed. Test the merging speed with the '-t size_gb' option. On my system the merge performance
is (for 16K blocksize on empty database, 2 threads):

Size (GB) Indexing(s)  Index(MB/s) Joining(s) Join(MB/s)
        2        0.08        27170       0.16      12931
       16        0.77        21251       1.35      12099
       64        3.71        17654       5.56      11781
      128       19.81        13230      23.57      11121
     1024       53.52        19592      98.39      10657

Tuning:

You may speed up I/O by altering the default database location from /var/tmp/qdda.db to another path with '-f',
on a faster file system (such as SSD based). 
You can also set the SQLite TEMP dir to an alternative location with -T (also if you run out of diskspace).

You can avoid the merge (join/sort) phase and delay it to a later moment using the "-r" (no report) option. 
Ideal if you scan on a slow server with limited space and you want to do the heavy lifting on a faster host later.

Merging datasets:

You may want to scan data and analyze dedupe ratios across different hosts. You can scan each host separately and then later combine
the databases by using the import option (add data of 2nd database to the primary). The block sizes of both databases should match.

Adding new streams/files to an existing database:

Use the "-a" (append) option to avoid overwriting (deleting) the existing database.

Dump: if you want to find out why certain blocks will or will not duplicate against each other, you can run the Dump (-d) option.
Beware that this slows down the scan process significantly because every block hash and compressed size will be listed to the console.
Ideally you should only use this for small size test scenarios.

Extended reports:

The -x (extended reports) option provides deep insight in deduplication and compression. 

The deduplication histogram shows detailed distribution of dedupe counts i.e. how many unique blocks are in the dataset, etc.
The Compression histogram shows the detailed compress bucket allocation for a given compression method.



Custom queries:

The database is standard SQLite and if you want to run your own queries, you can. Just type "sqlite /var/tmp/qdda.db" and you can run any SQLite
statement you like.

Troubleshooting:

If you don't get the dedupe results you expect, make sure you have the correct block alignment. Especially with (unnamed) pipes this can be tricky.
If blocks are shifted by just one byte, then all hashes will be different and analyzing two streams will result zero deduplication effect.

)";


