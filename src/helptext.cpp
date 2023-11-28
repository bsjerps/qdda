/*******************************************************************************
 * Title       : helptext.cpp
 * Description : C++ manpage and help text definitions for qdda
 * Author      : Bart Sjerps <bart@dirty-cache.com>
 * License     : GPLv3+
 * Disclaimer  : See https://www.gnu.org/licenses/gpl-3.0.txt
 * More info   : https://wiki.dirty-cache.com/qdda
 * -----------------------------------------------------------------------------
 * Revision history:
 * 1.0   - First published version
 * 1.2.1 - changed mhash crc32 to zlib crc32 (get rid of mhash lib dependency)
 *         changed zlib to lz4 compression (much better performance)
 *         now analyze all blocks for compression
 *         added thin ratio and other output changes
 * 1.3.0 - added All flash array aware compression (buckets), variable blocksize
 * 1.4.1 - Various improvements
 * 1.5.1 - Major performance increase when processing large amounts of data
 * 1.6.0 - Import function, file list, purge always after scanning, histograms
 * 1.7.0 - Change to MD5, performance improvements, tmpdir, improved reports
 * 1.7.1 - Minor bugfixes, improved long report, improved progress indicator,
 *         -a (append) option replaces -k (keep)
 * 1.7.2 - Fix rounding error, minor output changes
 * 1.7.3 - Replace openssl with compiled-in MD5 function (no dependency
 *         on ssl libs). Increased max blocksize to 128K
 * 1.8.0 - Updated compression support, default blocksize now 16K
 * 1.8.2 - Bugfixes, updated reporting & documentation
 * 1.9.0 - Added 128K compression support, split primary and staging DB,
 *         code cleanup, minor bugfixes, experimental VMAX compression
 * 2.0.0 - Multithreading and rewrite
 * 2.0.1 - Bugfix max filesize
 * 2.0.2 - Dynamic version strings
 * 2.0.3 - Various updates
 * 2.0.4 - Many improvements & fixes
 * 2.0.5 - Report blocksize fix
 * 2.0.6 - Exception handling
 * 2.0.7 - Bugfix for EOF while reading
 * 2.0.8 - Reduced max reader threads from 32 to 8
 * 2.1.0 - Added deflate (VMAX), cleaned code, compression sampling
 * 2.2.0 - Powermax, production version with updated command line, code updates
 * 2.2.1 - Minor man page update
 * 2.2.3 - Fix warning followed by core dump on later gcc versions
 * 2.2.4 - Update URLs and version
 * ---------------------------------------------------------------------------
 ******************************************************************************/

const char* manpage_head = R"(.TH qdda 1 "2019-09-23" "QDDA" "QDDA User Manual"
.SH NAME
qdda \- the quick & dirty dedupe analyzer
.SH SYNOPSIS
.B qdda <options> [FILE...]

