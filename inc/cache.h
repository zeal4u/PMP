#ifndef CACHE_H
#define CACHE_H

#include <functional>
#include <unordered_set>
#include <sstream>

#include "memory_class.h"

// PAGE
extern uint32_t PAGE_TABLE_LATENCY, SWAP_LATENCY;

/**
 * @brief support va for l1d prefetcher
 * 
 */
extern bool SUPPORT_VA;

#ifdef RECORD_INFO
struct Info {
    uint64_t pattern;
    uint64_t block_number;
    uint64_t score;
    uint64_t pc;
    uint64_t trigger_offset;
};
// extern map<uint64_t, Info> pf_addr_scores;
#endif


struct File 
{
    fstream fout;
    string filename;
};
extern struct File g_fout;


// CACHE TYPE
#define IS_ITLB 0
#define IS_DTLB 1
#define IS_STLB 2
#define IS_L1I 3
#define IS_L1D 4
#define IS_L2C 5
#define IS_LLC 6

// INSTRUCTION TLB
#define ITLB_SET 16
#define ITLB_WAY 4
#define ITLB_RQ_SIZE 16
#define ITLB_WQ_SIZE 16
#define ITLB_PQ_SIZE 0
#define ITLB_MSHR_SIZE 8
#define ITLB_LATENCY 1

// DATA TLB
#define DTLB_SET 16
#define DTLB_WAY 4
#define DTLB_RQ_SIZE 16
#define DTLB_WQ_SIZE 16
#define DTLB_PQ_SIZE 0
#define DTLB_MSHR_SIZE 8
#define DTLB_LATENCY 1

// SECOND LEVEL TLB
#define STLB_SET 128
#define STLB_WAY 12
#define STLB_RQ_SIZE 32
#define STLB_WQ_SIZE 32
#define STLB_PQ_SIZE 0
#define STLB_MSHR_SIZE 16
#define STLB_LATENCY 8

// L1 INSTRUCTION CACHE
#define L1I_SET 64
#define L1I_WAY 8
#define L1I_RQ_SIZE 64
#define L1I_WQ_SIZE 64
#define L1I_PQ_SIZE 32
#define L1I_MSHR_SIZE 8
#define L1I_LATENCY 4

// L1 DATA CACHE
#define L1D_SET 64
#define L1D_WAY 12
#define L1D_RQ_SIZE 64
#define L1D_WQ_SIZE 64
#define L1D_PQ_SIZE 8
#define L1D_MSHR_SIZE 16
#define L1D_LATENCY 5

// L2 CACHE
#define L2C_SET 1024
#define L2C_WAY 8
#define L2C_RQ_SIZE 32
#define L2C_WQ_SIZE 32
#define L2C_PQ_SIZE 16
#define L2C_MSHR_SIZE 32
#define L2C_LATENCY 10 // 4/5 (L1I or L1D) + 10 = 14/15 cycles

// LAST LEVEL CACHE
#define LLC_SET NUM_CPUS * 2048
#define LLC_WAY 16
#define LLC_RQ_SIZE NUM_CPUS *L2C_MSHR_SIZE //48
#define LLC_WQ_SIZE NUM_CPUS *L2C_MSHR_SIZE //48
#define LLC_PQ_SIZE NUM_CPUS * 32
#define LLC_MSHR_SIZE NUM_CPUS * 64
#define LLC_LATENCY 20 // 4/5 (L1I or L1D) + 10 + 20 = 34/35 cycles

#define CACHE_ACC_LEVELS 10

#define PREF_STAT_PERIOD (2048)

class PerformanceCounter {
  public:
    PerformanceCounter() : offset_counters(64) {}

    void record_match(bool is_long) {
      this->total_matches++;
      this->long_matches += is_long;
    }

    void record_prefetch(int requests) {
      if (requests)
        this->total_prefetch++;
      this->total_requests += requests;
    }

    void record_offset(uint64_t offset) {
      offset_counters[offset]++;
    }

    void record_bit_vector(const vector<bool>& bit_vector) {
      int bits = accumulate(bit_vector.begin(), bit_vector.end(), 0);
      this->offset_bits += bits;
      this->total_bit_vector++;
    }

