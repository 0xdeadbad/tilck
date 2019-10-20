/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/failsafe_assert.h>
#include <tilck/common/string_util.h>
#include <tilck/common/fat32_base.h>


/*
 * The following code uses in many cases the CamelCase naming convention
 * because it is based on the Microsoft's public document:
 *
 *    Microsoft Extensible Firmware Initiative
 *    FAT32 File System Specification
 *
 *    FAT: General Overview of On-Disk Format
 *
 *    Version 1.03, December 6, 2000
 *
 * Keeping the exact same names as the official document, helps a lot.
 */


#define FAT_ENTRY_DIRNAME_NO_MORE_ENTRIES ((char)0)
#define FAT_ENTRY_DIRNAME_EMPTY_DIR ((char)0xE5)

static u8 shortname_checksum(u8 *shortname)
{
   u8 sum = 0;

   for (int i = 0; i < 11; i++) {
      // NOTE: The operation is an unsigned char rotate right
      sum = (u8)( ((sum & 1u) ? 0x80u : 0u) + (sum >> 1u) + *shortname++ );
   }

   return sum;
}

static const bool fat32_valid_chars[256] =
{
   [0 ... 32] = 0,

   [33] = 0, /* ! */
   [34] = 0, /* " */
   [35] = 1, /* # */
   [36] = 1, /* $ */
   [37] = 1, /* % */
   [38] = 1, /* & */
   [39] = 1, /* ' */
   [40] = 1, /* ( */
   [41] = 1, /* ) */
   [42] = 0, /* * */
   [43] = 1, /* + */
   [44] = 1, /* , */
   [45] = 1, /* - */
   [46] = 1, /* . */
   [47] = 0, /* / */
   [48] = 1, /* 0 */
   [49] = 1, /* 1 */
   [50] = 1, /* 2 */
   [51] = 1, /* 3 */
   [52] = 1, /* 4 */
   [53] = 1, /* 5 */
   [54] = 1, /* 6 */
   [55] = 1, /* 7 */
   [56] = 1, /* 8 */
   [57] = 1, /* 9 */
   [58] = 0, /* : */
   [59] = 1, /* ; */
   [60] = 0, /* < */
   [61] = 1, /* = */
   [62] = 0, /* > */
   [63] = 0, /* ? */
   [64] = 1, /* @ */
   [65] = 1, /* A */
   [66] = 1, /* B */
   [67] = 1, /* C */
   [68] = 1, /* D */
   [69] = 1, /* E */
   [70] = 1, /* F */
   [71] = 1, /* G */
   [72] = 1, /* H */
   [73] = 1, /* I */
   [74] = 1, /* J */
   [75] = 1, /* K */
   [76] = 1, /* L */
   [77] = 1, /* M */
   [78] = 1, /* N */
   [79] = 1, /* O */
   [80] = 1, /* P */
   [81] = 1, /* Q */
   [82] = 1, /* R */
   [83] = 1, /* S */
   [84] = 1, /* T */
   [85] = 1, /* U */
   [86] = 1, /* V */
   [87] = 1, /* W */
   [88] = 1, /* X */
   [89] = 1, /* Y */
   [90] = 1, /* Z */
   [91] = 1, /* [ */
   [92] = 0, /* \ */
   [93] = 1, /* ] */
   [94] = 1, /* ^ */
   [95] = 1, /* _ */
   [96] = 1, /* ` */
   [97] = 1, /* a */
   [98] = 1, /* b */
   [99] = 1, /* c */
   [100] = 1, /* d */
   [101] = 1, /* e */
   [102] = 1, /* f */
   [103] = 1, /* g */
   [104] = 1, /* h */
   [105] = 1, /* i */
   [106] = 1, /* j */
   [107] = 1, /* k */
   [108] = 1, /* l */
   [109] = 1, /* m */
   [110] = 1, /* n */
   [111] = 1, /* o */
   [112] = 1, /* p */
   [113] = 1, /* q */
   [114] = 1, /* r */
   [115] = 1, /* s */
   [116] = 1, /* t */
   [117] = 1, /* u */
   [118] = 1, /* v */
   [119] = 1, /* w */
   [120] = 1, /* x */
   [121] = 1, /* y */
   [122] = 1, /* z */
   [123] = 1, /* { */
   [124] = 0, /* | */
   [125] = 1, /* } */
   [126] = 1, /* ~ */

   [127 ... 255] = 0,
};

