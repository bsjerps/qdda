Content-type: text/html

<HTML><HEAD><TITLE>Manpage of qdda</TITLE>
</HEAD><BODY>
<H1>qdda</H1>
Section: QDDA User Manual (1)<BR>Updated: 2018-03-24<BR><A HREF="#index">Index</A>
<A HREF="http://localhost/cgi-bin/man/man2html">Return to Main Contents</A><HR>

<A NAME="lbAB">&nbsp;</A>
<H2>NAME</H2>

qdda - the quick &amp; dirty dedupe analyzer
<A NAME="lbAC">&nbsp;</A>
<H2>SYNOPSIS</H2>

<B>qdda &lt;options&gt; [FILE...]</B>

<P>
<A NAME="lbAD">&nbsp;</A>
<H2>DESCRIPTION</H2>

<B>qdda</B>

checks files, data streams or block devices for duplicate blocks to estimate deduplication
efficiency on dedupe capable storage systems, using key-value stores in SQLite,
MD5 hashing and LZ4 compression.  It also estimates compression ratios for all-flash arrays
XtremIO X1 and X2 as well as VMAX AFA (experimental).

<P>
<A NAME="lbAE">&nbsp;</A>
<H2>IMPORTANT NOTES</H2>

<B>qdda</B>

can create very large database files and generate lots of read I/O and heavy CPU load. Check the 
<B>RESOURCE REQUIREMENTS</B>

section before you start.
<BR>

For additional safety, run
<B>qdda</B>

as non-root user. See the 
<B>SECURITY AND SAFETY</B>

section for details on how to do this.

<A NAME="lbAF">&nbsp;</A>
<H2>OPTIONS</H2>

<DL COMPACT>
<DT><B>-V, --version </B>

<DD>
show version and copyright info
<DT><B>-h, --help </B>

<DD>
show usage
<DT><B>-m, --man </B>

<DD>
show detailed manpage
<DT><B>-d, --db &lt;file&gt;</B>

<DD>
database file path (default $HOME/qdda.db)
<DT><B>-a, --append </B>

<DD>
Append data instead of deleting database
<DT><B>--delete </B>

<DD>
Delete database
<DT><B>-q, --quiet </B>

<DD>
Don't show progress indicator or intermediate results
<DT><B>-b, --bandwidth &lt;mb/s&gt;</B>

<DD>
Throttle bandwidth in MB/s (default 200, 0=disable)
<DT><B>--array &lt;id|def&gt;</B>

<DD>
set array type or custom definition &lt;x1|x2|vmax1|definition&gt;
<DT><B>-l, --list </B>

<DD>
list supported array types and custom definition options
<DT><B>-x, --detail </B>

<DD>
Detailed report (file info and dedupe/compression histograms)
<DT><B>-n, --dryrun </B>

<DD>
skip staging db updates during scan
<DT><B>--purge </B>

<DD>
Reclaim unused space in database (sqlite vacuum)
<DT><B>--import &lt;file&gt;</B>

<DD>
import another database (must have compatible metadata)
<DT><B>--cputest </B>

<DD>
Single thread CPU performance test
<DT><B>--nomerge </B>

<DD>
Skip staging data merge and reporting, keep staging database
<DT><B>--debug </B>

<DD>
Enable debug output
<DT><B>--queries </B>

<DD>
Show SQLite queries and results
<DT><B>--tmpdir &lt;dir&gt;</B>

<DD>
Set $SQLITE_TMPDIR for temporary files
<DT><B>--workers &lt;wthreads&gt;</B>

<DD>
number of worker threads
<DT><B>--readers &lt;rthreads&gt;</B>

<DD>
(max) number of reader threads
<DT><B>--findhash &lt;hash&gt;</B>

<DD>
find blocks with hash=&lt;hash&gt; in staging db
<DT><B>--tophash &lt;num&gt;</B>

<DD>
show top &lt;num&gt; hashes by refcount
<DT><B>--mandump </B>

<DD>
dump raw manpage to stdout

<P>

<B>FILE</B>

description:

