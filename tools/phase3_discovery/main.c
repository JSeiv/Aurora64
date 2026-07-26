#include <libdragon.h>
#include <n64sys.h>
#include <ctype.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DISCOVERY_MAX_ROOTS       4
#define DISCOVERY_MAX_DEPTH       16
#define DISCOVERY_MAX_RECORDS     2048
#define DISCOVERY_MAX_PATHS       4096
#define DISCOVERY_MAX_PATH_BYTES  196608
#define DISCOVERY_FULL_PATH_CAP   4357
#define DISCOVERY_READ_BUFFER_CAP (64 * 1024)
#define DISCOVERY_HEADER_BYTES    64
#define DISCOVERY_TOP_LEVEL_CAP   64
#define DISCOVERY_HASH_CHUNK      4096
#define DISCOVERY_HASH_CLASSES    3
#define SYNTH_RECORD_CAP          2048u
#define SYNTH_PATH_ARENA_BYTES    196608u
#define SYNTH_PATH_BYTES          96u
#define SYNTH_PATH_LENGTH         95u
#define SYNTH_SINK_BYTES          65536u
#define SYNTH_ENCODE_PASSES       2u
#define SYNTH_HEADER_ENCODED      32u
#define SYNTH_METADATA_ENCODED    24u

_Static_assert(DISCOVERY_FULL_PATH_CAP - 1 <= UINT16_MAX,
               "retained path_length must fit uint16_t");

static const char *const scan_roots[DISCOVERY_MAX_ROOTS] = { "sd:/" };
static const unsigned scan_root_count = 1;

static const char *const root_exclusions[] = {
    "menu.bin", "menu", "N64FlashcartMenu.n64", "ED64", "ED64P",
    "sc64menu.n64", "System Volume Information", ".fseventsd",
    ".Spotlight-V100", ".Trashes", ".VolumeIcon.icns",
    ".metadata_never_index"
};

typedef enum {
    STATE_SCAN,
    STATE_SELECTION,
    STATE_READ_4K,
    STATE_READ_64K,
    STATE_HASH,
    STATE_SYNTHETIC,
    STATE_CLOSE_SDFS,
    STATE_COMPLETE,
    STATE_ERROR,
    STATE_CLEANUP,
    STATE_QUIESCED,
    STATE_QUIESCE_FAILED_SAFE
} terminal_state_t;

typedef enum {
    READ_OPEN,
    READ_DATA,
    READ_FINALIZE,
    READ_CLOSE
} read_state_t;

typedef enum { HASH_OPEN, HASH_DATA, HASH_FINALIZE, HASH_CLOSE } hash_state_t;

typedef enum {
    SYNTH_ALLOC_RECORDS, SYNTH_ALLOC_PATHS, SYNTH_ALLOC_SINK,
    SYNTH_FILL, SYNTH_PASS_BEGIN, SYNTH_PASS_HEADER, SYNTH_PASS_RECORD,
    SYNTH_PASS_FINISH, SYNTH_FREE
} synth_state_t;

typedef struct {
    uint32_t path_offset;
    uint16_t path_length;
    uint8_t byte_order;
    uint8_t flags;
    uint64_t size;
    uint32_t header_word;
    uint32_t cic_word;
} synthetic_record_t;

_Static_assert(SYNTH_PATH_ARENA_BYTES / SYNTH_RECORD_CAP == SYNTH_PATH_BYTES,
               "synthetic path accounting must be exactly 96 bytes/record");
_Static_assert(sizeof(synthetic_record_t) == 24,
               "synthetic metadata allocation model changed");

/* Compact, original FIPS 180-4 SHA-256 implementation. */
typedef struct { uint32_t h[8]; uint64_t bytes; uint8_t block[64]; uint8_t used; } sha256_context_t;
static uint32_t sha_rotr(uint32_t x, unsigned n) { return (x >> n) | (x << (32u - n)); }
static void sha256_compress(sha256_context_t *c, const uint8_t p[64])
{
    static const uint32_t k[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2 };
    uint32_t w[64];
    for (unsigned i=0;i<16;++i) w[i]=((uint32_t)p[i*4]<<24)|((uint32_t)p[i*4+1]<<16)|((uint32_t)p[i*4+2]<<8)|p[i*4+3];
    for (unsigned i=16;i<64;++i) { uint32_t x=sha_rotr(w[i-15],7)^sha_rotr(w[i-15],18)^(w[i-15]>>3);
        uint32_t y=sha_rotr(w[i-2],17)^sha_rotr(w[i-2],19)^(w[i-2]>>10); w[i]=w[i-16]+x+w[i-7]+y; }
    uint32_t a=c->h[0],b=c->h[1],cc=c->h[2],d=c->h[3],e=c->h[4],f=c->h[5],g=c->h[6],h=c->h[7];
    for(unsigned i=0;i<64;++i){uint32_t s1=sha_rotr(e,6)^sha_rotr(e,11)^sha_rotr(e,25);
        uint32_t t1=h+s1+((e&f)^((~e)&g))+k[i]+w[i]; uint32_t s0=sha_rotr(a,2)^sha_rotr(a,13)^sha_rotr(a,22);
        uint32_t t2=s0+((a&b)^(a&cc)^(b&cc)); h=g;g=f;f=e;e=d+t1;d=cc;cc=b;b=a;a=t1+t2;}
    c->h[0]+=a;c->h[1]+=b;c->h[2]+=cc;c->h[3]+=d;c->h[4]+=e;c->h[5]+=f;c->h[6]+=g;c->h[7]+=h;
}
static void sha256_init(sha256_context_t *c)
{
    static const uint32_t v[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    memcpy(c->h,v,sizeof(v)); c->bytes=0; c->used=0;
}
static void sha256_update(sha256_context_t *c,const uint8_t *p,size_t n)
{
    c->bytes+=n; while(n){size_t room=64u-c->used,take=n<room?n:room; memcpy(c->block+c->used,p,take);
        c->used+=take;p+=take;n-=take;if(c->used==64){sha256_compress(c,c->block);c->used=0;}}
}
static void sha256_final(sha256_context_t *c,uint8_t out[32])
{
    uint64_t bits=c->bytes*8u;c->block[c->used++]=0x80;if(c->used>56){memset(c->block+c->used,0,64-c->used);sha256_compress(c,c->block);c->used=0;}
    memset(c->block+c->used,0,56-c->used);for(unsigned i=0;i<8;++i)c->block[63-i]=(uint8_t)(bits>>(i*8));sha256_compress(c,c->block);
    for(unsigned i=0;i<8;++i){out[i*4]=(uint8_t)(c->h[i]>>24);out[i*4+1]=(uint8_t)(c->h[i]>>16);out[i*4+2]=(uint8_t)(c->h[i]>>8);out[i*4+3]=(uint8_t)c->h[i];}
}
static bool sha256_selftest(void)
{
    static const uint8_t empty[32]={0xe3,0xb0,0xc4,0x42,0x98,0xfc,0x1c,0x14,0x9a,0xfb,0xf4,0xc8,0x99,0x6f,0xb9,0x24,0x27,0xae,0x41,0xe4,0x64,0x9b,0x93,0x4c,0xa4,0x95,0x99,0x1b,0x78,0x52,0xb8,0x55};
    static const uint8_t abc[32]={0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad};
    uint8_t out[32];sha256_context_t c;sha256_init(&c);sha256_final(&c,out);if(memcmp(out,empty,32))return false;
    sha256_init(&c);sha256_update(&c,(const uint8_t *)"abc",3);sha256_final(&c,out);return memcmp(out,abc,32)==0;
}

typedef enum {
    WORK_OPEN_DIR,
    WORK_NEXT_ENTRY,
    WORK_OPEN_FILE,
    WORK_READ_HEADER,
    WORK_CLOSE_FILE
} work_state_t;

typedef enum {
    CLEANUP_STOP,
    CLEANUP_ERROR
} cleanup_reason_t;

typedef struct {
    char path[DISCOVERY_FULL_PATH_CAP];
    dir_t iterator;
    uint16_t depth;
    int16_t top_index;
    bool owned;
} dir_frame_t;

typedef struct {
    char name[256];
    uint32_t directories;
    uint32_t paths;
    uint32_t candidates;
    uint32_t valid;
    uint64_t payload_bytes;
} top_level_t;

typedef struct {
    uint32_t path_offset;
    uint16_t path_length;
    uint8_t byte_order;
    uint8_t root_index;
    uint64_t size;
} retained_rom_t;

typedef struct {
    uint32_t request_bytes;
    uint64_t expected_bytes;
    uint64_t exact_bytes;
    uint64_t active_read_ticks;
    uint64_t wall_start_tick;
    uint64_t wall_ticks;
    uint64_t open_ticks;
    uint64_t close_ticks;
    uint64_t max_read_ticks;
    uint32_t read_calls;
    bool started;
    bool success;
    bool coverage_complete;
    bool io_error;
} read_benchmark_t;

typedef struct {
    uint64_t expected_bytes, input_bytes, active_stream_ticks, wall_start_tick, wall_ticks;
    uint64_t open_ticks, finalize_ticks, close_ticks, max_chunk_ticks;
    uint32_t read_calls;
    bool io_error, tail_error, size_error, success;
} hash_pass_t;

typedef struct {
    uint32_t retained_index;
    bool available, passes_match;
    uint8_t digest[2][32]; /* private: never emitted */
    hash_pass_t pass[2];
    uint64_t two_pass_wall_ticks;
} hash_class_t;

typedef struct {
    uint32_t directories;
    uint32_t searchable_entries;
    uint32_t excluded_entries;
    uint32_t max_depth;
    uint32_t max_logical_path_bytes;
    uint64_t logical_path_bytes;
    uint64_t path_storage_bytes;
    uint64_t payload_bytes;
    uint32_t extension_candidates;
    uint32_t root_direct_candidates;
    uint32_t root_direct_valid;
    uint32_t extension_z64;
    uint32_t extension_v64;
    uint32_t extension_n64;
    uint32_t extension_rom;
    uint32_t valid_magic;
    uint32_t magic_z64;
    uint32_t magic_v64;
    uint32_t magic_n64;
    uint32_t order_match;
    uint32_t order_mismatch;
    uint32_t short_header;
    uint32_t invalid_header;
    uint32_t open_errors;
    uint32_t read_errors;
    uint32_t size_bucket_lt16m;
    uint32_t size_bucket_16_32m;
    uint32_t size_bucket_32_64m;
    uint32_t size_bucket_ge64m;
    uint64_t valid_total_bytes;
    uint64_t valid_min_bytes;
    uint64_t valid_max_bytes;
} metrics_t;

static dir_frame_t dir_stack[DISCOVERY_MAX_DEPTH + 1];
static top_level_t top_levels[DISCOVERY_TOP_LEVEL_CAP];
static retained_rom_t retained[DISCOVERY_MAX_RECORDS];
static char retained_paths[DISCOVERY_MAX_PATH_BYTES];
static uint8_t read_buffer[DISCOVERY_READ_BUFFER_CAP];
static uint8_t normalize_buffer[DISCOVERY_HASH_CHUNK];
static char path_scratch[DISCOVERY_FULL_PATH_CAP];
static metrics_t metrics;

static terminal_state_t state = STATE_SCAN;
static work_state_t work = WORK_OPEN_DIR;
static read_state_t read_work = READ_OPEN;
static cleanup_reason_t cleanup_reason;
static unsigned stack_count;
static unsigned top_level_count;
static unsigned retained_count;
static unsigned retained_path_bytes;
static FILE *owned_file;
static char candidate_path[DISCOVERY_FULL_PATH_CAP];
static int64_t candidate_size;
static int candidate_top_index;
static int candidate_extension;
static size_t candidate_header_read;
static bool candidate_read_error;
static bool complete;
static bool population_complete = true;
static bool read_coverage_complete = true;
static bool sha_selftest_pass;
static bool hash_coverage_complete;
static bool hash_full_class_coverage;
static bool hash_any_available;
static bool cleanup_close_failed;
static bool stop_requested;
static bool usb_log_available;
static bool emu_log_available;
static bool sdfs_mount_owned;
static bool sdfs_close_attempted;
static char error_text[64] = "none";

static uint64_t scan_wall_start_tick;
static uint64_t scan_wall_ticks;
static uint64_t enum_active_ticks;
static uint64_t enum_max_call_ticks;
static uint32_t enum_call_count;
static uint32_t enum_returned_entries;
static unsigned selection_cursor;
static uint32_t representative_small = UINT32_MAX;
static uint32_t representative_medium = UINT32_MAX;
static uint32_t representative_large = UINT32_MAX;
static uint32_t sequential_target = UINT32_MAX;
static read_benchmark_t benchmark_4k = { .request_bytes = 4096 };
static read_benchmark_t benchmark_64k = { .request_bytes = 65536 };
static int heap_post_init;
static int heap_min;
static int heap_final;
static uint64_t last_frame_tick;
static uint64_t last_input_tick;
static uint64_t last_usb_opportunity_tick;
static uint64_t last_work_tick;
static uint64_t stop_request_tick;
static uint64_t max_frame_gap;
static uint64_t max_input_gap;
static uint64_t max_usb_opportunity_gap;
static uint64_t max_work_cadence;
static uint64_t max_work_duration;
static uint64_t request_to_safe_ticks;
static uint64_t max_close_ticks;
static hash_class_t hash_classes[DISCOVERY_HASH_CLASSES];
static hash_state_t hash_work;
static unsigned hash_class_cursor, hash_pass_cursor;
static sha256_context_t hash_context;
static uint8_t hash_carry[4];
static unsigned hash_carry_count;
static uint32_t inferred_equal_pairs, inferred_equal_groups;
static synth_state_t synth_work;
static synthetic_record_t *synth_records;
static char *synth_paths;
static uint8_t *synth_sink;
static uint32_t synth_fill_cursor, synth_encode_cursor, synth_pass_cursor;
static uint32_t synth_crc, synth_pass_crc[2];
static uint64_t synth_stream_bytes, synth_pass_bytes[2], synth_expected_bytes;
static uint64_t synth_pass_active_ticks[2], synth_pass_wall_ticks[2], synth_pass_wall_start;
static uint32_t synth_pass_operations[2];
static size_t synth_sink_cursor;
static int synth_heap_before = -1, synth_heap_min = -1, synth_heap_after_alloc = -1;
static int synth_heap_after_free = -1, synth_peak_delta = -1;
static bool synth_allocations_ok = true, synth_overflow, synth_crc_selftest_pass;
static bool synth_encode_match, synthetic_success;

typedef enum {
    REPORT_REPLAY_BEGIN,
    REPORT_INIT,
    REPORT_WARNING,
    REPORT_STOP_REQUEST,
    REPORT_SHA_SELFTEST,
    REPORT_ENUMERATION,
    REPORT_REPRESENTATIVES,
    REPORT_READS,
    REPORT_SHA_PASSES,
    REPORT_SHA_CLASSES,
    REPORT_SYNTHETIC_PASSES,
    REPORT_TERMINAL,
    REPORT_SYNTHETIC_SCHEMA,
    REPORT_SYNTHETIC_MEMORY,
    REPORT_SYNTHETIC_RESULT,
    REPORT_DUPLICATE,
    REPORT_LIFECYCLE,
    REPORT_SUMMARY,
    REPORT_EXTENSIONS,
    REPORT_MAGIC,
    REPORT_SIZE_BUCKETS,
    REPORT_TOP_LEVELS,
    REPORT_EFFECTIVE_ROOTS,
    REPORT_REPLAY_END
} report_stage_t;

typedef struct {
    uint64_t max_frame_gap;
    uint64_t max_input_gap;
    uint64_t max_usb_opportunity_gap;
    uint64_t max_work_cadence;
    uint64_t max_work_duration;
    uint64_t request_to_safe_ticks;
    uint64_t max_close_ticks;
    int heap_post_init;
    int heap_min;
    int heap_final;
} terminal_snapshot_t;

/* Terminal replay uses only retained metrics and these fixed-size cursors. */
static terminal_snapshot_t terminal_snapshot;
static bool terminal_snapshot_frozen;
static bool terminal_snapshot_pending;
static bool init_sd_ready;
static report_stage_t report_stage;
static uint32_t report_cycle;
static unsigned report_read_cursor;
static unsigned report_sha_class_cursor;
static unsigned report_sha_pass_cursor;
static unsigned report_synth_pass_cursor;
static unsigned report_top_cursor;
static unsigned report_effective_cursor;
static unsigned report_effective_emitted;
static char report_name_hex[511];

static void begin_hash_phase(void);
static void hash_one_operation(void);
static void synthetic_one_operation(void);
static bool crc32_selftest(void);

static uint64_t tick_delta(uint64_t newer, uint64_t older)
{
    return newer - older;
}

static void update_max(uint64_t *value, uint64_t candidate)
{
    if (candidate > *value) *value = candidate;
}

static bool ascii_case_equal(const char *a, const char *b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
        ++a;
        ++b;
    }
    return *a == *b;
}