.SH DESCRIPTION
.B qdda
checks files, data streams or block devices for duplicate blocks to estimate deduplication
efficiency on dedupe capable storage systems, using key-value stores in SQLite,
MD5 hashing and LZ4 or DEFLATE compression. It also estimates compression ratios for all-flash arrays
XtremIO X1 and X2 as well as VMAX All-flash / PowerMAX.
.SH IMPORTANT NOTES
.B qdda
can create very large database files and generate lots of read I/O and heavy CPU load. Check the
.B RESOURCE REQUIREMENTS
section before you start.
.P
The SQLite database file(s) (qdda.db) may be removed at any time using 'qdda --delete' or simply deleting the qdda.db file.
.P
For additional safety, run
.B qdda
as non-root user. See the
.B SECURITY AND SAFETY
section for details on how to do this.
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
.P
.IP XtremIO\ X1\ (--array=x1)
The first generation XtremIO with 8KB block size and compression bucket sizes 2K, 4K, 8K. As XtremIO performs all compression and dedupe
operations inline, the results of qdda for dedupe should match the array dedupe very closely. XtremIO uses a proprietary compression
algorithm which has a slightly lower compression ratio compared to LZ4, but claims to be much faster.
This means the qdda results are slightly over-optimistic. The differences are too small however to be a major issue.
.IP XtremIO\ X2\ (--array=x2)\ (default)
With the X2, the internal blocksize was increased to 16KiB and many more (15) compression bucket sizes are available: 1K up to 16K with
1K increments, where 14K is missing because in XtremIO X2 architecture it would allocate the same capacity as 15K uncompressed.
The larger block size and more variations in buckets makes XtremIO compression much more effective.
There is still a slight difference in compression ratio due to LZ4 versus the native XtremIO algorithm.
.IP VMAX\ (--array=vmax)
VMAX All-Flash
VMAX compresses/dedupes data using 128K chunks which get compressed in bucket sizes from 8K up to 128K with 8K increments.
The compression is initially performed by splitting 128K into 4 32K chunks, but if the data is cold for a while it can get
re-compressed on the full 128K block. Not all bucket sizes are available at initial configuration and
VMAX changes the compression layout dynamically. The compression algorithm in VMAX is LZS which is similar to LZ4 so
.B qdda
uses LZ4 to estimate VMAX compression.
It also can delay or avoid compression at all for up to 20% of all data to avoid overhead for hot data blocks.
As the data reduction rate is not immediately known or deterministic, qdda assumes the final state scenario where
128K blocks get fully compressed again and deduped
so the qdda result reflects the optimal end result for idle data.
.IP PowerMAX\ (--array=pmax)
PowerMAX uses DEFLATE (zlib) compression, on 128K blocks split into buckets of 8K .. 128K like VMAX.
DEFLATE achieves a higher compression ratio but at a higher CPU overhead.
.IP custom\ (--array=<custom\ definition>)
Specify a string with array=custom:<blocksize>:<size1,size2....>
.br
example: qdda --array=custom:64:8,16,32,48,64
.br
for a custom array with blocksize 64K, and buckets of 8, 16, 32, 48 and 64K
.P
The compress and hash algorithms are slightly different from these actual arrays
and the results are a (close) approximation of the real array data reduction.
Currently qdda only uses LZ4 (default) or DEFLATE (ZLIB) compression.
.P
.SH COMPRESSION
Currently qdda supports LZ4 as well as ZLIB (DEFLATE) compression.
.br
LZ4 is a very fast, lightweight compression algorithm with reasonable compression ratios. My i5-4440@3.1GHz can
compress roughly at 500MB/s per core.
DEFLATE offers higher compression ratios but at the expense of much heavier CPU load. The same i5-4440 can do
roughly 55MB/s per core.
.P
Both compression algorithms use their default compression level.
.P
For this reason, when using DEFLATE a default, random sample interval of 20 is used so that on average 1 out of 20 blocks
gets sampled. The end compression ratio is then calculated from the sampled values.
.P
You can change the default algorithm and interval using the
.B --compress
option:
.br
--compress <none|lz4|deflate>[:interval]
.br
where interval represents the average number of non-sampled vs sampled blocks i.e. an interval of 20
means on average one out of every 20 blocks gets sampled (inverse of the sample rate).
.br
When selecting 'none' no compression is done, only dedupe analysis.
.SH ERRORS
.B qdda
has basic error handling. Most errors result in simply aborting with an error message and return code.
.br
Currently aborting qdda with ctrl-c may result in corruption of the SQLite QDDA database.

.SH EXAMPLE
.TP
.B qdda -d /tmp/demo compress:128,4 compress:256,2 compress:512 zero:512
Analyze a compressible reference test data set with 128Mx4, 256Mx2, 512x1 and 512M zeroed.
.P
.B Example output
.nf
Database info (/tmp/demo.db):
database size       =        1.12 MiB
array id            =  XtremIO X2
blocksize           =          16 KiB
compression         =         lz4
sample percentage   =      100.00 %

Overview:
total               =     2048.00 MiB (    131072 blocks)
free (zero)         =      512.00 MiB (     32768 blocks)
used                =     1536.00 MiB (     98304 blocks)
dedupe savings      =      640.00 MiB (     40960 blocks)
deduped             =      896.00 MiB (     57344 blocks)
compressed          =      451.62 MiB (     46.08 %)
allocated           =      483.09 MiB (     30918 blocks)