FILE is usually a disk device such as /dev/sda but can also be a flat file, a named pipe, a disk partition or anything else that can be
read as a stream. It will also read from stdin if it is not connected to a tty, i.e. you can do 'cat &lt;file&gt; | qdda '.

<B>Modifiers</B>


Each file can have a modifier by adding a colon (:). Currently the modifier is in the format &lt;maxmb[,dup]&gt; where maxmb
is the maximum amount of mibibytes to read from the stream: /dev/sda:1024 will read the first 1024MiB and then stop. dup is the
number of times the data is processed (only for testing purposes) i.e. /dev/sda:1024,2 will generate 2048 MiB with dupcount=2 
(the first 1024 MiB of sda but processed twice).

<B>Special&nbsp;filenames</B>


Special filenames are zero (alias for /dev/zero), random (alias for /dev/urandom) and compress (same as random but make the
data compressible). This allows you to generate test data.

<B>Test data example</B>


qdda zero:512 random:512 random:256,2 compress:128,4
<BR>

Generates a test dataset with 512MiB zeroed, 512MiB uncompressible, unique data, 256MiB uncompressible data used twice, and 128MiB 
compressible data with 4 copies each.

<P>
</DL>
<A NAME="lbAG">&nbsp;</A>
<H2>STORAGE ARRAYS</H2>

Currently qdda supports 3 storage arrays:
<BR>

<DL COMPACT>
<DT>XtremIO&nbsp;X1&nbsp;(--array=x1)<DD>
The first generation XtremIO with 8KB block size and compression bucket sizes 2K, 4K, 8K. As XtremIO performs all compression and dedupe
operations inline, the results of qdda for dedupe should match the array dedupe very closely. XtremIO uses a proprietary compression
algorithm which has a slightly lower compression ratio compared to LZ4, but claims to be much faster. 
This means the qdda results are slightly over-optimistic. The differences are too small however to be a major issue.
<DT>XtremIO&nbsp;X2&nbsp;(--array=x2)&nbsp;(default)<DD>
With the X2, the internal blocksize was increased to 16KiB and many more (15) compression bucket sizes are available: 1K up to 16K with
1K increments, where 15K is missing because in XtremIO X2 architecture it would allocate the same capacity as 16K uncompressed. 
The larger block size and more variations in buckets makes XtremIO compression much more effective. 
There is still a slight difference in compression ratio due to LZ4 versus the native XtremIO algorithm.
<DT>VMAX&nbsp;All-Flash&nbsp;(--array=vmax1)<DD>
VMAX data reduction estimates are currently 
<B>EXPERIMENTAL.</B>

<BR>

VMAX compresses data using 128K chunks which get compressed in bucket sizes from 8K up to 128K with 8K increments. Not all bucket sizes are
available at initial configuration and VMAX changes the compression layout dynamically and also avoids compression
for up to 20% of all data, based on workload and data profile. This makes it hard for qdda to give reasonable estimates which is why
it is marked as EXPERIMENTAL for now. The output of qdda shows the results at most ideal circumstances (all data gets deduped and
compressed always). Future versions may improve on accuracy.

The compress and hash algorithms are slightly different from these actual arrays
and the results are a (close) approximation of the real array data reduction.
Currently qdda only uses LZ4 (default) compression,

<DT>custom&nbsp;(--array=&lt;custom&nbsp;definition&gt;)<DD>
Specify a string with array=name=&lt;name&gt;,bs=&lt;blocksize_kb&gt;,buckets=&lt;bucketsize1+bucketsize2....&gt;
<BR>

example: qdda --array=name=foo,bs=64,buckets=8+16+32+48+64
</DL>
<A NAME="lbAH">&nbsp;</A>
<H2>ERRORS</H2>

<B>qdda</B>

has basic error handling. Most errors result in simply aborting with an error message and return code.
<P>
<A NAME="lbAI">&nbsp;</A>
<H2>EXAMPLE</H2>

<DL COMPACT>
<DT><B>qdda compress:128,4 compress:256,2 compress:512 zero:512</B>

<DD>
Analyze a compressible reference test data set with 128Mx4, 256Mx2, 512x1 and 512M zeroed.

<B>Example output</B>

<PRE>
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
</PRE>