static bool excluded_at_storage_root(const char *name)
{
    for (unsigned i = 0; i < sizeof(root_exclusions) / sizeof(root_exclusions[0]); ++i) {
        if (strcmp(name, root_exclusions[i]) == 0) return true;
    }
    return false;
}

static bool excluded_basename(const char *name)
{
    return strcmp(name, "desktop.ini") == 0 || strcmp(name, "Thumbs.db") == 0 ||
           strcmp(name, ".DS_Store") == 0 ||
           (strncmp(name, "._", 2) == 0 && name[2] != '\0');
}

static bool entry_is_excluded(const dir_frame_t *frame, const char *name)
{
    return excluded_basename(name) ||
           (frame->depth == 0 && excluded_at_storage_root(name));
}

static int extension_kind(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (!dot || !dot[1]) return 0;
    if (ascii_case_equal(dot + 1, "z64")) return 1;
    if (ascii_case_equal(dot + 1, "v64")) return 2;
    if (ascii_case_equal(dot + 1, "n64")) return 3;
    if (ascii_case_equal(dot + 1, "rom")) return 4;
    return 0;
}

static bool make_child_path(char *out, size_t out_size, const char *parent,
                            const char *name)
{
    size_t parent_len = strlen(parent);
    size_t name_len = strlen(name);
    bool slash = parent_len > 0 && parent[parent_len - 1] != '/';
    if (name_len == 0 || strchr(name, '/') || strchr(name, '\\')) return false;
    if (parent_len + (slash ? 1u : 0u) + name_len + 1u > out_size) return false;
    memcpy(out, parent, parent_len);
    if (slash) out[parent_len++] = '/';
    memcpy(out + parent_len, name, name_len + 1);
    return true;
}

static void set_failure(const char *message)
{
    if (state == STATE_CLEANUP || state == STATE_QUIESCE_FAILED_SAFE) return;
    if (state == STATE_SCAN && scan_wall_ticks == 0)
        scan_wall_ticks = tick_delta(get_ticks(), scan_wall_start_tick);
    population_complete = false;
    snprintf(error_text, sizeof(error_text), "%s", message);
    complete = false;
    cleanup_reason = CLEANUP_ERROR;
    state = STATE_CLEANUP;
}

static void request_stop(void)
{
    if (state == STATE_CLEANUP || state == STATE_ERROR || state == STATE_QUIESCED ||
        state == STATE_QUIESCE_FAILED_SAFE || state == STATE_COMPLETE) return;
    stop_request_tick = get_ticks();
    if (state == STATE_SCAN && scan_wall_ticks == 0)
        scan_wall_ticks = tick_delta(stop_request_tick, scan_wall_start_tick);
    stop_requested = true;
    complete = false;
    cleanup_reason = CLEANUP_STOP;
    state = STATE_CLEANUP;
    snprintf(error_text, sizeof(error_text), "user stop");
}

static unsigned owned_handle_count(void)
{
    unsigned count = owned_file ? 1u : 0u;
    for (unsigned i = 0; i < stack_count; ++i) {
        if (dir_stack[i].owned) ++count;
    }
    return count;
}

static unsigned owned_allocation_count(void)
{
    return (synth_records ? 1u : 0u) + (synth_paths ? 1u : 0u) +
           (synth_sink ? 1u : 0u);
}

static void note_work(uint64_t start, uint64_t end)
{
    update_max(&max_work_cadence, tick_delta(start, last_work_tick));
    update_max(&max_work_duration, tick_delta(end, start));
    last_work_tick = start;
}

static void note_enumeration(uint64_t start, uint64_t end, int result)
{
    uint64_t elapsed = tick_delta(end, start);
    ++enum_call_count;
    enum_active_ticks += elapsed;
    update_max(&enum_max_call_ticks, elapsed);
    if (result == 0) ++enum_returned_entries;
}