bool fat32_is_valid_filename_character(char c)
{
   return fat32_valid_chars[(u8)c];
}

/*
 * WARNING: this implementation supports only the ASCII subset of UTF16.
 */
static void fat_handle_long_dir_entry(fat_walk_dir_ctx *ctx,
                                      fat_long_entry *le)
{
   char entrybuf[13] = {0};
   int ebuf_size=0;

   if (ctx->lname_chksum != le->LDIR_Chksum) {
      bzero(ctx->lname_buf, sizeof(ctx->lname_chksum));
      ctx->lname_sz = 0;
      ctx->lname_chksum = le->LDIR_Chksum;
      ctx->is_valid = true;
   }

   if (!ctx->is_valid)
      return;

   for (int i = 0; i < 10; i += 2) {

      u8 c = le->LDIR_Name1[i];

      /* NON-ASCII characters are NOT supported */
      if (le->LDIR_Name1[i+1] != 0) {
         ctx->is_valid = false;
         return;
      }

      if (c == 0 || c == 0xFF)
         goto end;

      entrybuf[ebuf_size++] = (char)c;
   }

   for (int i = 0; i < 12; i += 2) {

      u8 c = le->LDIR_Name2[i];

      /* NON-ASCII characters are NOT supported */
      if (le->LDIR_Name2[i+1] != 0) {
         ctx->is_valid = false;
         return;
      }

      if (c == 0 || c == 0xFF)
         goto end;

      entrybuf[ebuf_size++] = (char)c;
   }

   for (int i = 0; i < 4; i += 2) {

      u8 c = le->LDIR_Name3[i];

      /* NON-ASCII characters are NOT supported */
      if (le->LDIR_Name3[i+1] != 0) {
         ctx->is_valid = false;
         return;
      }

      if (c == 0 || c == 0xFF)
         goto end;

      entrybuf[ebuf_size++] = (char)c;
   }

   end:

   for (int i = ebuf_size-1; i >= 0; i--) {

      char c = entrybuf[i];

      if (!fat32_is_valid_filename_character(c)) {
         ctx->is_valid = false;
         break;
      }

      ctx->lname_buf[ctx->lname_sz++] = (u8) c;
   }
}

int fat_walk_directory(fat_walk_dir_ctx *ctx,
                       struct fat_header *hdr,
                       enum fat_type ft,
                       fat_entry *entry,
                       u32 cluster,
                       fat_dentry_cb cb,
                       void *arg)
{
   const u32 entries_per_cluster =
      ((u32)hdr->BPB_BytsPerSec * hdr->BPB_SecPerClus) / sizeof(fat_entry);

   ASSERT(ft == fat16_type || ft == fat32_type);

   if (ft == fat16_type) {
      ASSERT(cluster == 0 || entry == NULL); // cluster != 0 => entry == NULL
      ASSERT(entry == NULL || cluster == 0); // entry != NULL => cluster == 0
   }

   bzero(ctx->lname_buf, sizeof(ctx->lname_buf));
   ctx->lname_sz = 0;
   ctx->lname_chksum = -1;

   while (true) {

      if (cluster != 0) {

         /*
          * if cluster != 0, cluster is used and entry is overriden.
          * That's because on FAT16 we know only the sector of the root dir.
          * In that case, fat_get_rootdir() returns 0 as cluster. In all the
          * other cases, we need only the cluster.
          */
         entry = fat_get_pointer_to_cluster_data(hdr, cluster);
      }

      ASSERT(entry != NULL);

      for (u32 i = 0; i < entries_per_cluster; i++) {

         if (is_long_name_entry(&entry[i])) {
            fat_handle_long_dir_entry(ctx, (fat_long_entry*)&entry[i]);
            continue;
         }

         if (entry[i].volume_id) {
            continue; // the first "file" is the volume ID. Skip it.
         }

         // that means all the rest of the entries are free.
         if (entry[i].DIR_Name[0] == FAT_ENTRY_DIRNAME_NO_MORE_ENTRIES) {
            return 0;
         }

         // that means that the directory is empty
         if (entry[i].DIR_Name[0] == FAT_ENTRY_DIRNAME_EMPTY_DIR) {
            return 0;
         }

         const char *long_name_ptr = NULL;

         if (ctx->lname_sz > 0 && ctx->is_valid) {

            s16 entry_checksum = shortname_checksum((u8 *)entry[i].DIR_Name);

            if (ctx->lname_chksum == entry_checksum) {
               ctx->lname_buf[ctx->lname_sz] = 0;
               str_reverse((char *)ctx->lname_buf, (size_t)ctx->lname_sz);
               long_name_ptr = (const char *) ctx->lname_buf;
            }
         }

         int ret = cb(hdr, ft, entry + i, long_name_ptr, arg);

         ctx->lname_sz = 0;
         ctx->lname_chksum = -1;

         if (ret) {
            /* the callback returns a value != 0 to request a walk STOP. */
            return 0;
         }
      }

      /*
       * In case dump_directory() has been called on the root dir on a FAT16,
       * cluster is 0 (invalid) and there is no next cluster in the chain. This
       * fact seriously limits the number of items in the root dir of a FAT16
       * volume.
       */
      if (cluster == 0)
         break;

      /*
       * If we're here, it means that there is more then one cluster for the
       * entries of this directory. We have to follow the chain.
       */
      u32 val = fat_read_fat_entry(hdr, ft, cluster, 0);

      if (fat_is_end_of_clusterchain(ft, val))
         break; // that's it: we hit an exactly full cluster.

      // we do not expect BAD CLUSTERS
      ASSERT(!fat_is_bad_cluster(ft, val));

      cluster = val;
   }

   return 0;
}