Details:
used                =     1536.00 MiB (     98304 blocks)
unique data         =      512.00 MiB (     32768 blocks)
non-unique data     =     1024.00 MiB (     65536 blocks)
compressed raw      =      772.98 MiB (     49.67 %)
compressed net      =      451.62 MiB (     49.59 %)

Summary:
percentage used     =       75.00 %
percentage free     =       25.00 %
deduplication ratio =        1.71
compression ratio   =        1.85
thin ratio          =        1.33
combined            =        4.24
raw capacity        =     2048.00 MiB
net capacity        =      483.09 MiB
.fi
.PP
.P
.B Explanation
.IP database\ size 20
Size of the primary SQLite database on disk
.IP array\ ID
Name of array for which dedupe and compress estimates are calculated. Can be a custom string.
.IP blocksize
Blocksize on which hashes and compression sizes are calculated
.IP compression
Compression algorithm used
.IP sample percentage
Percentage of all blocks that were sampled for compression ratio. Equals 1/interval.
.IP total
Total scanned blocks
.IP free
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
.IP unique\ data
Blocks that are unique (cannot be deduped)
.IP non-unique\ data
Blocks that appear at least 2x (can be deduped)
.IP compressed\ raw
Capacity required for compressing all raw data (before dedupe)
i.e. sum of compressed size of all scanned blocks
.IP compressed\ net
Capacity required for compressing all deduped data (after dedupe)
i.e. sum of compressed size of all deduped blocks
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
1         16384       8192         128 20190204_0944      workstation:/dev/urandom
2         16384      16384         256 20190204_0944      workstation:/dev/urandom
3         16384      32768         512 20190204_0944      workstation:/dev/zero
4         16384      32768         512 20190204_0944      workstation:/dev/urandom

Dedupe histogram:
dup            blocks         perc          MiB
0               32768        25.00       512.00
1               32768        25.00       512.00
2               32768        25.00       512.00
4               32768        25.00       512.00
Total:         131072       100.00      2048.00

Compression Histogram (2):
size          buckets       RawMiB         perc       blocks                  MiB
1                3350        52.34         5.84          210                 3.28
2                3642        56.91         6.35          456                 7.12
3                3568        55.75         6.22          669                10.45
4                3648        57.00         6.36          912                14.25
5                3607        56.36         6.29         1128                17.62
6                3510        54.84         6.12         1317                20.58
7                3603        56.30         6.28         1577                24.64
8                3415        53.36         5.96         1708                26.69
9                3516        54.94         6.13         1978                30.91
10               3532        55.19         6.16         2208                34.50
11               3572        55.81         6.23         2456                38.38
12               3539        55.30         6.17         2655                41.48
13               3682        57.53         6.42         2992                46.75
15               7322       114.41        12.77         6865               107.27
16               3838        59.97         6.69         3838                59.97
Total:          57344       896.00       100.00        30969               483.89
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
The compression histogram shows the distribution of bucket sizes. In this case for XtremIO X2 it shows that 3350 blocks were compressed
into 1K buckets. The array has a blocksize of 16K so in order to store 3350 1K buckets we need 210 16K blocks (3350*1/16).
.br
3415 blocks were compressed into the 8K bucket, and this requires 1708 blocks to be allocated (3415*8/16).
.br
3838 blocks could not be compressed in less than 16K so these are stored 1:1.
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
.nf
92ab673d915a94dcf187720e8ac0d608  -
                 |-------------| --> Note that the last 15 hex digits (equal to 60 bits) match the hexadecimal hash value in the database.
.fi