</DL>
<P>


<B>Explanation</B>

<DL COMPACT>
<DT>Database&nbsp;size<DD>
Size of the primary SQLite databse on disk
<DT>Array&nbsp;ID<DD>
Name of array for which dedupe and compress estimates are calculated. Can be a custom string.
<DT>Blocksize<DD>
Blocksize on which hashes and compression sizes are calculated
<DT>Total<DD>
Total scanned blocks
<DT>Free<DD>
Free (zero) blocks
<DT>used<DD>
Used (non-zero) blocks
<DT>dedupe&nbsp;savings<DD>
Blocks saved by merging duplicate blocks
<DT>deduped<DD>
Blocks required after dedupe
<DT>compressed<DD>
Capacity after compressing (deduped) blocks i.e.sum of compressed size of all blocks after dedupe
<DT>allocated<DD>
Capacity after allocating compressed blocks into buckets. This is the required capacity on an inline dedupe/compress capable storage array
<DT>compressed&nbsp;raw<DD>
Capacity required for compressing all raw data (before dedupe) i.e. sum of compressed size of all scanned 
blocks
<DT>unique&nbsp;data<DD>
Blocks that are unique (cannot be deduped)
<DT>non-unique&nbsp;data<DD>
Blocks that appear at least 2x (can be deduped)
<DT>percentage&nbsp;used<DD>
Percentage of all raw blocks that are non-zero
<DT>percentage&nbsp;free<DD>
Percentage of all raw blocks that are zero
<DT>deduplication&nbsp;ratio<DD>
capacity used divided by deduped
<DT>compression&nbsp;ratio<DD>
capacity deduped divided by allocated
<DT>thin&nbsp;ratio<DD>
capacity used divided by total
<DT>combined<DD>
Overall data reduction (dedupe ratio * compress ratio * thin ratio)
<DT>raw&nbsp;capacity<DD>
equal to total
<DT>net&nbsp;capacity<DD>
equal to allocated

<DT><B>qdda --detail</B>

<DD>
Show detailed histograms from the database

<B>Example output</B>

<PRE>
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
</PRE>


<B>Explanation</B>


<B>File&nbsp;list</B>

shows info on the files that were scanned.

<B>Dedupe&nbsp;histogram</B>


shows the distribution of duplicate counts of the scanned blocks. The first row (0) is a special case and shows
how many blocks were blank (zeroed). Each other row shows dupcount (how many copies of each block were found), the amount of blocks,
the percentage (from all scanned blocks), and Mibibytes (after dedupe). For example, the row with dupcount 4 has 32768 blocks
which means qdda found 4 blocks to be the same (dupcount 4), 4 more blocks being the same and so on with a total of 32768 blocks 
(8192 sets of 4 similar blocks each).
The row with dup=1 means these are unique blocks in the dataset. A very high dupcount usually is the result of some special blocks such as 
filled with ones (0xFFFFFFFF...) or other common data structures such as metadata or padding blocks.
In our reference test set the dupcounts are distributed evenly.

<B>Compression&nbsp;histogram</B>


qdda will calculate the compressed size for each (deduped) block and sort it into 1KiB multiples. Then it will sort the amounts into
the defined bucket sizes for the array. For example XtremIO X1 has bucket sizes 2K,4K, 8K. A block with a compressed size between 1 and
2048 bytes will go into the 2K bucket, sizes between 2049 and 4096 will go into bucket 4K and everything else into 8K.
<BR>

The compression histogram shows the distribution of bucket sizes. In this case for XtremIO X2 it shows that 3360 blocks were compressed
into 1K buckets. The array has a blocksize of 16K so in order to store 3360 1K buckets we need 210 16K blocks (3360*1/16).
<BR>

3474 blocks were compressed into the 8K bucket, and this requires 1737 blocks to be allocated (3474*8/16).
<BR>

3768 blocks could not be compressed in less than 16K so these are stored 1:1.

<B>qdda --tophash 5</B>


Shows the 5 most common hash values in the database. Note that these are the 60-bit truncated MD5 hashes of each block.

<B>Example output</B>

<BR>