enum fat_type fat_get_type(struct fat_header *hdr)
{
   u32 FATSz = fat_get_FATSz(hdr);
   u32 TotSec = fat_get_TotSec(hdr);
   u32 RootDirSectors = fat_get_RootDirSectors(hdr);
   u32 FatAreaSize = hdr->BPB_NumFATs * FATSz;
   u32 DataSec = TotSec - (hdr->BPB_RsvdSecCnt + FatAreaSize + RootDirSectors);
   u32 CountofClusters = DataSec / hdr->BPB_SecPerClus;

   if (CountofClusters < 4085) {

      /* Volume is FAT12 */
      return fat12_type;

   } else if (CountofClusters < 65525) {

      /* Volume is FAT16 */
      return fat16_type;

   } else {

      return fat32_type;
      /* Volume is FAT32 */
   }
}

/*
 * Reads the entry in the FAT 'fatNum' for cluster 'clusterN'.
 * The entry may be 16 or 32 bit. It returns 32-bit integer for convenience.
 */
u32 fat_read_fat_entry(struct fat_header *hdr, enum fat_type ft, u32 clusterN, u32 fatNum)
{
   if (ft == fat_unknown) {
      ft = fat_get_type(hdr);
   }

   if (ft == fat12_type) {
      // FAT12 is NOT supported.
      NOT_REACHED();
   }

   ASSERT(fatNum < hdr->BPB_NumFATs);

   u32 FATSz = fat_get_FATSz(hdr);
   u32 FATOffset = (ft == fat16_type) ? clusterN * 2 : clusterN * 4;

   u32 ThisFATSecNum =
      fatNum * FATSz + hdr->BPB_RsvdSecCnt + (FATOffset / hdr->BPB_BytsPerSec);

   u32 ThisFATEntOffset = FATOffset % hdr->BPB_BytsPerSec;

   u8 *SecBuf = (u8*)hdr + ThisFATSecNum * hdr->BPB_BytsPerSec;

   if (ft == fat16_type) {
      return *(u16*)(SecBuf+ThisFATEntOffset);
   }

   // FAT32
   // Note: FAT32 "FAT" entries are 28-bit. The 4 higher bits are reserved.
   return (*(u32*)(SecBuf+ThisFATEntOffset)) & 0x0FFFFFFF;
}

u32 fat_get_first_data_sector(struct fat_header *hdr)
{
   u32 RootDirSectors = fat_get_RootDirSectors(hdr);
   u32 FATSz;

   if (hdr->BPB_FATSz16 != 0) {
      FATSz = hdr->BPB_FATSz16;
   } else {
      fat32_header2 *h32 = (fat32_header2*) (hdr+1);
      FATSz = h32->BPB_FATSz32;
   }

   u32 FirstDataSector = hdr->BPB_RsvdSecCnt +
      (hdr->BPB_NumFATs * FATSz) + RootDirSectors;

   return FirstDataSector;
}