.SH COMBINING MULTIPLE SCANS
By default, when scanning data, qdda deletes the existing database and creates a new one.
Using the --append option you can keep existing data
and add more file(s) to the existing database:
.P
qdda /dev/<disk1>
qdda --append /dev/<disk2>
.P
It is also possible to join 2 databases together using --import:
.P
.nf
qdda --delete
qdda --db db1 random:512
qdda --db db2 random:512
qdda --import db1.db
qdda --import db2.db
.fi
The newly created database qdda.db will contain data from both db1 and db2.
.P
The combined databases can be gathered from different servers (by copying
the qdda.db files to one central location) so this
allows one to create a data reduction analysis across multiple hosts.
.SH RESOURCE REQUIREMENTS
.B Storage capacity
.P
During scans, data is stored in a SQLite staging database table with 2 columns (hash and bytes).
The hash is usually a large integer requiring 8 bytes, bytes is another int which is usually 2 bytes (sometimes 3 when
using block sizes larger than 64K). The database also stores the rowid which is up to 4 bytes and a b-tree iternal index which usually
gets about 7 bytes per row.
.br
So the amount of bytes per row equals b-tree (7) + rowid (4) + hash (8) + val (2) = 21
.br
Scanning a terabyte disk at 16K blocksize requires 67,108,864 rows. The database capacity required for
the staging table is then 67108864 * 21 = 1,409,286,144 bytes = 1344 MiB (not including a little bit extra capacity for SQLite internals).
.br
So at 16K blocksize the database capacity for scanning is roughly 0.11% of the data size.
.P
After scanning the primary database will be updated from the staging database (merge process). During merge the required capacity is double
the size or 0.22% for both databases, however, SQLite also creates hidden temporary tables which require another 0.22%.
.P
Sizing summary for a 1TiB random dataset:
.br
.nf
Primary database - 1400MiB (will be smaller if the data has zero blocks or can be deduped)
Staging database - 1400MiB (deleted after merging)
Temp tables      - 2800MiB (hidden, deleted after merging)
Total            - 5600MiB (file system free space required, or 0.56%)
.fi
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
.B CPU
.P
.B qdda
starts a separate reader thread for each given file or stream (max 32), and a number of worker threads equivalent to the number of CPU cores
unless the amount of workers and readers is changed via command line options.
.br
If the amount of readers is less than the number of files, each reader will process one file at a time so some files will be on the wait
queue until another file is completed. If the amount of workers is set to less than the amount of CPU cores, hashing and compression
will be limited to those threads.
.P
.B Memory
.P
.B qdda
allocates a number of read buffers which are 1 MiB each. The amount of buffers is set to #workers + #readers + 32. So on a
system with 8 cores reading 2 files, the amount of buffers = 2 + 8 + 32 = 42 MiB.
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
are very close to what All-Flash Arrays can achieve. For VMAX/Powermax, DEFLATE (zlib) is used which is much slower
but achieves a higher compression rate (everything is a tradeoff).
.P
.B Bucket Compression:

If an array would store compressed blocks by just concatenation of the blocks (such as with file compression tools like ZIP or GZIP),
random access would be very poor as the overhead for finding block offsets would be very high. Also, modification of a compressed block
would cause severe fragmentation and other issues. For this reason, AFA's like XtremIO use "bucket compression". For example,
XtremIO has bucket sizes of 1K to 16K with 1K steps. Say an incoming 16K block compresses to a size of 4444 bytes. The smallest bucket
where this would fit into is the 5K bucket which means the remaining 676 bytes in the bucket are not used. This causes a slightly lower
compress ratio (16384:5120 vs 16384:4444) but vastly improves performance and reduces fragmentation and partial write issues.

.P
.B Throttling:

qdda processes a number of blocks per read IO and measures the service time.
If the service time is too low it means the throughput is
higher than the bandwidth limit. The reader is then put to sleep for a number of microseconds to match the overall bandwidth limit.
This prevents accidentally starving IO on a production host. Disable throttling with '--bandwidth 0' or set a different bandwidth.

.P
.B Blocksize:

The default blocksize is 16KiB (XtremIO X2). The block size is stored in metadata and only datasets
with matching blocksizes can be merged or combined. The maximum blocksize is currently 128K, the minimum is 1K.

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
.B rows = 0.5 + sqrt(0.25 + 2 * ln (2) * 2^bits)
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
usually for performance reasons or to achieve higher compression ratios. Also some arrays (such as VMAX) don't compress ALL
data but keep frequently accessed data in uncompressed storage pools.
.br
Some arrays do post-processing which also results in not all data being compressed or deduped all the time.
.B qdda
currently ignores these effects and produces results for all data as if it was compressed and deduped immediately (inline).

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
.nf
*** Synthetic performance test, 1 thread ***
Initializing:          65536 blocks, 16k (1024 MiB)
Hashing:          1842670         usec, 582.71     MB/s, 35565.78    rows/s
Compress DEFLATE: 32676647        usec, 32.86      MB/s, 2005.59     rows/s
Compress LZ4:     2503945         usec, 428.82     MB/s, 26173.10    rows/s
DB insert:        51219           usec, 20963.74   MB/s, 1279525.12  rows/s
.fi
.EE
.in
.P
The overview shows how fast a single core can hash, compress and update a dataset of the given size (this is on an
Intel Core i5-4440 CPU @ 3.10GHz). The reference dataset is a random(ish) block of data and the numbers are an indication only.
Note that the compress rate is inaccurate but repeatable. A real dataset is usually less random and may show higher or lower speeds.
.P
A data scan by default will allocate 1 thread per file, 1 thread for database updates and the number of worker threads equal to the
amount of cpu cures. Experience shows that the bottleneck is usually read IO bandwidth until the database updater is maxed out (on a
fast reference system this happened at about 7000MB/s). Future versions may use multiple updater threads to avoid this bottleneck.
.P
After data scan the staging data has to be merged with the primary database. This is done by joining existing data with staging data
and running an 'insert or replace' job in SQLite. Testing the speed can be done with the --dbtest option. Output of a merge of
1TB data @16K on i5-4440 CPU @ 3.10GHz:
.PP
.in +4n
.EX
.nf
Merging 67108864 blocks (1048576 MiB) with 0 blocks (0 MiB) in 157.28 sec (426686 blocks/s, 6666 MiB/s)
.fi
.EE
.in
.P
Tuning - You may speed up I/O by altering the default database location from $HOME/qdda.db to another path with the '-d' option,
to a faster file system (such as SSD based). You can also set the SQLite TEMP dir to an alternative location with '--tmpdir <dir>'
or setting SQLITE_TMPDIR (also helps if you run out of diskspace).
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
.B qdda
is safe to run even on files/devices that are in use. It opens streams read-only and by design, it cannot modify any files except
SQLite3 database files. It writes to a database file that needs to be either newly created or a pre-existing SQLite3 database.
It can remove the database file but ONLY if it is an SQLite3 file.
.P
For added safety you may run qdda as a non-privileged user. However, non-root users usually do not have read access to block devices.
To run qdda in a safe way, there are various methods you need to provide read access to the disk devices you need to scan.
.P
Changing the group/permissions using chmod on /dev/<disk> is problematic as it either gives all users read access
or alters permissions which may break other applications such as Oracle ASM.
.br
The best solution to this issue I have found is to use extended ACLs on Linux:
.PP
.br
.EX
setfacl -m u:<user>:r /dev/<disk>
.EE
.PP
.br
This gives <user> read-only access without altering any of the existing ownerships/permissions. The permissions will
typically be reset at next reboot or through udev(7).
You need to have ACL enabled on the file system containing /dev/ and the setfacl tool installed.

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

On ESXi, this worked for me. Make sure you pick the raw disk and not a partition (i.e. not ending with :1 or something similar).
You also need an open outgoing port for this, port 902 is usually open as it is reserved for vCenter.
.br
ESX host:
.br
cat /vmfs/devices/disks/t10.ATA_____Samsung_SSD_840_PRO_Series______________S1ATNSADB36601D_____ | nc db01 902
.br
Linux host (make sure to have netcat installed):
.br
nc -l 902 | qdda
.P

.SH KNOWN ISSUES
Database journaling and synchronous mode are disabled for performance reasons. This means the internal database may be corrupted if qdda is ended
in an abnormal way (killed, file system full, etc).
.br
Accessing the SQLite database directly requires recent versions of the sqlite3 tools. Older versions are not compatible with the database
schema and abort with an error upon opening.
.br
Scanning disk partitions (/dev/sdb1, /dev/sdd4 etc) or otherwise unaligned partitions may produce poor dedupe results. This is
"as designed" - we assume you know what you are doing.
.br
Dumping multiple devices to a single pipe (i.e. cat /dev/sda /dev/sdb | qdda) may result in wrong alignment as well.



.SH SEE ALSO
lz4(1), zlib(3), md5(1), sqlite3(1), mkfifo(1), nc(1), udev(7), setfacl(1)