(from a scan of a Linux bootdisk i.e. /dev/sda)
<PRE>
hash                 blocks    
452707908990536042   402       
110182122676868616   146       
356086100205023294   16        
918237101795036785   9         
941619753194353532   9      
</PRE>


<B>Explanation</B>


We see that 452707908990536042 is the most common hash in the database with a dupcount of 402. To find out what the contents are of a
block that has this hash value, we can scan the data again but keep the staging database with the --nomerge option
as the staging database keeps all the offsets of the block hashes (if we only scan one file). We can then query the staging database for
the offsets (will look for the 2nd most common hash, 110182122676868616):

sqlite3 qdda-staging.db &quot;select * from offsets where hash=110182122676868616 limit 2&quot;
<BR>

<PRE>
hash                hexhash             offset      bytes     
------------------  ------------------  ----------  ----------
110182122676868616  0x0187720e8ac0d608  181         2965504   
110182122676868616  0x0187720e8ac0d608  182         2981888   
</PRE>


We see that the hash appears on block offsets 181 and 182 (and 144 more but we limit the query to the first 2).
We can hexdump the contents of this particular block to see what's in there:

dd bs=16K status=none count=1 if=/dev/sda skip=181 | hexdump -Cv|head

<PRE>
00000000  ff ff ff ff ff ff ff ff  ff ff ff ff ff ff ff ff  |................|
00000010  ff ff ff ff ff ff ff ff  ff ff ff ff ff ff ff ff  |................|
00000020  ff ff ff ff ff ff ff ff  ff ff ff ff ff ff ff ff  |................|
00000030  ff ff ff ff ff ff ff ff  ff ff ff ff ff ff ff ff  |................|
etc...
</PRE>

We can verify the MD5 hash:

dd bs=16K status=none count=1 if=/dev/sda skip=181 | md5sum 

92ab673d915a94dcf187720e8ac0d608  -
<BR>&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;|-------------|&nbsp;--&gt;&nbsp;Note&nbsp;that&nbsp;the&nbsp;last&nbsp;15&nbsp;hex&nbsp;digits&nbsp;(equal&nbsp;to&nbsp;60&nbsp;bits)&nbsp;match&nbsp;the&nbsp;hexadecimal&nbsp;hash&nbsp;value&nbsp;in&nbsp;the&nbsp;database.
<P>
</DL>
<A NAME="lbAJ">&nbsp;</A>
<H2>COMBINING MULTIPLE SCANS</H2>

By default, when scanning data, qdda deletes the existing database and creates a new one. Using the --append option you can
keep existing data and add more file(s) to the existing database:

qdda /dev/&lt;disk1&gt;
qdda --append /dev/&lt;disk2&gt;

It is also possible to join 2 databases together using --import:

<PRE>
qdda --delete
qdda --db db1 random:512
qdda --db db2 random:512
qdda --import db1.db
</PRE>

<P>
<A NAME="lbAK">&nbsp;</A>
<H2>RESOURCE REQUIREMENTS</H2>

<B>Storage capacity</B>


During scans, data is stored in a SQLite staging database table with 2 columns (hash and bytes).
The hash is usually a large integer requiring 8 bytes, bytes is another int which is usually 2 bytes (sometimes 3 when
using block sizes larger than 64K). The database also stores the rowid which is up to 4 bytes and a b-tree iternal index which usually
gets about 7 bytes per row.
<BR>

So the amount of bytes per row equals <A HREF="http://localhost/cgi-bin/man/man2html?7+b-tree">b-tree</A>(7) + <A HREF="http://localhost/cgi-bin/man/man2html?4+rowid">rowid</A>(4) + <A HREF="http://localhost/cgi-bin/man/man2html?8+hash">hash</A>(8) + val (2) = 21
<BR>

Scanning a terabyte disk at 16K blocksize requires 67,108,864 rows. The database capacity required for
the staging table is then 67108864 * 21 = 1,409,286,144 bytes = 1344 MiB (not including a little bit extra capacity for SQLite internals).
<BR>

So at 16K blocksize the database capacity for scanning is roughly 0.11% of the data size.