    PerformanceCounter operator+(const PerformanceCounter& other) {
      PerformanceCounter pc;
      pc.long_matches = this->long_matches + other.long_matches;
      pc.total_matches = this->total_matches + other.total_matches;
      pc.total_bit_vector = this->total_bit_vector + other.total_bit_vector;
      pc.offset_bits = this->offset_bits+ other.offset_bits;
      pc.total_prefetch = this->total_prefetch + other.total_prefetch;
      pc.total_requests = this->total_requests + other.total_requests;
      for (size_t i = 0; i < pc.offset_counters.size(); i++) {
        pc.offset_counters[i] = this->offset_counters[i] + other.offset_counters[i];
      }
      return pc;
    }

    void reset() {
      for (auto& count : this->offset_counters) {
        count = 0;
      }
      total_matches = 0, long_matches = 0;
      total_bit_vector = 0, offset_bits = 0;
      total_prefetch = 0, total_requests = 0;
    }

    void output() {
      cout << "offsets per bit vector:" << double(this->offset_bits) / this->total_bit_vector << endl;
      cout << "requests per prefetch:" << double(this->total_requests) / this->total_prefetch << endl;
      cout << "long match rate:" << double(this->long_matches) / this->total_matches << endl;
      cout << "prefetch offset count:";
      for (auto& count : this->offset_counters) {
        cout << count << " ";
      }
      cout << endl;
    }
    // int offsets_per_bit_vector;
    // int requests_per_prefetch;
    // int long_match_rate;
    // int short_match_rate;

    vector<uint64_t> offset_counters;
    int total_matches, long_matches;
    int total_bit_vector, offset_bits;
    int total_prefetch, total_requests;
};

class CompulsoryMissRecorder {
  public:
    void record(uint64_t addr, int cache_type, bool hit) {
        // It'a new address
        if (cache_type == LOAD && addresses_.find(addr) == addresses_.end()) {
            if (hit)
                stat_.compulsory_hit_count++;
            else
                stat_.compulsory_miss_count++;
            addresses_.insert(addr);
        }
    }

    string to_string() {
        stringstream ss;
        ss << "Compulory Miss:" << stat_.compulsory_miss_count << ", Compulsory Hit:" << stat_.compulsory_hit_count;
        return ss.str();
    }

  private:
    struct {
        uint64_t compulsory_hit_count;
        uint64_t compulsory_miss_count;
    } stat_;
    unordered_set<uint64_t> addresses_;
};


#ifdef MEASURE
extern PerformanceCounter batch_perf_counter[NUM_CPUS];
#endif

class CACHE : public MEMORY
{
public:
    uint32_t cpu;
    const string NAME;
    const uint32_t NUM_SET, NUM_WAY, NUM_LINE, WQ_SIZE, RQ_SIZE, PQ_SIZE, MSHR_SIZE;
    uint32_t LATENCY;
    BLOCK **block;
    int fill_level;
    uint32_t MAX_READ, MAX_FILL;
    uint32_t reads_available_this_cycle;
    uint8_t cache_type;

    // prefetch stats
    uint64_t pf_requested,
        pf_issued,
        pf_useful,
        pf_late,
        pf_useless,
        pf_fill,
        last_period_useless;

    uint8_t cur_bw_level;
    uint8_t cur_ipc;
    uint32_t acc_level;
    uint32_t overprediction_level;
    uint32_t pref_overp;

    /**
     * @brief Detailed Prefetch Performance Analyze Metrics
     * 
     */

    uint64_t pref_useful[NUM_CPUS][64], pref_filled[NUM_CPUS][64], pref_late[NUM_CPUS][64];

    // queues
    PACKET_QUEUE WQ{NAME + "_WQ", WQ_SIZE},       // write queue
        RQ{NAME + "_RQ", RQ_SIZE},                // read queue
        PQ{NAME + "_PQ", PQ_SIZE},                // prefetch queue
        MSHR{NAME + "_MSHR", MSHR_SIZE},          // MSHR
        PROCESSED{NAME + "_PROCESSED", ROB_SIZE}; // processed queue

    uint64_t sim_access[NUM_CPUS][NUM_TYPES],
        sim_hit[NUM_CPUS][NUM_TYPES],
        sim_miss[NUM_CPUS][NUM_TYPES],
        roi_access[NUM_CPUS][NUM_TYPES],
        roi_hit[NUM_CPUS][NUM_TYPES],
        roi_miss[NUM_CPUS][NUM_TYPES],
        last_total_load_miss;