.SH AUTHOR
Written by Bart Sjerps \fIhttp://bartsjerps.wordpress.com\fR
.br
If you have suggestions for improvements in this tool, please send them along via the above address.
.br
The source code and downloadable binaries are available from \fIhttps://github.com/bsjerps/qdda\fR

.SH COPYRIGHT
Copyright © 2018 Bart Sjerps,  License GPLv3+: GNU GPL version 3 or later <http://gnu.org/licenses/gpl.html>.
.br
This is free software: you are free to change and redistribute it.  There is NO WARRANTY, to the extent permitted by law.

.SH DISCLAIMER
This software is provided "as is" and follows the licensing and warranty guidelines
of the GPL. In normal language that means I will not be held
responsible for any problems you may encounter with this software.
)";

const char* bash_complete =
R"(#============================================================================
# Title       : qdda.bash
# Description : bash_completion file for qdda
# Author      : Bart Sjerps <bart@dirty-cache.com>
# Usage       : Copy this file to /etc/bash_completion.d/qdda.bash
# License     : GPLv3+
# ---------------------------------------------------------------------------
# To install:
# qdda --complete > /etc/bash_completion.d/qdda.bash
# (not needed if you installed via YUM/RPM)

_qdda() {
  local -a opts shortopts longopts
  local cur prev disks
  _get_comp_words_by_ref cur prev

  shortopts=(V h m d a q b x n)
  longopts+=(version help man db append delete quiet bandwidth array)
  longopts+=(compress detail dryrun purge import cputest nomerge debug queries)
  longopts+=(tmpdir workers readers findhash tophash squash bashdump complete demo)

  opts=$(printf "\x2d%s " "${shortopts[@]}")
  opts+=$(printf "\x2d\x2d%s " "${longopts[@]}")

  disks=$(printf "/dev/%s\n" $(lsblk -ndo name))

  case ${prev} in
    -V|--version)   ;;
    -h|--help)      ;;
    -m|--man)       ;;
    -d|--db)        COMPREPLY=($(compgen -f -X "!*.db" -- "${cur}")) ;;
    -a|--append)    COMPREPLY=($(compgen -W "$opts" -- ${cur})) ;;
       --delete)    COMPREPLY=($(compgen -W "-d" -- ${cur})) ;;
    -q|--quiet)     COMPREPLY=($(compgen -W "$opts" -- ${cur})) ;;
    -b|--bandwidth) COMPREPLY=($(compgen -W "0 200 1000" -- ${cur})) ;;
       --array)     COMPREPLY=($(compgen -W "x1 x2 vmax pmax list custom:blksz:buckets" -- ${cur})) ;;
       --compress)  COMPREPLY=($(compgen -W "none lz4 deflate lz4" -- ${cur})) ;;
    -x|--detail)    COMPREPLY=($(compgen -W "$opts" -- ${cur})) ;;
    -n|--dryrun)    COMPREPLY=($(compgen -W "$opts" -- ${cur})) ;;
       --purge)     ;;
       --import)    COMPREPLY=($(compgen -f -X "!*.db" -- "${cur}")) ;;
       --cputest)   ;;
       --nomerge)   COMPREPLY=($(compgen -W "$opts" -- ${cur})) ;;
       --debug)     COMPREPLY=($(compgen -W "$opts" -- ${cur})) ;;
       --queries)   COMPREPLY=($(compgen -W "$opts" -- ${cur})) ;;
       --tmpdir)    COMPREPLY=($(compgen -W "/tmp /var/tmp" -- ${cur}))  ;;
       --workers)   COMPREPLY=($(compgen -W "1 2 4 8 16 32" -- ${cur})) ;;
       --readers)   COMPREPLY=($(compgen -W "1 2 4 8 16" -- ${cur})) ;;
       --findhash)  ;;
       --tophash)   COMPREPLY=($(compgen -W "5 10 25" -- ${cur})) ;;
       --squash)    ;;
       --bashdump)  ;;
       --complete)  ;;
       --demo)      ;;
    *)              COMPREPLY=($(compgen -W "$opts $disks" -- ${cur})) ;;
  esac
  return 0
}
complete -F _qdda qdda
)";