static bool free_one_synthetic_allocation(void)
{
    uint64_t start = get_ticks();
    void *allocation = NULL;
    if (synth_sink) { allocation = synth_sink; synth_sink = NULL; }
    else if (synth_paths) { allocation = synth_paths; synth_paths = NULL; }
    else if (synth_records) { allocation = synth_records; synth_records = NULL; }
    if (!allocation) return false;
    free(allocation);
    uint64_t end = get_ticks();
    note_work(start, end);
    update_max(&max_close_ticks, tick_delta(end, start));
    return true;
}

static void cleanup_one_handle(void)
{
    uint64_t start;
    uint64_t elapsed;

    if (free_one_synthetic_allocation()) return;

    if (owned_file) {
        FILE *file = owned_file;
        owned_file = NULL;
        start = get_ticks();
        int result = fclose(file);
        elapsed = tick_delta(get_ticks(), start);
        update_max(&max_close_ticks, elapsed);
        note_work(start, start + elapsed);
        if (result != 0) cleanup_close_failed = true;
        return;
    }

    for (unsigned i = stack_count; i > 0; --i) {
        dir_frame_t *frame = &dir_stack[i - 1];
        if (!frame->owned) continue;
        frame->owned = false; /* pinned contract clears backend ownership even on failure */
        start = get_ticks();
        int result = dir_findclose(frame->path, &frame->iterator);
        elapsed = tick_delta(get_ticks(), start);
        update_max(&max_close_ticks, elapsed);
        note_work(start, start + elapsed);
        if (result != 0) cleanup_close_failed = true;
        return;
    }

    if (sdfs_mount_owned) {
        start = get_ticks();
        debug_close_sdfs();
        elapsed = tick_delta(get_ticks(), start);
        update_max(&max_close_ticks, elapsed);
        note_work(start, start + elapsed);
        sdfs_mount_owned = false;
        sdfs_close_attempted = true;
        return;
    }

    heap_stats_t heap;
    sys_get_heap_stats(&heap);
    heap_final = heap.total - heap.used;
    if (stop_requested)
        request_to_safe_ticks = tick_delta(get_ticks(), stop_request_tick);
    if (cleanup_close_failed) {
        state = STATE_QUIESCE_FAILED_SAFE;
        snprintf(error_text, sizeof(error_text), "close failure; ownership cleared");
    } else if (cleanup_reason == CLEANUP_STOP) {
        state = STATE_QUIESCED;
    } else {
        state = STATE_ERROR;
    }
}

static bool account_path(const char *path, int64_t payload, int top_index)
{
    size_t full_bytes = strlen(path);
    size_t root_prefix_bytes = strlen(scan_roots[0]);
    size_t logical_bytes = full_bytes >= root_prefix_bytes ? full_bytes - root_prefix_bytes : 0;
    size_t storage_bytes = logical_bytes + 1;
    if (metrics.searchable_entries >= DISCOVERY_MAX_PATHS) {
        set_failure("visited path cap");
        return false;
    }
    ++metrics.searchable_entries;
    metrics.logical_path_bytes += logical_bytes;
    metrics.path_storage_bytes += storage_bytes;
    if (logical_bytes > metrics.max_logical_path_bytes)
        metrics.max_logical_path_bytes = logical_bytes;
    if (payload > 0) metrics.payload_bytes += (uint64_t)payload;
    if (top_index >= 0) {
        top_levels[top_index].paths++;
        if (payload > 0) top_levels[top_index].payload_bytes += (uint64_t)payload;
    }
    return true;
}

static int record_top_level(const char *name)
{
    if (top_level_count >= DISCOVERY_TOP_LEVEL_CAP) {
        set_failure("top-level name cap");
        return -1;
    }
    top_level_t *top = &top_levels[top_level_count];
    snprintf(top->name, sizeof(top->name), "%s", name);
    return (int)top_level_count++;
}

static void begin_candidate(const char *path, int64_t size, int top_index, int ext)
{
    snprintf(candidate_path, sizeof(candidate_path), "%s", path);
    candidate_size = size;
    candidate_top_index = top_index;
    candidate_extension = ext;
    ++metrics.extension_candidates;
    if (top_index >= 0) ++top_levels[top_index].candidates;
    else ++metrics.root_direct_candidates;
    if (ext == 1) ++metrics.extension_z64;
    else if (ext == 2) ++metrics.extension_v64;
    else if (ext == 3) ++metrics.extension_n64;
    else ++metrics.extension_rom;
    work = WORK_OPEN_FILE;
}

static void process_entry(dir_frame_t *frame)
{
    const char *name = frame->iterator.d_name;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        work = WORK_NEXT_ENTRY;
        return;
    }
    if (entry_is_excluded(frame, name)) {
        ++metrics.excluded_entries;
        work = WORK_NEXT_ENTRY;
        return;
    }

    if (!make_child_path(path_scratch, sizeof(path_scratch), frame->path, name)) {
        set_failure("full path cap/invalid name");
        return;
    }

    int top_index = frame->top_index;
    if (frame->depth == 0 && frame->iterator.d_type == DT_DIR) {
        top_index = record_top_level(name);
        if (state == STATE_CLEANUP) return;
    }

    if (!account_path(path_scratch, frame->iterator.d_type == DT_REG ? frame->iterator.d_size : 0,
                      top_index)) return;

    if (frame->iterator.d_type == DT_DIR) {
        ++metrics.directories;
        if (top_index >= 0) ++top_levels[top_index].directories;
        uint32_t child_depth = frame->depth + 1u;
        if (child_depth > metrics.max_depth) metrics.max_depth = child_depth;
        if (child_depth > DISCOVERY_MAX_DEPTH) {
            set_failure("directory depth cap");
            return;
        }
        if (stack_count >= DISCOVERY_MAX_DEPTH + 1u) {
            set_failure("directory stack cap");
            return;
        }
        dir_frame_t *child = &dir_stack[stack_count++];
        memset(child, 0, sizeof(*child));
        snprintf(child->path, sizeof(child->path), "%s", path_scratch);
        child->depth = (uint16_t)child_depth;
        child->top_index = (int16_t)top_index;
        work = WORK_OPEN_DIR;
        return;
    }

    int ext = extension_kind(name);
    if (ext) begin_candidate(path_scratch, frame->iterator.d_size, top_index, ext);
    else work = WORK_NEXT_ENTRY;
}

static void classify_header(void)
{
    int magic = 0;
    if (candidate_read_error) {
        if (candidate_header_read != DISCOVERY_HEADER_BYTES) ++metrics.short_header;
        return;
    }
    if (candidate_header_read != DISCOVERY_HEADER_BYTES) {
        ++metrics.short_header;
        return;
    }
    if (read_buffer[0] == 0x80 && read_buffer[1] == 0x37 &&
        read_buffer[2] == 0x12 && read_buffer[3] == 0x40) magic = 1;
    else if (read_buffer[0] == 0x37 && read_buffer[1] == 0x80 &&
             read_buffer[2] == 0x40 && read_buffer[3] == 0x12) magic = 2;
    else if (read_buffer[0] == 0x40 && read_buffer[1] == 0x12 &&
             read_buffer[2] == 0x37 && read_buffer[3] == 0x80) magic = 3;
    if (!magic) {
        ++metrics.invalid_header;
        return;
    }

    if (retained_count >= DISCOVERY_MAX_RECORDS) {
        set_failure("valid record cap");
        return;
    }
    size_t path_bytes = strlen(candidate_path) + 1;
    if (path_bytes > DISCOVERY_MAX_PATH_BYTES - retained_path_bytes) {
        set_failure("retained path byte cap");
        return;
    }

    retained_rom_t *record = &retained[retained_count++];
    record->path_offset = retained_path_bytes;
    record->path_length = (uint16_t)(path_bytes - 1);
    record->byte_order = (uint8_t)magic;
    record->root_index = candidate_top_index >= 0 ? (uint8_t)candidate_top_index : UINT8_MAX;
    record->size = candidate_size > 0 ? (uint64_t)candidate_size : 0;
    memcpy(retained_paths + retained_path_bytes, candidate_path, path_bytes);
    retained_path_bytes += path_bytes;

    ++metrics.valid_magic;
    if (candidate_top_index >= 0) ++top_levels[candidate_top_index].valid;
    else ++metrics.root_direct_valid;
    if (magic == 1) ++metrics.magic_z64;
    else if (magic == 2) ++metrics.magic_v64;
    else ++metrics.magic_n64;
    if (candidate_extension == 4 || candidate_extension == magic) ++metrics.order_match;
    else ++metrics.order_mismatch;

    uint64_t size = candidate_size > 0 ? (uint64_t)candidate_size : 0;
    metrics.valid_total_bytes += size;
    if (metrics.valid_magic == 1 || size < metrics.valid_min_bytes) metrics.valid_min_bytes = size;
    if (size > metrics.valid_max_bytes) metrics.valid_max_bytes = size;
    if (size < 16u * 1024u * 1024u) ++metrics.size_bucket_lt16m;
    else if (size < 32u * 1024u * 1024u) ++metrics.size_bucket_16_32m;
    else if (size < 64u * 1024u * 1024u) ++metrics.size_bucket_32_64m;
    else ++metrics.size_bucket_ge64m;
}

static void complete_scan(void)
{
    scan_wall_ticks = tick_delta(get_ticks(), scan_wall_start_tick);
    /* Overall completion is established only after every measurement and SDFS close. */
    complete = false;
    state = STATE_SELECTION;
    selection_cursor = 0;
}

static uint64_t distance_from(uint64_t value, uint64_t target)
{
    return value > target ? value - target : target - value;
}

static bool nearer_representative(uint32_t current, uint32_t candidate, uint64_t target)
{
    if (current == UINT32_MAX) return true;
    uint64_t candidate_distance = distance_from(retained[candidate].size, target);
    uint64_t current_distance = distance_from(retained[current].size, target);
    if (candidate_distance != current_distance) return candidate_distance < current_distance;
    if (retained[candidate].size != retained[current].size)
        return retained[candidate].size < retained[current].size;
    return candidate < current;
}

