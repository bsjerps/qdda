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
#include <string>

#include "tools.h"
#include "database.h"
#include "qdda.h"

using namespace std;
extern bool g_quiet;

/*******************************************************************************
 * Formatting helpers
 ******************************************************************************/

// left and right column formatting
ostream& col1(ostream& os) { os << "\n" << left << setw(19);  return os; }
ostream& col2(ostream& os) { os << setfill(' ') << fixed << setprecision(2) << right << setw(11); return os; }

// Megabytes
class mib {
public:
  mib(float in) { val = in ; }
  float val;
};

// Data blocks
class blocks {
public:
  blocks(uint64 in) { val = in ; }
  uint64 val;
};

// Percentages
class pct {
public:
  pct(float in) { val = in ; }
  float val;
};

ostream& operator<<(ostream& os,mib x) {
  os << setfill(' ') << fixed << setprecision(2) << right << setw(11) << x.val << " MiB";
  return os; 
}

ostream& operator<<(ostream& os,blocks x) { 
  os << " (" << setfill(' ') << right << setw(10) << x.val << " blocks)";
  return os; 
}

ostream& operator<<(ostream& os,pct x) { 
  os << " (" << setfill(' ') << fixed << setprecision(2) << right << setw(10) << x.val << " %)";
  return os;
}

/*******************************************************************************
 * Basic data reduction report
 ******************************************************************************/