After scanning the primary database will be updated from the staging database (merge process). During merge the required capacity is double
the size or 0.22% for both databases, however, SQLite also creates hidden temporary tables which require another 0.22%.

Sizing summary for a 1TiB random dataset:
<BR>

Primary database - 1400MiB (will be smaller if the data has zero blocks or can be deduped)
<BR>

Staging database - 1400MiB (deleted after merging)
<BR>

Temp tables      - 2800MiB (hidden, deleted after merging)
<BR>

Total            - 5600MiB (file system free space required, or 0.56%)

A (very) safe assumption for reserved space for qdda is 1% of data size for a blocksize of 16kb.
<BR>

After merging the data, the staging database is deleted and the database size is about 0.12% of the original data size (at 16K blocksize).

<B>Default SQLite storage locations:</B>

<BR>

Primary database: $HOME/qdda.db
<BR>

Staging database: $HOME/qdda-staging.db
<BR>

Temp storage: /var/tmp

Note that you may change the locations of primary and staging database (--db &lt;file&gt; option) and for the hidden temp tables
(--tmpdir &lt;path&gt; option or by setting SQLITE_TMPDIR).

<B>I/O requirements</B>


qdda will scan data at 200MB/s throttled using large blocks, concurrently reading all files. If throttling is disabled, qdda will scan
as fast as possible until CPU power for processing the data or writing to the database becomes the bottleneck.

<B>CPU</B>


<B>qdda</B>

starts a separate reader thread for each given file or stream (max 32), and a number of worker threads equivalent to the number of CPU cores
unless the amount of workers and readers is changed via command line options.
<BR>

If the amount of readers is less than the number of files, each reader will process one file at a time so some files will be on the wait
queue until another file is completed. If the amount of workers is set to less than the amount of CPU cores, hashing and compression
will be limited to those threads.

<B>Memory</B>


<B>qdda</B>

allocates a number of read buffers which are 1 MiB each. The amount of buffers is set to #workers + #readers + 8. So on a
system with 8 cores reading 2 files, the amount of buffers = 2 + 8 + 8 = 18 MiB.
<BR>

qdda also requires additional memory for SQLite, etc. but the total required memory usually fits in less than 100MiB.
<P>
<A NAME="lbAL">&nbsp;</A>
<H2>EXPLANATION</H2>

How qdda works:

Each stream (device, file or pipe) is scanned where each block is hashed and compressed.

The results (hash,compressed_bytes) go into a staging table. At the end of processing, the
staging data is merged into the main kv table (which actually holds 3 columns: hash - blocks - compressed_bytes).
<BR>

The report is then generated by querying the kv table.

<B>Hashing:</B>

<P>
The hash value is a 60-bit truncated MD5 sum (tradeoff between database limits, efficiency
and low chance of hash collisions when scanning very large data sets).
Although storage arrays typically use the SHA algorithm with higher number of bits, MD5 has
better performance, and a very low amount of collisions will not impact the results or cause data corruption.
See also the
<B>ACCURACY</B>

section.

<B>Compression:</B>

<P>
Some All-Flash arrays use &quot;bucket&quot; compression to achieve high throughput, low overhead and good compression. 
qdda simulates compression uzing LZ4 compression. LZ4 has very high throughput and the compression ratios
are very close to what All-Flash Arrays can achieve.
<P>

<B>Bucket Compression:</B>

If an array would store compressed blocks by just concatenation of the blocks (such as with file compression tools like ZIP or GZIP),
random access would be very poor as the overhead for finding block offsets would be very high. Also, modification of a compressed block
would cause severe fragmentation and other issues. For this reason, AFA's like XtremIO use &quot;bucket compression&quot;. For example,
XtremIO has bucket sizes of 1K to 16K with 1K steps. Say an incoming 16K block compresses to a size of 4444 bytes. The smallest bucket
where this would fit into is the 5K bucket which means the remaining 676 bytes in the bucket are not used. This causes a slightly lower
compress ratio but vastly improves performance and reduces fragmentation and partial write issues.
<P>

<B>Throttling:</B>