static void select_one_record(void)
{
    if (selection_cursor < retained_count) {
        uint32_t index = selection_cursor++;
        uint64_t size = retained[index].size;
        if (size < 16u * 1024u * 1024u) {
            if (nearer_representative(representative_small, index, 8u * 1024u * 1024u))
                representative_small = index;
        } else if (size < 32u * 1024u * 1024u) {
            if (nearer_representative(representative_medium, index, 24u * 1024u * 1024u))
                representative_medium = index;
        } else if (representative_large == UINT32_MAX ||
                   size > retained[representative_large].size ||
                   (size == retained[representative_large].size && index < representative_large)) {
            representative_large = index;
        }
        if (sequential_target == UINT32_MAX || size > retained[sequential_target].size ||
            (size == retained[sequential_target].size && index < sequential_target))
            sequential_target = index;
        return;
    }

    if (sequential_target == UINT32_MAX || !population_complete) {
        read_coverage_complete = false;
        benchmark_4k.coverage_complete = benchmark_64k.coverage_complete = false;
        benchmark_4k.success = benchmark_64k.success = false;
        if (sequential_target != UINT32_MAX) {
            benchmark_4k.expected_bytes = retained[sequential_target].size;
            benchmark_64k.expected_bytes = retained[sequential_target].size;
        }
        begin_hash_phase();
        return;
    }
    benchmark_4k.expected_bytes = retained[sequential_target].size;
    benchmark_64k.expected_bytes = retained[sequential_target].size;
    read_work = READ_OPEN;
    state = STATE_READ_4K;
}

static read_benchmark_t *current_benchmark(void)
{
    return state == STATE_READ_4K ? &benchmark_4k : &benchmark_64k;
}

static void finish_benchmark_stage(read_benchmark_t *benchmark, uint64_t end)
{
    benchmark->wall_ticks = tick_delta(end, benchmark->wall_start_tick);
    if (state == STATE_READ_4K) {
        read_work = READ_OPEN;
        state = STATE_READ_64K;
    } else {
        begin_hash_phase();
    }
}

static void benchmark_one_operation(void)
{
    read_benchmark_t *benchmark = current_benchmark();
    uint64_t start = get_ticks();
    uint64_t end;

    switch (read_work) {
    case READ_OPEN:
        benchmark->started = true;
        benchmark->wall_start_tick = start;
        owned_file = fopen(retained_paths + retained[sequential_target].path_offset, "rb");
        end = get_ticks();
        benchmark->open_ticks = tick_delta(end, start);
        note_work(start, end);
        if (!owned_file) {
            benchmark->io_error = true;
            read_work = READ_FINALIZE;
        } else {
            read_work = READ_DATA;
        }
        break;

    case READ_DATA: {
        size_t amount = fread(read_buffer, 1, benchmark->request_bytes, owned_file);
        end = get_ticks();
        uint64_t elapsed = tick_delta(end, start);
        benchmark->active_read_ticks += elapsed;
        update_max(&benchmark->max_read_ticks, elapsed);
        ++benchmark->read_calls;
        benchmark->exact_bytes += amount;
        if (ferror(owned_file)) {
            benchmark->io_error = true;
            read_coverage_complete = false;
            complete = false;
        }
        note_work(start, end);
        if (benchmark->io_error || amount < benchmark->request_bytes)
            read_work = READ_FINALIZE;
        break;
    }

    case READ_FINALIZE:
        benchmark->coverage_complete = !benchmark->io_error &&
            benchmark->exact_bytes == benchmark->expected_bytes;
        benchmark->success = benchmark->coverage_complete;
        if (!benchmark->coverage_complete) {
            read_coverage_complete = false;
            complete = false;
        }
        read_work = READ_CLOSE;
        break;

    case READ_CLOSE:
        if (owned_file) {
            FILE *file = owned_file;
            owned_file = NULL;
            int result = fclose(file);
            end = get_ticks();
            benchmark->close_ticks = tick_delta(end, start);
            update_max(&max_close_ticks, benchmark->close_ticks);
            note_work(start, end);
            if (result != 0) {
                benchmark->io_error = true;
                benchmark->success = false;
                benchmark->coverage_complete = false;
                read_coverage_complete = false;
                complete = false;
            }
        } else {
            end = get_ticks();
        }
        finish_benchmark_stage(benchmark, end);
        break;
    }
}

static const char *hash_class_name(unsigned index)
{
    static const char *const names[3] = { "small", "medium", "large" };
    return names[index];
}

static void begin_hash_phase(void)
{
    uint32_t indices[3] = { representative_small, representative_medium, representative_large };
    memset(hash_classes, 0, sizeof(hash_classes));
    hash_any_available = false;
    hash_full_class_coverage = true;
    hash_coverage_complete = true;
    for (unsigned i=0;i<3;++i) {
        hash_classes[i].retained_index = indices[i];
        hash_classes[i].available = indices[i] != UINT32_MAX;
        hash_any_available |= hash_classes[i].available;
        if (!hash_classes[i].available) hash_full_class_coverage = false;
        for (unsigned p=0;p<2;++p)
            hash_classes[i].pass[p].expected_bytes = hash_classes[i].available ? retained[indices[i]].size : 0;
    }
    if (!hash_any_available) hash_coverage_complete = false;
    hash_class_cursor=0; hash_pass_cursor=0; hash_work=HASH_OPEN; state=STATE_HASH;
}

static void normalize_hash_chunk(uint8_t order, const uint8_t *input, size_t amount)
{
    if (order == 1) { memcpy(normalize_buffer,input,amount); sha256_update(&hash_context,normalize_buffer,amount); return; }
    unsigned unit = order == 2 ? 2u : 4u;
    size_t output = 0;
    for (size_t i=0;i<amount;++i) {
        hash_carry[hash_carry_count++] = input[i];
        if (hash_carry_count == unit) {
            for (unsigned j=0;j<unit;++j) normalize_buffer[output++] = hash_carry[unit-1u-j];
            hash_carry_count = 0;
        }
    }
    if (output) sha256_update(&hash_context,normalize_buffer,output);
}

static void begin_synthetic_phase(void)
{
    heap_stats_t heap;
    sys_get_heap_stats(&heap);
    synth_heap_before = heap.total - heap.used;
    synth_heap_min = synth_heap_before;
    synth_work = SYNTH_ALLOC_RECORDS;
    synth_crc_selftest_pass = crc32_selftest();
    uint64_t per_record = (uint64_t)SYNTH_METADATA_ENCODED + SYNTH_PATH_LENGTH;
    if (per_record < SYNTH_METADATA_ENCODED || per_record == 0 ||
        SYNTH_RECORD_CAP > (UINT64_MAX - SYNTH_HEADER_ENCODED) / per_record ||
        (uint64_t)SYNTH_PATH_ARENA_BYTES != (uint64_t)SYNTH_RECORD_CAP * SYNTH_PATH_BYTES) {
        synth_overflow = true;
        synth_work = SYNTH_FREE;
    } else {
        synth_expected_bytes = SYNTH_HEADER_ENCODED + (uint64_t)SYNTH_RECORD_CAP * per_record;
    }
    state = STATE_SYNTHETIC;
}

static void finish_hash_phase(void)
{
    inferred_equal_pairs = inferred_equal_groups = 0;
    bool grouped[3] = {false,false,false};
    for (unsigned i=0;i<3;++i) for (unsigned j=i+1;j<3;++j) {
        hash_class_t *a=&hash_classes[i], *b=&hash_classes[j];
        if (a->available && b->available && a->passes_match && b->passes_match &&
            a->pass[1].success && b->pass[1].success &&
            a->pass[1].input_bytes == b->pass[1].input_bytes &&
            memcmp(a->digest[1],b->digest[1],32)==0) {
            ++inferred_equal_pairs; grouped[i]=grouped[j]=true;
        }
    }
    for (unsigned i=0;i<3;++i) if (grouped[i]) {
        ++inferred_equal_groups;
        for (unsigned j=i+1;j<3;++j)
            if (grouped[j] && hash_classes[i].pass[1].input_bytes==hash_classes[j].pass[1].input_bytes &&
                memcmp(hash_classes[i].digest[1],hash_classes[j].digest[1],32)==0) grouped[j]=false;
    }
    begin_synthetic_phase();
}

static void advance_hash_pass(uint64_t end)
{
    hash_class_t *hc=&hash_classes[hash_class_cursor]; hash_pass_t *hp=&hc->pass[hash_pass_cursor];
    hp->wall_ticks=tick_delta(end,hp->wall_start_tick);
    if (!hp->success) hash_coverage_complete=false;
    if (hash_pass_cursor==0) { hash_pass_cursor=1; hash_work=HASH_OPEN; return; }
    hc->passes_match=hc->pass[0].success && hp->success && memcmp(hc->digest[0],hc->digest[1],32)==0;
    if (!hc->passes_match) hash_coverage_complete=false;
    hc->two_pass_wall_ticks=hc->pass[0].wall_ticks+hc->pass[1].wall_ticks;
    ++hash_class_cursor; hash_pass_cursor=0; hash_work=HASH_OPEN;
}

static void hash_one_operation(void)
{
    while (hash_class_cursor<3 && !hash_classes[hash_class_cursor].available) {
        ++hash_class_cursor;
    }
    if (hash_class_cursor>=3) { finish_hash_phase(); return; }
    hash_class_t *hc=&hash_classes[hash_class_cursor]; hash_pass_t *hp=&hc->pass[hash_pass_cursor];
    retained_rom_t *rom=&retained[hc->retained_index]; uint64_t start=get_ticks(),end;
    switch(hash_work) {
    case HASH_OPEN:
        hp->wall_start_tick=start; sha256_init(&hash_context); hash_carry_count=0;
        owned_file=fopen(retained_paths+rom->path_offset,"rb"); end=get_ticks(); hp->open_ticks=tick_delta(end,start); note_work(start,end);
        if(!owned_file){hp->io_error=true;hash_work=HASH_FINALIZE;}else hash_work=HASH_DATA;
        break;
    case HASH_DATA: {
        size_t amount=fread(read_buffer,1,DISCOVERY_HASH_CHUNK,owned_file);
        if (UINT64_MAX-hp->input_bytes<amount) hp->size_error=true; else hp->input_bytes+=amount;
        if(amount) normalize_hash_chunk(rom->byte_order,read_buffer,amount);
        if(ferror(owned_file)) hp->io_error=true;
        end=get_ticks();uint64_t elapsed=tick_delta(end,start);hp->active_stream_ticks+=elapsed;update_max(&hp->max_chunk_ticks,elapsed);++hp->read_calls;note_work(start,end);
        if(hp->io_error||hp->size_error||amount<DISCOVERY_HASH_CHUNK)hash_work=HASH_FINALIZE;
        break; }
    case HASH_FINALIZE:
        hp->tail_error=rom->byte_order!=1 && hash_carry_count!=0;
        hp->size_error |= hp->input_bytes!=hp->expected_bytes;
        sha256_final(&hash_context,hc->digest[hash_pass_cursor]);end=get_ticks();hp->finalize_ticks=tick_delta(end,start);note_work(start,end);
        hp->success=!hp->io_error&&!hp->tail_error&&!hp->size_error;hash_work=HASH_CLOSE;break;
    case HASH_CLOSE:
        if(owned_file){FILE *file=owned_file;owned_file=NULL;int result=fclose(file);end=get_ticks();hp->close_ticks=tick_delta(end,start);update_max(&max_close_ticks,hp->close_ticks);note_work(start,end);
            if(result!=0){hp->io_error=true;hp->success=false;}}
        else end=get_ticks();
        advance_hash_pass(end);break;
    }
}