u32 fat_get_sector_for_cluster(struct fat_header *hdr, u32 N)
{
   u32 FirstDataSector = fat_get_first_data_sector(hdr);

   // FirstSectorofCluster
   return ((N - 2) * hdr->BPB_SecPerClus) + FirstDataSector;
}

fat_entry *fat_get_rootdir(struct fat_header *hdr, enum fat_type ft, u32 *cluster /* out */)
{
   ASSERT(ft != fat12_type);
   ASSERT(ft != fat_unknown);

   u32 sector;

   if (ft == fat16_type) {

      u32 FirstDataSector =
         (u32)hdr->BPB_RsvdSecCnt + (u32)(hdr->BPB_NumFATs * hdr->BPB_FATSz16);

      sector = FirstDataSector;
      *cluster = 0; /* On FAT16 the root dir entry is NOT a cluster chain! */

   } else {

      /* FAT32 case */
      fat32_header2 *h32 = (fat32_header2 *) (hdr + 1);
      *cluster = h32->BPB_RootClus;
      sector = fat_get_sector_for_cluster(hdr, *cluster);
   }

   return (fat_entry*) ((u8*)hdr + (hdr->BPB_BytsPerSec * sector));
}

void fat_get_short_name(fat_entry *entry, char *destbuf)
{
   u32 i = 0;
   u32 d = 0;

   for (i = 0; i < 8 && entry->DIR_Name[i] != ' '; i++) {

      char c = entry->DIR_Name[i];

      destbuf[d++] =
         (entry->DIR_NTRes & FAT_ENTRY_NTRES_BASE_LOW_CASE)
            ? (char)tolower(c)
            : c;
   }

   i = 8; // beginning of the extension part.

   if (entry->DIR_Name[i] != ' ') {

      destbuf[d++] = '.';

      for (; i < 11 && entry->DIR_Name[i] != ' '; i++) {

         char c = entry->DIR_Name[i];

         destbuf[d++] =
            (entry->DIR_NTRes & FAT_ENTRY_NTRES_EXT_LOW_CASE)
               ? (char)tolower(c)
               : c;
      }
   }

   destbuf[d] = 0;
}

static bool fat_fetch_next_component(fat_search_ctx *ctx)
{
   ASSERT(ctx->pcl == 0);

   /*
    * Fetch a path component from the abspath: we'll use it while iterating
    * the whole directory. On a match, we reset pcl and start a new walk on
    * the subdirectory.
    */

   while (*ctx->path && *ctx->path != '/') {
      ctx->pc[ctx->pcl++] = *ctx->path++;
   }

   ctx->pc[ctx->pcl++] = 0;
   return ctx->pcl != 0;
}

int fat_search_entry_cb(struct fat_header *hdr,
                        enum fat_type ft,
                        fat_entry *entry,
                        const char *long_name,
                        void *arg)
{
   fat_search_ctx *ctx = arg;

   if (ctx->pcl == 0) {
      if (!fat_fetch_next_component(ctx)) {
         // The path was empty, so no path component has been fetched.
         return -1;
      }
   }

   /*
    * NOTE: the following is NOT fully FAT32 compliant: for long names this
    * code compares file names using a CASE SENSITIVE comparison!
    * This HACK allows a UNIX system like Tilck to use FAT32 [case sensitivity
    * is a MUST in UNIX] by just forcing each file to have a long name, even
    * when that is not necessary.
    */

   if (long_name) {

      if (strcmp(long_name, ctx->pc)) {
         // no match, continue.
         return 0;
      }

      // we have a long-name match (case sensitive)

   } else {

      /*
       * no long name: for short names, we do a compliant case INSENSITIVE
       * string comparison.
       */

      fat_get_short_name(entry, ctx->shortname);

      if (stricmp(ctx->shortname, ctx->pc)) {
         // no match, continue.
         return 0;
      }

      // we have a short-name match (case insensitive)
   }

   // we've found a match.

   if (ctx->single_comp || *ctx->path == 0) {
      ctx->result = entry; // if the path ended, that's it. Just return.
      return -1;
   }

   /*
    * The next char in path MUST be a '/' since otherwise
    * fat_fetch_next_component() would have continued, until a '/' or a
    * '\0' is hit.
    */
   ASSERT(*ctx->path == '/');

   // path's next char is a '/': maybe there are more components in the path.
   ctx->path++;

   if (*ctx->path == 0) {

      /*
       * The path just ended with '/'. That's OK only if entry is acutally is
       * a directory.
       */

      if (entry->directory)
         ctx->result = entry;
      else
         ctx->not_dir = true;

      return -1;
   }

   if (!entry->directory)
      return -1; // if the entry is not a directory, we failed.

   // The path did not end: we have to do a walk in the sub-dir.
   ctx->pcl = 0;
   ctx->subdir_cluster = fat_get_first_cluster(entry);
   return -1;
}