<P>
qdda processes a number of blocks per read IO and measures the service time. 
If the service time is too low it means the throughput is
higher than the bandwidth limit. The reader is then put to sleep for a number of microseconds to match the overall bandwidth limit.
This prevents accidentally starving IO on a production host. Disable throttling with '--bandwidth 0' or set a different bandwidth.
<P>

<B>Blocksize:</B>

<P>
The default blocksize is 16KiB (XtremIO X2). The block size is stored in metadata and only datasets
with matching blocksizes can be merged or combined. The maximum blocksize is currently 128K, the minimum is 1K.
<P>
<A NAME="lbAM">&nbsp;</A>
<H2>ACCURACY</H2>

<B>Notes on hash algorithm</B>


<B>qdda</B>

uses an integer field to store the hash value in SQLite. 
An SQLite integer (used for the hash in the primary key-value table) has max 8 bytes and is
signed integer with a MAX value of 9 223 372 036 854 775 807 (2^64/2-1).
<BR>

The hashing algorithm of qdda is MD5 which is 128 bits - which would not fit in SQLite
integers and would be converted to another datatype (TXT or blob) resulting in poor performance.
Therefore qdda only uses the 7.5 least significant bytes (60 bits) of the MD5 hash.
The number of rows with a 50% chance of a hash collision is roughly
<BR>

<B>rows = 0.5 + sqrt(0.25 + 2 * <A HREF="http://localhost/cgi-bin/man/man2html?2+ln">ln</A>(2) * 2^bits)</B>

<BR>

See 
<I><A HREF="https://en.wikipedia.org/wiki/Birthday_problem#Probability_table">https://en.wikipedia.org/wiki/Birthday_problem#Probability_table</A></I>

for more info on hash collisions.
<BR>

A hash of (60 bits) is a tradeoff between DB space consumed, performance and
accuracy and has a 50% chance of a single collision with ~ 1.2 billion rows (At 16K blocksize this equals about 19 TB)
which is fine for datasets up to many terabytes.
A 64-bit hash would get roughly 1 collision every 77TB@16K. A collision would be a serious problem for a deduplicating storage array
but for an analysis tool a few collisions are not a serious problem so we can get away with using truncated hashes.

<B>Notes on compression algorithm</B>


<B>qdda</B>

uses LZ4 with default parameters for compression. Some storage arrays (including XtremIO) use a proprietary compression algorithm
usually for performance reasons or to achieve higher compression ratios. Also some arrays (such as VMAX) don't compress ALL
data but keep frequently accessed data in uncompressed storage pools.
<BR>

Some arrays do post-processing which also results in not all data being compressed or deduped all the time.
<BR>

<B>qdda</B>

currently ignores these effects and produces results for all data as if it was compressed and deduped immediately (inline).
<P>
<A NAME="lbAN">&nbsp;</A>
<H2>PERFORMANCE</H2>

<B>qdda</B>

is multi-threaded during disk scans so the read process can go as fast as possible while the worker processes handle the compression and
hashing calculations. An updater process is dedicated to update the SQLite staging database.
<BR>

Giving accurate numbers for performance is almost impossible due to differences in IO speed, CPU power and other factors. You can get
a rough idea of your system's capabilities by running the --cputest option which gives the estimates for a single thread:
<P>



*** Synthetic performance test, 1 thread ***
Initializing:          65536 blocks, 16k (1024 MiB)
Hashing:             1799584 usec,     596.66 MB/s,    36417.30 rows/s
Compressing:         2412561 usec,     445.06 MB/s,    27164.49 rows/s
DB insert:             52301 usec,   20530.04 MB/s,  1253054.38 rows/s



The overview shows how fast a single core can hash, compress and update a dataset of the given size (this is on an
Intel Core i5-4440 CPU @ 3.10GHz). The reference dataset is a random(ish) block of data and the numbers are an indication only. 
Note that the compress rate is inaccurate but repeatable. A real dataset is usually less random and may show higher or lower speeds.

A data scan by default will allocate 1 thread per file, 1 thread for database updates and the number of worker threads equal to the
amount of cpu cures. Experience shows that the bottleneck is usually read IO bandwidth until the database updater is maxed out (on a 
fast reference system this happened at about 3000MB/s). Future versions may use multiple updater threads to avoid this bottleneck.