static uint32_t crc32_byte(uint32_t crc, uint8_t byte)
{
    crc ^= byte;
    for (unsigned bit = 0; bit < 8; ++bit)
        crc = (crc >> 1) ^ (0xedb88320u & (uint32_t)-(int32_t)(crc & 1u));
    return crc;
}

static bool crc32_selftest(void)
{
    uint32_t crc = UINT32_MAX;
    const char *text = "123456789";
    for (unsigned i = 0; i < 9; ++i) crc = crc32_byte(crc, (uint8_t)text[i]);
    return (crc ^ UINT32_MAX) == 0xcbf43926u;
}

static void synth_put_u8(uint8_t value)
{
    if (!synth_sink || synth_stream_bytes == UINT64_MAX) { synth_overflow = true; return; }
    synth_sink[synth_sink_cursor++] = value;
    if (synth_sink_cursor == SYNTH_SINK_BYTES) synth_sink_cursor = 0;
    synth_crc = crc32_byte(synth_crc, value);
    ++synth_stream_bytes;
}

static void synth_put_u16(uint16_t value)
{
    synth_put_u8((uint8_t)(value >> 8)); synth_put_u8((uint8_t)value);
}

static void synth_put_u32(uint32_t value)
{
    for (int shift = 24; shift >= 0; shift -= 8) synth_put_u8((uint8_t)(value >> shift));
}

static void synth_put_u64(uint64_t value)
{
    for (int shift = 56; shift >= 0; shift -= 8) synth_put_u8((uint8_t)(value >> shift));
}

static void sample_synth_heap(void)
{
    heap_stats_t heap;
    sys_get_heap_stats(&heap);
    int free_heap = heap.total - heap.used;
    if (synth_heap_min < 0 || free_heap < synth_heap_min) synth_heap_min = free_heap;
}

static void synth_allocation_failed(void)
{
    synth_allocations_ok = false;
    snprintf(error_text, sizeof(error_text), "synthetic allocation failed");
    synth_work = SYNTH_FREE;
}

static void synth_encode_header(void)
{
    static const uint8_t magic[8] = {'A','6','4','C','A','C','H','E'};
    for (unsigned i = 0; i < sizeof(magic); ++i) synth_put_u8(magic[i]);
    synth_put_u32(1); synth_put_u32(SYNTH_RECORD_CAP); synth_put_u32(SYNTH_RECORD_CAP);
    synth_put_u32(SYNTH_PATH_ARENA_BYTES); synth_put_u32(SYNTH_SINK_BYTES);
    synth_put_u32(SYNTH_METADATA_ENCODED);
}

static void synth_encode_record(uint32_t index)
{
    const synthetic_record_t *r = &synth_records[index];
    synth_put_u32(r->path_offset); synth_put_u16(r->path_length);
    synth_put_u8(r->byte_order); synth_put_u8(r->flags); synth_put_u64(r->size);
    synth_put_u32(r->header_word); synth_put_u32(r->cic_word);
    const uint8_t *path = (const uint8_t *)(synth_paths + r->path_offset);
    for (unsigned i = 0; i < SYNTH_PATH_LENGTH; ++i) synth_put_u8(path[i]);
}

static void synthetic_one_operation(void)
{
    uint64_t start = get_ticks(), end;
    switch (synth_work) {
    case SYNTH_ALLOC_RECORDS:
        synth_records = malloc(sizeof(*synth_records) * SYNTH_RECORD_CAP);
        end = get_ticks(); note_work(start, end); sample_synth_heap();
        if (!synth_records) synth_allocation_failed(); else synth_work = SYNTH_ALLOC_PATHS;
        break;
    case SYNTH_ALLOC_PATHS:
        synth_paths = malloc(SYNTH_PATH_ARENA_BYTES);
        end = get_ticks(); note_work(start, end); sample_synth_heap();
        if (!synth_paths) synth_allocation_failed(); else synth_work = SYNTH_ALLOC_SINK;
        break;
    case SYNTH_ALLOC_SINK:
        synth_sink = malloc(SYNTH_SINK_BYTES);
        end = get_ticks(); note_work(start, end); sample_synth_heap();
        if (!synth_sink) synth_allocation_failed();
        else {
            heap_stats_t heap; sys_get_heap_stats(&heap);
            synth_heap_after_alloc = heap.total - heap.used;
            synth_peak_delta = synth_heap_before >= synth_heap_min ? synth_heap_before - synth_heap_min : -1;
            synth_work = SYNTH_FILL;
        }
        break;
    case SYNTH_FILL: {
        uint32_t i = synth_fill_cursor;
        synthetic_record_t *r = &synth_records[i];
        r->path_offset = i * SYNTH_PATH_BYTES; r->path_length = SYNTH_PATH_LENGTH;
        r->byte_order = (uint8_t)(1u + i % 3u); r->flags = (uint8_t)(i & 7u);
        r->size = (uint64_t)(i + 1u) * 1024u * 1024u;
        r->header_word = 0x80371240u ^ i; r->cic_word = 0x6170a4a1u + i * 2654435761u;
        char *path = synth_paths + r->path_offset;
        for (unsigned j = 0; j < SYNTH_PATH_LENGTH; ++j)
            path[j] = (char)(33u + ((i * 37u + j * 17u) % 94u));
        path[SYNTH_PATH_LENGTH] = '\0';
        ++synth_fill_cursor; end = get_ticks(); note_work(start, end);
        if (synth_fill_cursor == SYNTH_RECORD_CAP) synth_work = SYNTH_PASS_BEGIN;
        break; }
    case SYNTH_PASS_BEGIN:
        synth_stream_bytes = 0; synth_crc = UINT32_MAX; synth_sink_cursor = 0;
        synth_encode_cursor = 0; synth_pass_wall_start = start;
        synth_work = SYNTH_PASS_HEADER;
        break;
    case SYNTH_PASS_HEADER:
        synth_encode_header(); end = get_ticks();
        synth_pass_active_ticks[synth_pass_cursor] += tick_delta(end, start);
        ++synth_pass_operations[synth_pass_cursor]; note_work(start, end);
        synth_work = SYNTH_PASS_RECORD;
        break;
    case SYNTH_PASS_RECORD:
        synth_encode_record(synth_encode_cursor++); end = get_ticks();
        synth_pass_active_ticks[synth_pass_cursor] += tick_delta(end, start);
        ++synth_pass_operations[synth_pass_cursor]; note_work(start, end);
        if (synth_encode_cursor == SYNTH_RECORD_CAP) synth_work = SYNTH_PASS_FINISH;
        break;
    case SYNTH_PASS_FINISH:
        synth_pass_crc[synth_pass_cursor] = synth_crc ^ UINT32_MAX;
        synth_pass_bytes[synth_pass_cursor] = synth_stream_bytes;
        synth_pass_wall_ticks[synth_pass_cursor] = tick_delta(start, synth_pass_wall_start);

        if (++synth_pass_cursor < SYNTH_ENCODE_PASSES) synth_work = SYNTH_PASS_BEGIN;
        else {
            synth_crc_selftest_pass = crc32_selftest();
            synth_encode_match = synth_pass_bytes[0] == synth_pass_bytes[1] &&
                                 synth_pass_crc[0] == synth_pass_crc[1];
            synthetic_success = synth_allocations_ok && synth_fill_cursor == SYNTH_RECORD_CAP &&
                !synth_overflow && synth_crc_selftest_pass && synth_encode_match &&
                synth_pass_bytes[0] == synth_expected_bytes && synth_pass_bytes[1] == synth_expected_bytes;
            synth_work = SYNTH_FREE;
        }
        break;
    case SYNTH_FREE:
        if (free_one_synthetic_allocation()) break;
        { heap_stats_t heap; sys_get_heap_stats(&heap); synth_heap_after_free = heap.total - heap.used; }
        state = STATE_CLOSE_SDFS;
        break;
    }
}

static void close_sdfs_and_complete(void)
{
    uint64_t start = get_ticks();
    if (sdfs_mount_owned) {
        debug_close_sdfs();
        uint64_t end = get_ticks(); note_work(start, end);
        update_max(&max_close_ticks, tick_delta(end, start));
        sdfs_mount_owned = false; sdfs_close_attempted = true;
    }
    heap_stats_t heap; sys_get_heap_stats(&heap); heap_final = heap.total - heap.used;
    complete = population_complete && read_coverage_complete && benchmark_4k.success &&
        benchmark_64k.success && sha_selftest_pass && hash_any_available &&
        hash_coverage_complete && synthetic_success;
    state = STATE_COMPLETE;
}

static void scan_one_operation(void)
{
    if (stack_count == 0) {
        complete_scan();
        return;
    }

    dir_frame_t *frame = &dir_stack[stack_count - 1];
    uint64_t start = get_ticks();
    int result;

    switch (work) {
    case WORK_OPEN_DIR: {
        result = dir_findfirst(frame->path, &frame->iterator);
        uint64_t end = get_ticks();
        note_work(start, end);
        note_enumeration(start, end, result);
        if (result == 0) {
            frame->owned = true;
            process_entry(frame);
        } else if (result == -1) {
            frame->owned = false;
            --stack_count;
            work = stack_count ? WORK_NEXT_ENTRY : WORK_OPEN_DIR;
            if (!stack_count) complete_scan();
        } else {
            frame->owned = false;
            set_failure("directory open error");
        }
        break;
    }

    case WORK_NEXT_ENTRY: {
        result = dir_findnext(frame->path, &frame->iterator);
        uint64_t end = get_ticks();
        note_work(start, end);
        note_enumeration(start, end, result);
        if (result == 0) {
            process_entry(frame);
        } else {
            frame->owned = false; /* pinned FAT EOF/error closes and clears ownership */
            --stack_count;
            work = stack_count ? WORK_NEXT_ENTRY : WORK_OPEN_DIR;
            if (result < -1) {
                cleanup_close_failed = true; /* includes an EOF-close failure */
                set_failure("directory read/close error");
            } else if (!stack_count) {
                complete_scan();
            }
        }
        break;
    }

    case WORK_OPEN_FILE:
        owned_file = fopen(candidate_path, "rb");
        note_work(start, get_ticks());
        if (!owned_file) {
            ++metrics.open_errors;
            population_complete = false;
            complete = false;
            work = WORK_NEXT_ENTRY; /* bounded continuation; benchmark readiness is blocked */
        } else {
            work = WORK_READ_HEADER;
        }
        break;

    case WORK_READ_HEADER:
        candidate_header_read = fread(read_buffer, 1, DISCOVERY_HEADER_BYTES, owned_file);
        candidate_read_error = ferror(owned_file) != 0;
        if (candidate_read_error) {
            ++metrics.read_errors;
            population_complete = false;
            complete = false;
        }
        note_work(start, get_ticks());
        work = WORK_CLOSE_FILE;
        break;

    case WORK_CLOSE_FILE: {
        FILE *file = owned_file;
        owned_file = NULL;
        result = fclose(file);
        uint64_t elapsed = tick_delta(get_ticks(), start);
        update_max(&max_close_ticks, elapsed);
        note_work(start, start + elapsed);
        if (result != 0) {
            population_complete = false;
            complete = false;
            cleanup_close_failed = true;
            set_failure("file close error");
        } else {
            classify_header();
            if (state == STATE_SCAN) work = WORK_NEXT_ENTRY;
        }
        break;
    }
    }
}