    uint64_t total_miss_latency;
    uint64_t miss_latency[NUM_CPUS][NUM_TYPES];

    /* For cache accuracy measurement */
    uint64_t cycle, next_measure_cycle;
    uint64_t pf_useful_epoch, pf_filled_epoch;
    uint32_t pref_acc;
    uint64_t total_acc_epochs, acc_epoch_hist[CACHE_ACC_LEVELS];

#ifdef MEASURE_COMPULSORY
    CompulsoryMissRecorder cmr_;
#endif
    /**
     * @brief dynamic functions needed by some prefetchers;
     * 
     */
    function<uint32_t(uint32_t, uint64_t, uint64_t, uint32_t)> l1d_prefetcher_prefetch_hit;
    function<uint32_t(uint32_t, uint64_t, uint64_t, uint32_t)> l2c_prefetcher_prefetch_hit;
    function<uint32_t(uint32_t, uint64_t, uint64_t, uint32_t)> llc_prefetcher_prefetch_hit;

    // constructor
    CACHE(string v1, uint32_t v2, int v3, uint32_t v4, uint32_t v5, uint32_t v6, uint32_t v7, uint32_t v8)
        : NAME(v1), NUM_SET(v2), NUM_WAY(v3), NUM_LINE(v4), WQ_SIZE(v5), RQ_SIZE(v6), PQ_SIZE(v7), MSHR_SIZE(v8)
    {

        LATENCY = 0;

        // cache block
        block = new BLOCK *[NUM_SET];
        for (uint32_t i = 0; i < NUM_SET; i++)
        {
            block[i] = new BLOCK[NUM_WAY];

            for (uint32_t j = 0; j < NUM_WAY; j++)
            {
                block[i][j].lru = j;
            }
        }

        for (uint32_t i = 0; i < NUM_CPUS; i++)
        {
            upper_level_icache[i] = NULL;
            upper_level_dcache[i] = NULL;

            for (uint32_t j = 0; j < NUM_TYPES; j++)
            {
                sim_access[i][j] = 0;
                sim_hit[i][j] = 0;
                sim_miss[i][j] = 0;
                roi_access[i][j] = 0;
                roi_hit[i][j] = 0;
                roi_miss[i][j] = 0;
                miss_latency[i][j] = 0;
            }

            for (int j = 0; j < 6; j++)
            {
                pref_useful[i][j] = 0;
                pref_filled[i][j] = 0;
                pref_late[i][j] = 0;
            }
        }

        total_miss_latency = 0;

        lower_level = NULL;
        extra_interface = NULL;
        fill_level = -1;
        MAX_READ = 1;
        MAX_FILL = 1;

        pf_requested = 0;
        pf_issued = 0;
        pf_useful = 0;
        pf_useless = 0;
        pf_fill = 0;
        last_period_useless = 0;
    };

    // destructor
    virtual ~CACHE()
    {
        for (uint32_t i = 0; i < NUM_SET; i++)
            delete[] block[i];
        delete[] block;
    };

    // functions
    int add_rq(PACKET *packet),
        add_wq(PACKET *packet),
        add_pq(PACKET *packet);

    void return_data(PACKET *packet),
        operate(),
        increment_WQ_FULL(uint64_t address);

    uint32_t get_occupancy(uint8_t queue_type, uint64_t address),
        get_size(uint8_t queue_type, uint64_t address);

    int check_hit(PACKET *packet),
        invalidate_entry(uint64_t inval_addr),
        check_mshr(PACKET *packet),
        prefetch_line(uint64_t ip, uint64_t base_addr, uint64_t pf_addr, int prefetch_fill_level, uint32_t prefetch_metadata),
        kpc_prefetch_line(uint64_t base_addr, uint64_t pf_addr, int prefetch_fill_level, int delta, int depth, int signature, int confidence, uint32_t prefetch_metadata);

    virtual void handle_fill(),
        handle_writeback(),
        handle_read(),
        handle_prefetch();