After data scan the staging data has to be merged with the primary database. This is done by joining existing data with staging data
and running an 'insert or replace' job in SQLite. Testing the speed can be done with the --dbtest option. Output of a merge of
1TB data @16K on i5-4440 CPU @ 3.10GHz:
<P>



Merging 67108864 blocks (1048576 MiB) with 0 blocks (0 MiB) in 157.28 sec (426686 blocks/s, 6666 MiB/s)



Tuning - You may speed up I/O by altering the default database location from $HOME/qdda.db to another path with the '-d' option,
to a faster file system (such as SSD based). You can also set the SQLite TEMP dir to an alternative location with '--tmpdir &lt;dir&gt;'
or setting SQLITE_TMPDIR (also helps if you run out of diskspace).
<BR>

You can avoid the merge (join) phase and delay it to a later moment using the &quot;--nomerge&quot; (no report) option. 
Ideal if you scan on a slow server with limited space and you want to do the heavy lifting on a faster host later.
<P>
<A NAME="lbAO">&nbsp;</A>
<H2>CONFIG FILES</H2>

None, everything is contained in the SQLite database and command line options
<A NAME="lbAP">&nbsp;</A>
<H2>ENVIRONMENT VARIABLES</H2>

<DL COMPACT>
<DT>SQLITE_TMPDIR<DD>
<BR>

if set, is used for the temporary tables such as used for sorting and joining
<BR>

<DT>TMPDIR<DD>
<BR>

if SQLITE_TMPDIR is not set, TMPDIR is used for temp tables
<P>
</DL>
<A NAME="lbAQ">&nbsp;</A>
<H2>SECURITY AND SAFETY</H2>

<B>qdda</B>

is safe to run even on files/devices that are in use. It opens streams read-only and by design, it cannot modify any files except 
SQLite3 database files. It writes to a database file that needs to be either newly created or a pre-existing SQLite3 database.
It can remove the database file but ONLY if it is an SQLite3 file.

For added safety you may run qdda as a non-privileged user. However, non-root users usually do not have read access to block devices.
To run qdda in a safe way, there are various methods you need to provide read access to the disk devices you need to scan.

Changing the group/permissions using chmod on /dev/&lt;disk&gt; is problematic as it either gives all users read access
or alters permissions which may break other applications such as Oracle ASM. 
<BR>

The best solution to this issue I have found is to use extended ACLs on Linux:
<P>

<BR>


setfacl -m u:&lt;user&gt;:r /dev/&lt;disk&gt;

<P>

<BR>

This gives &lt;user&gt; read-only access without altering any of the existing ownerships/permissions. The permissions will 
typically be reset at next reboot or through <A HREF="http://localhost/cgi-bin/man/man2html?7+udev">udev</A>(7).
You need to have ACL enabled on the file system containing /dev/ and the setfacl tool installed.
<P>
<A NAME="lbAR">&nbsp;</A>
<H2>OTHER PLATFORMS</H2>

<B>qdda</B>

currently only runs on 64-bit linux. Disks from other platforms can be processed by using qdda over a named or unnamed pipe.
<BR>

You can do this using netcat (nc)
<P>
target host: (as qdda)
<BR>

nc -l 19000 | qdda
<BR>

source host: (as root) 
<BR>

cat /dev/&lt;disk&gt; | nc targethost 19000

<P>
<A NAME="lbAS">&nbsp;</A>
<H2>KNOWN ISSUES</H2>

Database journaling and synchronous mode are disabled for performance reasons. This means the database may be corrupted if qdda is ended
in an abnormal way (killed, file system full, etc).
<BR>

Accessing the SQLite database directly requires recent versions of the sqlite3 tools. Older versions are not compatible with the database
schema and abort with an error upon opening.
<BR>

Scanning disk partitions (/dev/sdb1, /dev/sdd4 etc) or otherwise unaligned partitions may produce poor dedupe results. This is
&quot;as designed&quot; - we assume you know what you are doing.
<BR>