static const char *state_name(void)
{
    switch (state) {
    case STATE_SCAN: return "SCAN";
    case STATE_SELECTION: return "SELECTION";
    case STATE_READ_4K: return "READ_4K";
    case STATE_READ_64K: return "READ_64K";
    case STATE_HASH: return "HASH";
    case STATE_SYNTHETIC: return "SYNTHETIC";
    case STATE_CLOSE_SDFS: return "CLOSE_SDFS";
    case STATE_COMPLETE: return "COMPLETE";
    case STATE_ERROR: return "ERROR";
    case STATE_CLEANUP: return "CLEANUP";
    case STATE_QUIESCED: return "QUIESCED";
    case STATE_QUIESCE_FAILED_SAFE: return "QUIESCE_FAILED_SAFE";
    }
    return "UNKNOWN";
}

static uint64_t ticks_to_us(uint64_t ticks)
{
    uint64_t whole = ticks / TICKS_PER_SECOND;
    uint64_t remainder = ticks % TICKS_PER_SECOND;
    if (whole > UINT64_MAX / 1000000u) return UINT64_MAX;
    return whole * 1000000u + (remainder * 1000000u) / TICKS_PER_SECOND;
}

static uint64_t enumeration_rate(uint64_t ticks)
{
    if (ticks == 0) return 0;
    return ((uint64_t)enum_returned_entries * TICKS_PER_SECOND) / ticks;
}

static void hex_encode_name(const char *name, char out[511])
{
    static const char digits[] = "0123456789abcdef";
    size_t length = strlen(name);
    for (size_t i = 0; i < length; ++i) {
        uint8_t byte = (uint8_t)name[i];
        out[i * 2] = digits[byte >> 4];
        out[i * 2 + 1] = digits[byte & 15];
    }
    out[length * 2] = '\0';
}

static void draw_dashboard(void)
{
    surface_t *surface = display_get();
    graphics_fill_screen(surface, graphics_make_color(0, 0, 24, 255));
    char line[80];
    int y = 8;
#define DRAW(...) do { snprintf(line, sizeof(line), __VA_ARGS__); \
    graphics_draw_text(surface, 8, y, line); y += 10; } while (0)
    DRAW("AURORA64 DISCOVERY 2.3");
    DRAW("STATE %-21s", state_name());
    DRAW("B/START stop; sd own/try=%u/%u", (unsigned)sdfs_mount_owned,
         (unsigned)sdfs_close_attempted);
    DRAW("handles/alloc=%u/%u", owned_handle_count(), owned_allocation_count());
    DRAW("complete/gate=%u/%u pop=%u", (unsigned)complete, (unsigned)complete,
         (unsigned)population_complete);
    DRAW("entries %lu/%u excl %lu", (unsigned long)metrics.searchable_entries,
         (unsigned)DISCOVERY_MAX_PATHS, (unsigned long)metrics.excluded_entries);
    DRAW("dirs %lu tops %u cand %lu", (unsigned long)metrics.directories,
         top_level_count, (unsigned long)metrics.extension_candidates);
    DRAW("valid %lu bytes %" PRIu64, (unsigned long)metrics.valid_magic,
         metrics.valid_total_bytes);

    if (state == STATE_SCAN) {
        DRAW("enum calls %lu returned %lu", (unsigned long)enum_call_count,
             (unsigned long)enum_returned_entries);
        DRAW("enum active us %" PRIu64, ticks_to_us(enum_active_ticks));
        DRAW("enum maxcall us %" PRIu64, ticks_to_us(enum_max_call_ticks));
    } else if (state == STATE_SELECTION) {
        DRAW("select %u/%u (one/frame)", selection_cursor, retained_count);
        DRAW("small %s", representative_small == UINT32_MAX ? "empty" : "selected");
        DRAW("medium %s", representative_medium == UINT32_MAX ? "empty" : "selected");
        DRAW("large %s", representative_large == UINT32_MAX ? "empty" : "selected");
    } else if (state == STATE_READ_4K || state == STATE_READ_64K) {
        read_benchmark_t *b = current_benchmark();
        DRAW("request %lu read calls %lu", (unsigned long)b->request_bytes,
             (unsigned long)b->read_calls);
        DRAW("bytes %" PRIu64 "/%" PRIu64, b->exact_bytes, b->expected_bytes);
        DRAW("active us %" PRIu64, ticks_to_us(b->active_read_ticks));
        DRAW("max read us %" PRIu64, ticks_to_us(b->max_read_ticks));
    } else if (state == STATE_HASH) {
        DRAW("SHA selftest=%u class %u/3 pass %u/2", (unsigned)sha_selftest_pass,
             hash_class_cursor < 3 ? hash_class_cursor + 1 : 3, hash_pass_cursor + 1);
        if (hash_class_cursor < 3 && hash_classes[hash_class_cursor].available) {
            hash_pass_t *p=&hash_classes[hash_class_cursor].pass[hash_pass_cursor];
            DRAW("hash %s bytes %" PRIu64, hash_class_name(hash_class_cursor), p->input_bytes);
            DRAW("calls %lu active us %" PRIu64, (unsigned long)p->read_calls,
                 ticks_to_us(p->active_stream_ticks));
            DRAW("max chunk us %" PRIu64, ticks_to_us(p->max_chunk_ticks));
        }
    } else if (state == STATE_SYNTHETIC) {
        DRAW("synth sub=%u fill=%lu/%u", (unsigned)synth_work,
             (unsigned long)synth_fill_cursor, (unsigned)SYNTH_RECORD_CAP);
        DRAW("pass=%lu records=%lu/%u", (unsigned long)synth_pass_cursor + 1,
             (unsigned long)synth_encode_cursor, (unsigned)SYNTH_RECORD_CAP);
        DRAW("heap b/min/a %d/%d/%d", synth_heap_before, synth_heap_min,
             synth_heap_after_alloc);
        DRAW("crc/match/success %u/%u/%u", (unsigned)synth_crc_selftest_pass,
             (unsigned)synth_encode_match, (unsigned)synthetic_success);
    }

    DRAW("heap post/min/final %d/%d/%d", heap_post_init, heap_min, heap_final);
    DRAW("gap us input=%" PRIu64 " frame=%" PRIu64, ticks_to_us(max_input_gap),
         ticks_to_us(max_frame_gap));
    DRAW("work max/cad us %" PRIu64 "/%" PRIu64, ticks_to_us(max_work_duration),
         ticks_to_us(max_work_cadence));
    if (state == STATE_ERROR || state == STATE_QUIESCE_FAILED_SAFE)
        DRAW("ERR %.34s", error_text);
#undef DRAW
    display_show(surface);
}


static void freeze_terminal_snapshot(void)
{
    terminal_snapshot.max_frame_gap = max_frame_gap;
    terminal_snapshot.max_input_gap = max_input_gap;
    terminal_snapshot.max_usb_opportunity_gap = max_usb_opportunity_gap;
    terminal_snapshot.max_work_cadence = max_work_cadence;
    terminal_snapshot.max_work_duration = max_work_duration;
    terminal_snapshot.request_to_safe_ticks = request_to_safe_ticks;
    terminal_snapshot.max_close_ticks = max_close_ticks;
    terminal_snapshot.heap_post_init = heap_post_init;
    terminal_snapshot.heap_min = heap_min;
    terminal_snapshot.heap_final = heap_final;
    terminal_snapshot_frozen = true;
}

static void reset_report_cursors(void)
{
    report_read_cursor = 0;
    report_sha_class_cursor = 0;
    report_sha_pass_cursor = 0;
    report_synth_pass_cursor = 0;
    report_top_cursor = 0;
    report_effective_cursor = 0;
    report_effective_emitted = 0;
}