void
fat_init_search_ctx(fat_search_ctx *ctx, const char *path, bool single_comp)
{
   bzero(ctx, sizeof(fat_search_ctx));

#ifdef __clang_analyzer__
   ctx->pcl = 0;       /* SA: make it sure ctx.pcl is zeroed */
   ctx->result = NULL; /* SA: make it sure ctx.result is zeroed */
#endif

   ctx->path = path;
   ctx->single_comp = single_comp;
}

fat_entry *
fat_search_entry(struct fat_header *hdr, enum fat_type ft, const char *abspath, int *err)
{
   fat_search_ctx ctx;
   fat_entry *root;
   u32 root_dir_cluster;

   if (ft == fat_unknown) {
       ft = fat_get_type(hdr);
   }

   ASSERT(*abspath == '/');
   abspath++;

   root = fat_get_rootdir(hdr, ft, &root_dir_cluster);

   if (!*abspath) {
      /* the whole abspath was just '/' */
      return root;
   }

   fat_init_search_ctx(&ctx, abspath, false);
   fat_walk_directory(&ctx.walk_ctx, hdr, ft, root, root_dir_cluster,
                      &fat_search_entry_cb, &ctx);

   while (ctx.subdir_cluster) {

      u32 cluster = ctx.subdir_cluster;
      ctx.subdir_cluster = 0;

      fat_walk_directory(&ctx.walk_ctx, hdr, ft, NULL, cluster,
                         &fat_search_entry_cb, &ctx);
   }

   if (err) {
      if (ctx.not_dir)
         *err = -20; /* -ENOTDIR */
      else
         *err = !ctx.result ? -2 /* ENOENT */: 0;
   }

   return ctx.result;
}

size_t fat_get_file_size(fat_entry *entry)
{
   return entry->DIR_FileSize;
}

void
fat_read_whole_file(struct fat_header *hdr,
                    fat_entry *entry, char *dest_buf, size_t dest_buf_size)
{
   ASSERT(entry->DIR_FileSize <= dest_buf_size);

   // cluster size in bytes
   const u32 cs = (u32)hdr->BPB_SecPerClus * (u32)hdr->BPB_BytsPerSec;

   u32 cluster;
   size_t written = 0;
   size_t fsize = entry->DIR_FileSize;

   enum fat_type ft = fat_get_type(hdr);

   cluster = fat_get_first_cluster(entry);

   do {

      char *data = fat_get_pointer_to_cluster_data(hdr, cluster);

      size_t rem = fsize - written;

      if (rem <= cs) {
         // read what is needed
         memmove(dest_buf + written, data, rem);
         /* written += rem; */ // Avoid SA warning "dead increment"
         break;
      }

      // read the whole cluster
      memmove(dest_buf + written, data, cs);
      written += cs;

      ASSERT((fsize - written) > 0);

      // find the next cluster
      u32 fatval = fat_read_fat_entry(hdr, ft, cluster, 0);

      if (fat_is_end_of_clusterchain(ft, fatval)) {
         // rem is still > 0, this should NOT be the last cluster
         NOT_REACHED();
      }

      // we do not expect BAD CLUSTERS
      ASSERT(!fat_is_bad_cluster(ft, fatval));

      cluster = fatval; // go reading the new cluster in the chain.

   } while (written < fsize);
}

u32
fat_get_used_bytes(struct fat_header *hdr)
{
   u32 clusterN;
   const u32 cluster_count = fat_get_TotSec(hdr) / hdr->BPB_SecPerClus;

   for (clusterN = 0; clusterN < cluster_count; clusterN++) {
      if (!fat_read_fat_entry(hdr, fat_unknown, clusterN, 0))
         break;
   }

   u32 first_free_sector = fat_get_sector_for_cluster(hdr, clusterN);
   return first_free_sector * hdr->BPB_BytsPerSec;
}