void report(QddaDB& db) {
  if(g_quiet) return;
  int64 blocksize       = db.getblocksize();
  const float blocks2mb = blocksize/1024.0;
  const float bytes2mb  = 1.0/1048576;
  
  int64 blocks_total  = db.getint("select sum(blocks) from kv");                   // Total blocks (total file size)
  int64 blocks_free   = db.getint("select blocks from kv where hash=0");           // Total zero blocks
  int64 blocks_used   = db.getint("select sum(ref*blocks) from m_sums_deduped");   // Total used blocks
  int64 blocks_dedup  = db.getint("select sum(blocks) from m_sums_deduped");       // Unique hashes (deduped) between 0 and max
  int64 blocks_unique = db.getint("select blocks from m_sums_deduped where ref=1");          // Hashes with count=1 (non-dedupable data)
  int64 blocks_nuniq  = db.getint("select sum(ref*blocks) from m_sums_deduped where ref>1"); // count>1 (dedupable data)
  int64 blocks_merged = blocks_used - blocks_dedup;                                         // blocks saved by dedup

  float sample_perc  = db.getfloat("select 100.0*(select sum(blocks) from m_sums_compressed)/(select sum(blocks) from m_sums_deduped)");
  float ratio_raw    = db.getfloat("with data(blksz) as (select blksz*1024 from metadata)\n"
                                "select 1.0*(select sum(totblocks*blksz))/(select sum(raw)) ratio from m_sums_compressed,data");
  float ratio_net    = db.getfloat("with data(blksz) as (select blksz*1024 from metadata)\n"
                                "select 1.0*(select sum(blocks*blksz))/(select sum(bytes)) ratio from m_sums_compressed,data");
  float ratio_compr  = db.getfloat("select 1.0*(select sum(buckets) from v_compressed)/(select sum(blocks) from v_compressed)");

  float perc_raw     = safeDiv_float(100,ratio_raw);
  float perc_net     = safeDiv_float(100,ratio_net);
  float perc_compr   = safeDiv_float(100,ratio_compr);

  int64 blocks_raw   = safeDiv_float(blocks_used , ratio_raw);
  int64 blocks_net   = safeDiv_float(blocks_dedup , ratio_net);
  int64 blocks_alloc = safeDiv_float(blocks_dedup , ratio_compr);

  float perc_used   = safeDiv_float (blocks_used,  blocks_total); // percentage used / raw
  float perc_free   = safeDiv_float (blocks_free,  blocks_total); // percentage free (zero) / raw
  float ratio_dedup = safeDiv_float (blocks_used,  blocks_dedup);
  float ratio_thin  = safeDiv_float (blocks_total, blocks_used);

  float ratio_total = ratio_dedup*ratio_compr*ratio_thin; // overall storage reduction
  float filesize    = fileSize(db.filename()) * bytes2mb; // file size in MiB

  cout
  << "\nDatabase info (" << db.filename() << "):"
  << col1 << "database size"       << " = " << col2 << filesize << " MiB"
  << col1 << "array id"            << " = " << col2 << Metadata::getArrayName(db.getarrayid())
  << col1 << "blocksize"           << " = " << col2 << blocksize << " KiB"
  << col1 << "compression"         << " = " << col2 << Metadata::getMethodName(db.getmethod())
  << col1 << "sample percentage"   << " = " << col2 << sample_perc << " %"
  << "\n\nOverview:"
  << col1 << "total"               << " = " << mib(blocks_total  * blocks2mb) << blocks(blocks_total  )
  << col1 << "free (zero)"         << " = " << mib(blocks_free   * blocks2mb) << blocks(blocks_free   )
  << col1 << "used"                << " = " << mib(blocks_used   * blocks2mb) << blocks(blocks_used   )
  << col1 << "dedupe savings"      << " = " << mib(blocks_merged * blocks2mb) << blocks(blocks_merged )
  << col1 << "deduped"             << " = " << mib(blocks_dedup  * blocks2mb) << blocks(blocks_dedup  )
  << col1 << "compressed"          << " = " << mib(blocks_net    * blocks2mb) << pct(100-perc_compr)
  << col1 << "allocated"           << " = " << mib(blocks_alloc  * blocks2mb) << blocks(blocks_alloc  )
  << "\n\nDetails:"
  << col1 << "used"                << " = " << mib(blocks_used   * blocks2mb) << blocks(blocks_used   )
  << col1 << "unique data"         << " = " << mib(blocks_unique * blocks2mb) << blocks(blocks_unique )
  << col1 << "non-unique data"     << " = " << mib(blocks_nuniq  * blocks2mb) << blocks(blocks_nuniq  )

  << col1 << "compressed raw"      << " = " << mib(blocks_raw    * blocks2mb) << pct(100-perc_raw)
  << col1 << "compressed net"      << " = " << mib(blocks_net    * blocks2mb) << pct(100-perc_net)
  << "\n\nSummary:"
  << col1 << "percentage used"     << " = " << col2 << 100*perc_used << " %"
  << col1 << "percentage free"     << " = " << col2 << 100*perc_free << " %"
  << col1 << "deduplication ratio" << " = " << col2 << ratio_dedup
  << col1 << "compression ratio"   << " = " << col2 << ratio_compr
  << col1 << "thin ratio"          << " = " << col2 << ratio_thin
  << col1 << "combined"            << " = " << col2 << ratio_total
  << col1 << "raw capacity"        << " = " << mib(blocks_total*blocks2mb)
  << col1 << "net capacity"        << " = " << mib(blocks_alloc*blocks2mb)
  << "\n" << endl;
}

/*******************************************************************************
 * Extended report - histograms and file info
 ******************************************************************************/

void reportDetail(QddaDB& db) {
  IntArray tabs;
  Query filelist(db,"select * from v_files");
  Query compresshistogram(db,"select * from v_compressed union all\n"
                             "select 'Total:', sum(buckets), sum(rawmib), sum(perc), sum(blocks), sum(MiB) from v_compressed");
  Query dedupehistogram(db,"select * from v_deduped union all \n"
                           "select 'Total:',sum(blocks), sum(perc),sum(MiB) from v_deduped");

  cout << "File list:" << endl;

  tabs << 8 << -6 << -10 << -11 << 18 << 80;
  filelist.report(cout, tabs);
  
  tabs.clear();
  tabs << 8 << -12 << -12 << -12;

  cout << endl << "Dedupe histogram:" << endl;
  dedupehistogram.report(cout, tabs);

  tabs.clear();
  tabs << 8 << -12 << -12 << -12 << -12 << -20;

  cout << endl << "Compression Histogram (" << db.getarrayid() << "): " << endl;
  compresshistogram.report(cout, tabs);
}