static void emit_terminal_replay_record(void)
{
    bool final_state = state == STATE_COMPLETE || state == STATE_ERROR ||
        state == STATE_QUIESCED || state == STATE_QUIESCE_FAILED_SAFE;
    if (!final_state) return;
    if (!terminal_snapshot_frozen) {
        /* Let the next frame/input samples include terminal-transition work. */
        if (!terminal_snapshot_pending) {
            terminal_snapshot_pending = true;
            return;
        }
        freeze_terminal_snapshot();
    }

    /* Stages without a record advance here; every return below emitted exactly one. */
    for (;;) {
        switch (report_stage) {
        case REPORT_REPLAY_BEGIN:
            ++report_cycle;
            reset_report_cursors();
            report_stage = REPORT_INIT;
            debugf("P3DISC replay_begin cycle=%lu schema=2 bounded_records_max=99\n",
                   (unsigned long)report_cycle);
            return;
        case REPORT_INIT:
            report_stage = REPORT_WARNING;
            debugf("P3DISC event=INIT cycle=%lu sd=%u usb_log=%u emu_log=%u increment=2.3 roots=%u caps=4/16/2048/4096/196608 pathcap=%u readcap=%u\n",
                   (unsigned long)report_cycle, (unsigned)init_sd_ready,
                   (unsigned)usb_log_available, (unsigned)emu_log_available,
                   scan_root_count, (unsigned)DISCOVERY_FULL_PATH_CAP,
                   (unsigned)DISCOVERY_READ_BUFFER_CAP);
            return;
        case REPORT_WARNING:
            report_stage = REPORT_STOP_REQUEST;
            if (!usb_log_available) {
                debugf("P3DISC warning=USB_LOG_UNAVAILABLE cycle=%lu\n",
                       (unsigned long)report_cycle);
                return;
            }
            break;
        case REPORT_STOP_REQUEST:
            report_stage = REPORT_SHA_SELFTEST;
            if (stop_requested) {
                debugf("P3DISC event=STOP_REQUEST cycle=%lu\n",
                       (unsigned long)report_cycle);
                return;
            }
            break;
        case REPORT_SHA_SELFTEST:
            report_stage = REPORT_ENUMERATION;
            debugf("P3DISC sha_selftest pass=%u vectors=2 digests_emitted=0\n",
                   (unsigned)sha_selftest_pass);
            return;
        case REPORT_ENUMERATION:
            report_stage = REPORT_REPRESENTATIVES;
            debugf("P3DISC enumeration population_complete=%u scan_wall_ticks=%" PRIu64 " active_dir_ticks=%" PRIu64 " dir_calls=%lu returned_entries=%lu max_dir_call_ticks=%" PRIu64 " active_entries_per_second=%" PRIu64 " cooperative_entries_per_second=%" PRIu64 "\n",
                   (unsigned)population_complete, scan_wall_ticks, enum_active_ticks,
                   (unsigned long)enum_call_count, (unsigned long)enum_returned_entries,
                   enum_max_call_ticks, enumeration_rate(enum_active_ticks),
                   enumeration_rate(scan_wall_ticks));
            return;
        case REPORT_REPRESENTATIVES:
            report_stage = REPORT_READS;
            debugf("P3DISC representatives examined=%u retained=%u selection_complete=%u small_state=%s small_index=%lu small_size=%" PRIu64 " medium_state=%s medium_index=%lu medium_size=%" PRIu64 " large_state=%s large_index=%lu large_size=%" PRIu64 " sequential_state=%s sequential_index=%lu sequential_size=%" PRIu64 "\n",
                   selection_cursor, retained_count,
                   (unsigned)(selection_cursor == retained_count),
                   representative_small == UINT32_MAX ? "empty" : "selected",
                   (unsigned long)(representative_small == UINT32_MAX ? 0 : representative_small),
                   representative_small == UINT32_MAX ? 0 : retained[representative_small].size,
                   representative_medium == UINT32_MAX ? "empty" : "selected",
                   (unsigned long)(representative_medium == UINT32_MAX ? 0 : representative_medium),
                   representative_medium == UINT32_MAX ? 0 : retained[representative_medium].size,
                   representative_large == UINT32_MAX ? "empty" : "selected",
                   (unsigned long)(representative_large == UINT32_MAX ? 0 : representative_large),
                   representative_large == UINT32_MAX ? 0 : retained[representative_large].size,
                   sequential_target == UINT32_MAX ? "empty" : "selected",
                   (unsigned long)(sequential_target == UINT32_MAX ? 0 : sequential_target),
                   sequential_target == UINT32_MAX ? 0 : retained[sequential_target].size);
            return;
        case REPORT_READS: {
            read_benchmark_t *b = report_read_cursor == 0 ? &benchmark_4k : &benchmark_64k;
            ++report_read_cursor;
            if (report_read_cursor == 2) report_stage = REPORT_SHA_PASSES;
            debugf("P3DISC read request=%lu expected_bytes=%" PRIu64 " exact_bytes=%" PRIu64 " read_calls=%lu active_read_ticks=%" PRIu64 " wall_ticks=%" PRIu64 " open_ticks=%" PRIu64 " close_ticks=%" PRIu64 " max_read_ticks=%" PRIu64 " coverage_complete=%u success=%u io_error=%u skipped=%u\n",
                   (unsigned long)b->request_bytes, b->expected_bytes, b->exact_bytes,
                   (unsigned long)b->read_calls, b->active_read_ticks, b->wall_ticks,
                   b->open_ticks, b->close_ticks, b->max_read_ticks,
                   (unsigned)b->coverage_complete, (unsigned)b->success,
                   (unsigned)b->io_error, (unsigned)!b->started);
            return;
        }
        case REPORT_SHA_PASSES: {
            hash_class_t *hc = &hash_classes[report_sha_class_cursor];
            hash_pass_t *hp = &hc->pass[report_sha_pass_cursor];
            unsigned class_index = report_sha_class_cursor;
            unsigned pass_index = report_sha_pass_cursor;
            if (++report_sha_pass_cursor == 2) {
                report_sha_pass_cursor = 0;
                if (++report_sha_class_cursor == DISCOVERY_HASH_CLASSES) {
                    report_sha_class_cursor = 0;
                    report_stage = REPORT_SHA_CLASSES;
                }
            }
            debugf("P3DISC sha class=%s pass=%u available=%u expected_bytes=%" PRIu64 " input_bytes=%" PRIu64 " active_stream_ticks=%" PRIu64 " wall_ticks=%" PRIu64 " open_ticks=%" PRIu64 " finalize_ticks=%" PRIu64 " close_ticks=%" PRIu64 " max_chunk_ticks=%" PRIu64 " read_calls=%lu io_error=%u tail_error=%u size_error=%u success=%u\n",
                   hash_class_name(class_index), pass_index + 1,
                   (unsigned)hc->available,
                   hp->expected_bytes, hp->input_bytes, hp->active_stream_ticks,
                   hp->wall_ticks, hp->open_ticks, hp->finalize_ticks, hp->close_ticks,
                   hp->max_chunk_ticks, (unsigned long)hp->read_calls,
                   (unsigned)hp->io_error, (unsigned)hp->tail_error,
                   (unsigned)hp->size_error, (unsigned)hp->success);
            return;
        }
        case REPORT_SHA_CLASSES: {
            unsigned i = report_sha_class_cursor++;
            hash_class_t *hc = &hash_classes[i];
            if (report_sha_class_cursor == DISCOVERY_HASH_CLASSES) {
                report_sha_class_cursor = 0;
                report_stage = REPORT_SYNTHETIC_PASSES;
            }
            debugf("P3DISC sha_class class=%s available=%u passes_match=%u two_pass_wall_ticks=%" PRIu64 "\n",
                   hash_class_name(i), (unsigned)hc->available,
                   (unsigned)hc->passes_match,
                   hc->two_pass_wall_ticks);
            return;
        }
        case REPORT_SYNTHETIC_PASSES: {
            unsigned i = report_synth_pass_cursor++;
            if (report_synth_pass_cursor == SYNTH_ENCODE_PASSES)
                report_stage = REPORT_TERMINAL;
            debugf("P3DISC synthetic_encode pass=%u bytes=%" PRIu64 " records=%u operations=%lu active_encode_ticks=%" PRIu64 " wall_ticks=%" PRIu64 " skipped=%u\n",
                   i + 1, synth_pass_bytes[i], (unsigned)SYNTH_RECORD_CAP,
                   (unsigned long)synth_pass_operations[i], synth_pass_active_ticks[i],
                   synth_pass_wall_ticks[i],
                   (unsigned)(synth_pass_operations[i] == 0));
            return;
        }
        case REPORT_TERMINAL:
            report_stage = REPORT_SYNTHETIC_SCHEMA;
            debugf("P3DISC terminal state=%s complete=%u production_gate_ready=%u population_complete=%u handles=%u allocations=%u sdfs_mount_owned=%u sdfs_close_attempted=%u sdfs_close_verified=unavailable stop_requested=%u stop_to_safe_ticks=%" PRIu64 " cleanup_close_failed=%u error=%.48s\n",
                   state_name(), (unsigned)complete, (unsigned)complete,
                   (unsigned)population_complete, owned_handle_count(),
                   owned_allocation_count(), (unsigned)sdfs_mount_owned,
                   (unsigned)sdfs_close_attempted, (unsigned)stop_requested,
                   terminal_snapshot.request_to_safe_ticks,
                   (unsigned)cleanup_close_failed, error_text);
            return;
        case REPORT_SYNTHETIC_SCHEMA:
            report_stage = REPORT_SYNTHETIC_MEMORY;
            debugf("P3DISC synthetic_schema version=1 fixed_endian=big record_struct_bytes=%lu record_allocated_bytes=%lu metadata_encoded_bytes=%u path_length=%u path_storage_per_record=%u record_cap=%u path_arena_cap=%u sink_cap=%u passes=%u expected_bytes=%" PRIu64 " filesystem_access=0\n",
                   (unsigned long)sizeof(synthetic_record_t),
                   (unsigned long)(sizeof(synthetic_record_t) * SYNTH_RECORD_CAP),
                   (unsigned)SYNTH_METADATA_ENCODED, (unsigned)SYNTH_PATH_LENGTH,
                   (unsigned)SYNTH_PATH_BYTES, (unsigned)SYNTH_RECORD_CAP,
                   (unsigned)SYNTH_PATH_ARENA_BYTES, (unsigned)SYNTH_SINK_BYTES,
                   (unsigned)SYNTH_ENCODE_PASSES, synth_expected_bytes);
            return;
        case REPORT_SYNTHETIC_MEMORY:
            report_stage = REPORT_SYNTHETIC_RESULT;
            debugf("P3DISC synthetic_memory heap_before=%d heap_min=%d heap_after_alloc=%d heap_after_free=%d measured_peak_delta=%d allocations_ok=%u\n",
                   synth_heap_before, synth_heap_min, synth_heap_after_alloc,
                   synth_heap_after_free, synth_peak_delta,
                   (unsigned)synth_allocations_ok);
            return;
        case REPORT_SYNTHETIC_RESULT:
            report_stage = REPORT_DUPLICATE;
            debugf("P3DISC synthetic_result initialized=%lu crc_selftest=%u encode_match=%u overflow=%u success=%u bytes_pass1=%" PRIu64 " bytes_pass2=%" PRIu64 "\n",
                   (unsigned long)synth_fill_cursor, (unsigned)synth_crc_selftest_pass,
                   (unsigned)synth_encode_match, (unsigned)synth_overflow,
                   (unsigned)synthetic_success,
                   synth_pass_bytes[0], synth_pass_bytes[1]);
            return;
        case REPORT_DUPLICATE:
            report_stage = REPORT_LIFECYCLE;
            debugf("P3DISC duplicate_evidence status=representative_only population_complete=%u inferred_equal_pairs=%lu inferred_equal_groups=%lu hash_any_available=%u hash_all_available_succeeded=%u hash_full_class_coverage=%u digests_emitted=0\n",
                   (unsigned)population_complete, (unsigned long)inferred_equal_pairs,
                   (unsigned long)inferred_equal_groups, (unsigned)hash_any_available,
                   (unsigned)hash_coverage_complete,
                   (unsigned)hash_full_class_coverage);
            return;
        case REPORT_LIFECYCLE:
            report_stage = REPORT_SUMMARY;
            debugf("P3DISC lifecycle state=%s handles=%u allocations=%u sdfs_mount_owned=%u sdfs_close_attempted=%u sdfs_close_verified=unavailable complete=%u production_gate_ready=%u\n",
                   state_name(), owned_handle_count(), owned_allocation_count(),
                   (unsigned)sdfs_mount_owned, (unsigned)sdfs_close_attempted,
                   (unsigned)complete, (unsigned)complete);
            return;
        case REPORT_SUMMARY:
            report_stage = REPORT_EXTENSIONS;
            debugf("P3DISC summary state=%s complete=%u population_complete=%u handles=%u roots=%u top_dirs=%u dirs=%lu searchable_entries=%lu excluded_entries=%lu logical_path_bytes=%" PRIu64 " path_storage_bytes=%" PRIu64 " max_depth=%lu max_logical_path_bytes=%lu candidates=%lu valid=%lu root_candidates=%lu root_valid=%lu mismatch=%lu short=%lu invalid=%lu open_errors=%lu read_errors=%lu rom_bytes=%" PRIu64 " valid_size_state=%s valid_min=%" PRIu64 " valid_max=%" PRIu64 " retained_path_arena_bytes=%u heap=%d/%d/%d usb_log=%u emu_log=%u sdfs_mount_owned=%u sdfs_close_attempted=%u sdfs_close_verified=unavailable stop_requested=%u input_gap_us=%" PRIu64 " frame_gap_us=%" PRIu64 " usb_gap_us=%" PRIu64 " work_cadence_us=%" PRIu64 " max_work_duration_us=%" PRIu64 " stop_to_safe_us=%" PRIu64 " close_us=%" PRIu64 "\n",
                   state_name(), (unsigned)complete, (unsigned)population_complete,
                   owned_handle_count(),
                   scan_root_count, top_level_count, (unsigned long)metrics.directories,
                   (unsigned long)metrics.searchable_entries, (unsigned long)metrics.excluded_entries,
                   metrics.logical_path_bytes, metrics.path_storage_bytes,
                   (unsigned long)metrics.max_depth, (unsigned long)metrics.max_logical_path_bytes,
                   (unsigned long)metrics.extension_candidates, (unsigned long)metrics.valid_magic,
                   (unsigned long)metrics.root_direct_candidates, (unsigned long)metrics.root_direct_valid,
                   (unsigned long)metrics.order_mismatch, (unsigned long)metrics.short_header,
                   (unsigned long)metrics.invalid_header, (unsigned long)metrics.open_errors,
                   (unsigned long)metrics.read_errors, metrics.valid_total_bytes,
                   metrics.valid_magic ? "present" : "none",
                   metrics.valid_magic ? metrics.valid_min_bytes : 0,
                   metrics.valid_magic ? metrics.valid_max_bytes : 0, retained_path_bytes,
                   terminal_snapshot.heap_post_init, terminal_snapshot.heap_min,
                   terminal_snapshot.heap_final, (unsigned)usb_log_available,
                   (unsigned)emu_log_available, (unsigned)sdfs_mount_owned,
                   (unsigned)sdfs_close_attempted, (unsigned)stop_requested,
                   ticks_to_us(terminal_snapshot.max_input_gap),
                   ticks_to_us(terminal_snapshot.max_frame_gap),
                   ticks_to_us(terminal_snapshot.max_usb_opportunity_gap),
                   ticks_to_us(terminal_snapshot.max_work_cadence),
                   ticks_to_us(terminal_snapshot.max_work_duration),
                   stop_requested ? ticks_to_us(terminal_snapshot.request_to_safe_ticks) : 0,
                   ticks_to_us(terminal_snapshot.max_close_ticks));
            return;
        case REPORT_EXTENSIONS:
            report_stage = REPORT_MAGIC;
            debugf("P3DISC extensions candidates=%lu z64=%lu v64=%lu n64=%lu rom=%lu\n",
                   (unsigned long)metrics.extension_candidates,
                   (unsigned long)metrics.extension_z64, (unsigned long)metrics.extension_v64,
                   (unsigned long)metrics.extension_n64, (unsigned long)metrics.extension_rom);
            return;
        case REPORT_MAGIC:
            report_stage = REPORT_SIZE_BUCKETS;
            debugf("P3DISC magic valid=%lu z64=%lu v64=%lu n64=%lu order_match=%lu order_mismatch=%lu\n",
                   (unsigned long)metrics.valid_magic, (unsigned long)metrics.magic_z64,
                   (unsigned long)metrics.magic_v64, (unsigned long)metrics.magic_n64,
                   (unsigned long)metrics.order_match, (unsigned long)metrics.order_mismatch);
            return;
        case REPORT_SIZE_BUCKETS:
            report_stage = REPORT_TOP_LEVELS;
            debugf("P3DISC size_buckets lt16m=%lu from16_to_lt32m=%lu from32_to_lt64m=%lu ge64m=%lu\n",
                   (unsigned long)metrics.size_bucket_lt16m,
                   (unsigned long)metrics.size_bucket_16_32m,
                   (unsigned long)metrics.size_bucket_32_64m,
                   (unsigned long)metrics.size_bucket_ge64m);
            return;
        case REPORT_TOP_LEVELS:
            if (report_top_cursor < top_level_count) {
                unsigned i = report_top_cursor++;
                hex_encode_name(top_levels[i].name, report_name_hex);
                debugf("P3DISC top index=%u name_encoding=hex name_hex=%s dirs=%lu entries=%lu candidates=%lu valid=%lu payload_bytes=%" PRIu64 "\n",
                       i, report_name_hex, (unsigned long)top_levels[i].directories,
                       (unsigned long)top_levels[i].paths,
                       (unsigned long)top_levels[i].candidates,
                       (unsigned long)top_levels[i].valid, top_levels[i].payload_bytes);
                return;
            }
            report_stage = REPORT_EFFECTIVE_ROOTS;
            break;
        case REPORT_EFFECTIVE_ROOTS: {
            unsigned valid_top_count = 0;
            for (unsigned i = 0; i < top_level_count; ++i)
                if (top_levels[i].valid) ++valid_top_count;
            if (metrics.root_direct_valid || valid_top_count > DISCOVERY_MAX_ROOTS) {
                if (report_effective_emitted == 0) {
                    report_effective_emitted = 1;
                    debugf("P3DISC effective_root index=0 encoding=literal value=/\n");
                    return;
                }
            } else {
                while (report_effective_cursor < top_level_count &&
                       !top_levels[report_effective_cursor].valid)
                    ++report_effective_cursor;
                if (report_effective_cursor < top_level_count &&
                    report_effective_emitted < DISCOVERY_MAX_ROOTS) {
                    unsigned i = report_effective_cursor++;
                    hex_encode_name(top_levels[i].name, report_name_hex);
                    debugf("P3DISC effective_root index=%u encoding=hex value_hex=%s valid=%lu\n",
                           report_effective_emitted++, report_name_hex,
                           (unsigned long)top_levels[i].valid);
                    return;
                }
                if (report_effective_emitted == 0) {
                    report_effective_emitted = 1;
                    debugf("P3DISC effective_root state=none valid=0\n");
                    return;
                }
            }
            report_stage = REPORT_REPLAY_END;
            break;
        }
        case REPORT_REPLAY_END:
            debugf("P3DISC replay_end cycle=%lu\n", (unsigned long)report_cycle);
            report_stage = REPORT_REPLAY_BEGIN;
            return;
        }
    }
}