    void add_mshr(PACKET *packet),
        update_fill_cycle(),
        llc_initialize_replacement(),
        update_replacement_state(uint32_t cpu, uint32_t set, uint32_t way, uint64_t full_addr, uint64_t ip, uint64_t victim_addr, uint32_t type, uint8_t hit),
        llc_update_replacement_state(uint32_t cpu, uint32_t set, uint32_t way, uint64_t full_addr, uint64_t ip, uint64_t victim_addr, uint32_t type, uint8_t hit),
        lru_update(uint32_t set, uint32_t way),
        lru_update_prefetch(uint32_t set, uint32_t way),
        fill_cache(uint32_t set, uint32_t way, PACKET *packet),
        replacement_final_stats(),
        llc_replacement_final_stats(),
        //prefetcher_initialize(),
        l1d_prefetcher_initialize(),
        l2c_prefetcher_initialize(),
        llc_prefetcher_initialize(),
        prefetcher_operate(uint64_t addr, uint64_t ip, uint8_t cache_hit, uint8_t type),
        l1d_prefetcher_operate(uint64_t addr, uint64_t ip, uint8_t cache_hit, uint8_t type),
        prefetcher_cache_fill(uint64_t addr, uint32_t set, uint32_t way, uint8_t prefetch, uint64_t evicted_addr),
        l1d_prefetcher_cache_fill(uint64_t addr, uint32_t set, uint32_t way, uint8_t prefetch, uint64_t evicted_addr, uint32_t metadata_in),
        //prefetcher_final_stats(),
        l1d_prefetcher_final_stats(),
        l2c_prefetcher_final_stats(),
        llc_prefetcher_final_stats();
    void l1d_prefetcher_operate(uint64_t v_addr, uint64_t p_addr, uint64_t ip, uint8_t cache_hit, uint8_t type);
    void (*l1i_prefetcher_cache_operate)(uint32_t, uint64_t, uint8_t, uint8_t);
    void (*l1i_prefetcher_cache_fill)(uint32_t, uint64_t, uint32_t, uint32_t, uint8_t, uint64_t);

    uint32_t l2c_prefetcher_operate(uint64_t addr, uint64_t ip, uint8_t cache_hit, uint8_t type, uint32_t metadata_in),
        llc_prefetcher_operate(uint64_t addr, uint64_t ip, uint8_t cache_hit, uint8_t type, uint32_t metadata_in),
        l2c_prefetcher_cache_fill(uint64_t addr, uint32_t set, uint32_t way, uint8_t prefetch, uint64_t evicted_addr, uint32_t metadata_in),
        llc_prefetcher_cache_fill(uint64_t addr, uint32_t set, uint32_t way, uint8_t prefetch, uint64_t evicted_addr, uint32_t metadata_in);

    uint32_t get_set(uint64_t address),
        get_way(uint64_t address, uint32_t set),
        find_victim(uint32_t cpu, uint64_t instr_id, uint32_t set, const BLOCK *current_set, uint64_t ip, uint64_t full_addr, uint32_t type),
        llc_find_victim(uint32_t cpu, uint64_t instr_id, uint32_t set, const BLOCK *current_set, uint64_t ip, uint64_t full_addr, uint32_t type),
        lru_victim(uint32_t cpu, uint64_t instr_id, uint32_t set, const BLOCK *current_set, uint64_t ip, uint64_t full_addr, uint32_t type);

    bool is_in_cache(uint64_t addr);
    bool print_timeliness_stat();
    void broadcast_bw(uint8_t bw_level);
    double get_intime_pref_accuray();
    double get_period_pref_accuray();

    void broadcast_ipc(uint8_t ipc);
    void handle_prefetch_feedback();
};

class InfinityCACHE : public CACHE 
{
public:
    map<uint64_t, BLOCK> blocks;

    /**
     * @brief Detailed Prefetch Performance Analyze Metrics
     * 
     */

    // constructor
    InfinityCACHE(string v1, uint32_t v2, int v3, uint32_t v4, uint32_t v5, uint32_t v6, uint32_t v7, uint32_t v8)
        : CACHE(v1,v2,v3,v4,v5,v6,v7,v8)
    {};

    // destructor
    virtual ~InfinityCACHE()
    {
    };

    // functions
    void handle_fill() override;
    void handle_writeback() override;
    void handle_read() override;
    void handle_prefetch() override;
    void fill_cache(PACKET *packet);
};
#endif