Dumping multiple devices to a single pipe (i.e. cat /dev/sda /dev/sdb | qdda) may result in wrong alignment as well.
<P>
<P>
<P>
<A NAME="lbAT">&nbsp;</A>
<H2>SEE ALSO</H2>

<A HREF="http://localhost/cgi-bin/man/man2html?1+lz4">lz4</A>(1), <A HREF="http://localhost/cgi-bin/man/man2html?1+md5">md5</A>(1), <A HREF="http://localhost/cgi-bin/man/man2html?1+sqlite3">sqlite3</A>(1), <A HREF="http://localhost/cgi-bin/man/man2html?1+mkfifo">mkfifo</A>(1), <A HREF="http://localhost/cgi-bin/man/man2html?1+nc">nc</A>(1), <A HREF="http://localhost/cgi-bin/man/man2html?7+udev">udev</A>(7), <A HREF="http://localhost/cgi-bin/man/man2html?1+setfacl">setfacl</A>(1)
<P>
<A NAME="lbAU">&nbsp;</A>
<H2>AUTHOR</H2>

Written by Bart Sjerps <I><A HREF="http://bartsjerps.wordpress.com">http://bartsjerps.wordpress.com</A></I>
<BR>

If you have suggestions for improvements in this tool, please send them along via the above address.
<BR>

The source code and downloadable binaries are available from <I><A HREF="https://github.com/outrunnl/qdda">https://github.com/outrunnl/qdda</A></I> 
<P>
<A NAME="lbAV">&nbsp;</A>
<H2>COPYRIGHT</H2>

Copyright © 2018 Bart Sjerps,  License GPLv3+: GNU GPL version 3 or later &lt;<A HREF="http://gnu.org/licenses/gpl.html">http://gnu.org/licenses/gpl.html</A>&gt;.
<BR>

This is free software: you are free to change and redistribute it.  There is NO WARRANTY, to the extent permitted by law.
<P>
<A NAME="lbAW">&nbsp;</A>
<H2>DISCLAIMER</H2>

This software is provided &quot;as is&quot; and follows the licensing and warranty guidelines 
of the GPL. In normal language that means I will not be held 
responsible for any problems you may encounter with this software.
<P>

<HR>
<A NAME="index">&nbsp;</A><H2>Index</H2>
<DL>
<DT><A HREF="#lbAB">NAME</A><DD>
<DT><A HREF="#lbAC">SYNOPSIS</A><DD>
<DT><A HREF="#lbAD">DESCRIPTION</A><DD>
<DT><A HREF="#lbAE">IMPORTANT NOTES</A><DD>
<DT><A HREF="#lbAF">OPTIONS</A><DD>
<DT><A HREF="#lbAG">STORAGE ARRAYS</A><DD>
<DT><A HREF="#lbAH">ERRORS</A><DD>
<DT><A HREF="#lbAI">EXAMPLE</A><DD>
<DT><A HREF="#lbAJ">COMBINING MULTIPLE SCANS</A><DD>
<DT><A HREF="#lbAK">RESOURCE REQUIREMENTS</A><DD>
<DT><A HREF="#lbAL">EXPLANATION</A><DD>
<DT><A HREF="#lbAM">ACCURACY</A><DD>
<DT><A HREF="#lbAN">PERFORMANCE</A><DD>
<DT><A HREF="#lbAO">CONFIG FILES</A><DD>
<DT><A HREF="#lbAP">ENVIRONMENT VARIABLES</A><DD>
<DT><A HREF="#lbAQ">SECURITY AND SAFETY</A><DD>
<DT><A HREF="#lbAR">OTHER PLATFORMS</A><DD>
<DT><A HREF="#lbAS">KNOWN ISSUES</A><DD>
<DT><A HREF="#lbAT">SEE ALSO</A><DD>
<DT><A HREF="#lbAU">AUTHOR</A><DD>
<DT><A HREF="#lbAV">COPYRIGHT</A><DD>
<DT><A HREF="#lbAW">DISCLAIMER</A><DD>
</DL>
<HR>
This document was created by
<A HREF="http://localhost/cgi-bin/man/man2html">man2html</A>,
using the manual pages.<BR>
Time: 07:42:34 GMT, April 29, 2018
</BODY>
</HTML>