int main(void)
{
    usb_log_available = debug_init_usblog();
    emu_log_available = debug_init_emulog();
    display_init(RESOLUTION_320x240, DEPTH_16_BPP, 2, GAMMA_NONE, FILTERS_DISABLED);
    timer_init();
    joypad_init();

    bool sd_ready = debug_init_sdfs("sd:/", -1);
    init_sd_ready = sd_ready;
    sdfs_mount_owned = sd_ready;
    heap_stats_t heap;
    sys_get_heap_stats(&heap);
    heap_post_init = heap.total - heap.used;
    heap_min = heap_post_init;
    heap_final = -1;

    uint64_t now = get_ticks();
    last_frame_tick = now;
    last_input_tick = now;
    last_usb_opportunity_tick = now;
    last_work_tick = now;
    stop_request_tick = now;
    scan_wall_start_tick = now;

    memset(&dir_stack[0], 0, sizeof(dir_stack[0]));
    snprintf(dir_stack[0].path, sizeof(dir_stack[0].path), "%s", scan_roots[0]);
    dir_stack[0].top_index = -1;
    stack_count = 1;

    sha_selftest_pass = sha256_selftest();
    if (!sha_selftest_pass) set_failure("SHA-256 self-test failed");
    else if (!sd_ready) set_failure("SD mount failed");

    while (1) {
        now = get_ticks();
        if (!terminal_snapshot_frozen)
            update_max(&max_frame_gap, tick_delta(now, last_frame_tick));
        last_frame_tick = now;

        joypad_poll();
        now = get_ticks();
        if (!terminal_snapshot_frozen)
            update_max(&max_input_gap, tick_delta(now, last_input_tick));
        last_input_tick = now;
        joypad_buttons_t pressed = joypad_get_buttons_pressed(JOYPAD_PORT_1);

        /* Explicit service-opportunity marker; this ROM never calls usb_comm_poll(). */
        if (!terminal_snapshot_frozen)
            update_max(&max_usb_opportunity_gap, tick_delta(now, last_usb_opportunity_tick));
        last_usb_opportunity_tick = now;

        if (pressed.b || pressed.start)
            request_stop(); /* checked before filesystem/selection work */

        if (state == STATE_CLEANUP) cleanup_one_handle();
        else if (state == STATE_SCAN) scan_one_operation();
        else if (state == STATE_SELECTION) select_one_record();
        else if (state == STATE_READ_4K || state == STATE_READ_64K)
            benchmark_one_operation();
        else if (state == STATE_HASH) hash_one_operation();
        else if (state == STATE_SYNTHETIC) synthetic_one_operation();
        else if (state == STATE_CLOSE_SDFS) close_sdfs_and_complete();

        if (!terminal_snapshot_frozen) {
            sys_get_heap_stats(&heap);
            int free_heap = heap.total - heap.used;
            if (free_heap < heap_min) heap_min = free_heap;
        }

        draw_dashboard();
        emit_terminal_replay_record();
    }
}
