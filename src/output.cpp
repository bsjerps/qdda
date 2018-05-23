/*******************************************************************************
 * Title       : output.cpp
 * Description : Screen output functions for qdda
 * Author      : Bart Sjerps <bart@outrun.nl>
 * License     : GPLv3+, https://www.gnu.org/licenses/gpl-3.0.txt
 * Disclaimer  : GPLv3+
 * URL         : http://outrun.nl/wiki/qdda
 ******************************************************************************/

#include <iostream>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <string>
#include <cstring>
#include <limits.h>

#include "tools.h"
#include "database.h"
#include "qdda.h"

using namespace std;
extern bool g_quiet;

/*******************************************************************************
 * Formatting
 ******************************************************************************/

// format blocks value
const string formatBlocks(ulong blocks) {
  stringstream ss;
  ss << " ("
  << setfill(' ') << right << setw(10) << blocks << " blocks)";
  return ss.str();
}

// format bytes into MiB
const string formatMib(float b) {
  stringstream ss;
  ss << setfill(' ') << fixed << setprecision(2) << right << setw(11) << b << " MiB";
  return ss.str();
}

// format percentage
const string formatPerc(float p) {
  stringstream ss;
  ss << " (" << setfill(' ') << fixed << setprecision(2) << right << setw(10) << p*100 << " %)";
  return ss.str();
}

// format ratio
const string formatRatio(float r) {
  stringstream ss;
  ss << setfill(' ') << fixed << setprecision(2) << right << setw(11) << r;
  return ss.str();
}

// format string
const string formatString(const string& s) {
  stringstream ss;
  ss << right << setw(15) << s;
  return ss.str();
}

// format field name
const string formatName(string s) {
  stringstream ss;
  ss << left << setw(19) << s;
  if(!s.empty()) ss << " = ";
  else ss << "   ";
  return ss.str();
}

/*******************************************************************************
 * Reports
 ******************************************************************************/

// Print stats report
void report(QddaDB& db) {
  if(g_quiet) return;
  ulong blocksize        = db.blocksize;
  string arrayid         = db.arrayid.str();
  const float blocks2mb  = blocksize/1024.0;
  const float bytes2mb   = 1.0/1048576;

  ulong blocks_total     = db.totalblocks;              // Total blocks (total file size)
  ulong blocks_used      = db.usedblocks;               // Total used blocks
  ulong blocks_free      = db.zeroblocks;               // Total zero blocks
  ulong blocks_dedup     = db.dedupedblocks;            // Unique hashes (deduped) between 0 and max
  ulong blocks_alloc     = db.allocatedblocks;          // blocks required after bucket compression
  ulong blocks_unique    = db.uniqueblocks;             // Hashes with count=1 (non-dedupable data)
  ulong blocks_nuniq     = db.nonuniqblocks;            // count>1 (dedupable data)
  ulong bytes_compr_raw  = db.bytescompressedraw;       // compressed bytes before dedup
  ulong bytes_compr_net  = db.bytescompressednet;       // compressed bytes after dedup
  ulong blocks_merged    = blocks_used - blocks_dedup;  // blocks saved by dedup

  // ratios & percentages - divide by zero -> 0
  float perc_used   = safeDiv_float (blocks_used,  blocks_total); // percentage used / raw
  float perc_free   = safeDiv_float (blocks_free,  blocks_total); // percentage free (zero) / raw
  float ratio_dedup = safeDiv_float (blocks_used,  blocks_dedup);
  float ratio_thin  = safeDiv_float (blocks_total, blocks_used);
  float ratio_compr = safeDiv_float (blocks_dedup, blocks_alloc); // deduped compression ratio (sorted into slots)

  float perc_compr_raw = 1 - safeDiv_float (bytes_compr_raw,  blocks_used*blocksize*1024);  // compression ratio before dedupe
  float perc_postdedup = 1 - safeDiv_float (bytes_compr_net,  blocks_dedup*blocksize*1024); // compression ratio after dedupe
  
  float ratio_total = ratio_dedup*ratio_compr*ratio_thin; // overall storage reduction
  float filesize    = fileSize(db.filename()) * bytes2mb; // file size in MiB


// add explanation row
  cout
  << endl << "Database info (" << db.filename() << "):"
  << endl << formatName("database size")       << setprecision(2) << fixed << filesize << " MiB"
  << endl << formatName("array id")            << arrayid
  << endl << formatName("blocksize")           << toString(blocksize) + " KiB"
  << endl
  << endl << "Overview:"
  << endl << formatName("total")               << formatMib(blocks_total     * blocks2mb) << formatBlocks(blocks_total)
  << endl << formatName("free (zero)")         << formatMib(blocks_free      * blocks2mb) << formatBlocks(blocks_free)
  << endl << formatName("used")                << formatMib(blocks_used      * blocks2mb) << formatBlocks(blocks_used)
  << endl << formatName("dedupe savings")      << formatMib(blocks_merged    * blocks2mb) << formatBlocks(blocks_merged)
  << endl << formatName("deduped")             << formatMib(blocks_dedup     * blocks2mb) << formatBlocks(blocks_dedup)
  << endl << formatName("compressed")          << formatMib(bytes_compr_net  * bytes2mb)  << formatPerc(perc_postdedup)  
  << endl << formatName("allocated")           << formatMib(blocks_alloc     * blocks2mb) << formatBlocks(blocks_alloc)
  << endl
  << endl << "Details:"  
  << endl << formatName("used")                << formatMib(blocks_used      * blocks2mb) << formatBlocks(blocks_used)
  << endl << formatName("compressed raw")      << formatMib(bytes_compr_raw  * bytes2mb)  << formatPerc(perc_compr_raw)
  << endl << formatName("unique data")         << formatMib(blocks_unique    * blocks2mb) << formatBlocks(blocks_unique)
  << endl << formatName("non-unique data")     << formatMib(blocks_nuniq     * blocks2mb) << formatBlocks(blocks_nuniq) 
  << endl
  << endl << "Summary:"
  << endl << formatName("percentage used")     << formatRatio(100*perc_used) << " %"
  << endl << formatName("percentage free")     << formatRatio(100*perc_free) << " %"
  << endl << formatName("deduplication ratio") << formatRatio(ratio_dedup)
  << endl << formatName("compression ratio")   << formatRatio(ratio_compr)
  << endl << formatName("thin ratio")          << formatRatio(ratio_thin)
  << endl << formatName("combined")            << formatRatio(ratio_total)
  << endl << formatName("raw capacity")        << formatMib(blocks_total*blocks2mb)
  << endl << formatName("net capacity")        << formatMib(blocks_alloc*blocks2mb)
  << endl << endl;
}


// print extended report with file info and dedupe/compression histograms
void reportDetail(QddaDB& db) {

  cout << "File list:" << endl;
  db.filelist.report(cout, "8,-6,-10,-11,18,80");

  cout << endl << "Dedupe histogram:" << endl;
  db.dedupehistogram.report(cout, "8,-12,-12,-12");

  cout << endl << "Compression Histogram (" << db.arrayid.str() << "): " << endl;
  db.compresshistogram.report(cout, "8,-12,-12,-12,-12");
}
