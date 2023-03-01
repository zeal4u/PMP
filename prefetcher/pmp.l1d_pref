#include "cache.h"
#include "common.h"
#include "component.h"
#include "ooo_cpu.h"
#include <bits/stdc++.h>
#include <random>

using namespace std;

bool SUPPORT_VA = false;

#define DEBUG(x)
#define BOTTOM_BITS 6
#define PC_BITS 5
#define BACKOFF_TIMES 1

#define IN_REGION_BITS 12
#define OFFSET_BITS (IN_REGION_BITS - BOTTOM_BITS)
#define OFFSET_MASK ((1 << OFFSET_BITS) - 1)
#define __fine_offset(addr) (addr & OFFSET_MASK)
#define __coarse_offset(fine_offset) ((fine_offset) >> (LOG2_BLOCK_SIZE - BOTTOM_BITS))

#define START_CONF 0

#define PATTERN_DEGRADE_LEVEL 2

class FilterTableData
{
public:
    int offset;
    uint64_t pc;
};

#define FT_CACHE_TYPE SRRIPSetAssociativeCache

class FilterTable : public FT_CACHE_TYPE<FilterTableData>
{
    typedef FT_CACHE_TYPE<FilterTableData> Super;

public:
    FilterTable(int size, int debug_level = 0, int num_ways = 16) : Super(size, num_ways)
    {
        if (this->debug_level >= 1)
            cerr << "FilterTable::FilterTable(size=" << size << ", debug_level=" << debug_level
                 << ", num_ways=" << num_ways << ")" << dec << endl;
    }

    Entry *find(uint64_t region_number)
    {
        if (this->debug_level >= 2)
            cerr << "FilterTable::find(region_number=0x" << hex << region_number << ")" << dec << endl;
        uint64_t key = this->build_key(region_number);
        Entry *entry = Super::find(key);
        if (!entry)
        {
            if (this->debug_level >= 2)
                cerr << "[FilterTable::find] Miss!" << dec << endl;
            return nullptr;
        }
        if (this->debug_level >= 2)
            cerr << "[FilterTable::find] Hit!" << dec << endl;
        Super::rp_promote(key);
        return entry;
    }

    void insert(uint64_t region_number, int offset, uint64_t pc)
    {
        if (this->debug_level >= 2)
            cerr << "FilterTable::insert(region_number=0x" << hex << region_number 
                 << ", offset=" << dec << offset << ")" << dec << endl;
        uint64_t key = this->build_key(region_number);
        Super::insert(key, {offset, pc});
        Super::rp_insert(key);
    }

    Entry *erase(uint64_t region_number)
    {
        uint64_t key = this->build_key(region_number);
        return Super::erase(key);
    }

    string log()
    {
        vector<string> headers({"Region", "Offset"});
        return Super::log(headers);
    }

private:
    void write_data(Entry &entry, Table &table, int row)
    {
        uint64_t key = hash_index(entry.key, this->index_len);
        table.set_cell(row, 0, key);
        table.set_cell(row, 1, entry.data.offset);
    }

    uint64_t build_key(uint64_t region_number)
    {
        uint64_t key = region_number & ((1ULL << 37) - 1);
        return hash_index(key, this->index_len);
    }
};

class AccumulationTableData
{
public:
    int offset;
    uint64_t pc;
    vector<bool> pattern;
};

#define AT_CACHE_TYPE LRUSetAssociativeCache
class AccumulationTable : public AT_CACHE_TYPE<AccumulationTableData>
{
    typedef AT_CACHE_TYPE<AccumulationTableData> Super;

public:
    AccumulationTable(int size, int pattern_len, int debug_level = 0, int num_ways = 16)
        : Super(size, num_ways), pattern_len(pattern_len)
    {
        if (this->debug_level >= 1)
            cerr << "AccumulationTable::AccumulationTable(size=" << size << ", pattern_len=" << pattern_len
                 << ", debug_level=" << debug_level << ", num_ways=" << num_ways << ")" << dec << endl;
    }

    bool set_pattern(uint64_t region_number, int offset)
    {
        if (this->debug_level >= 2)
            cerr << "AccumulationTable::set_pattern(region_number=0x" << hex << region_number << ", offset=" << dec
                 << offset << ")" << dec << endl;
        uint64_t key = this->build_key(region_number);
        Entry *entry = Super::find(key);
        if (!entry)
        {
            if (this->debug_level >= 2)
                cerr << "[AccumulationTable::set_pattern] Not found!" << dec << endl;
            return false;
        }
        entry->data.pattern[offset] = true;
        Super::rp_promote(key);
        if (this->debug_level >= 2)
            cerr << "[AccumulationTable::set_pattern] OK!" << dec << endl;
        return true;
    }

    Entry insert(uint64_t region_number, uint64_t pc, int offset)
    {
        if (this->debug_level >= 2)
            cerr << "AccumulationTable::insert(region_number=0x" << hex << region_number
                 << ", offset=" << dec << offset << dec << endl;
        uint64_t key = this->build_key(region_number);
        vector<bool> pattern(this->pattern_len, false);
        pattern[__coarse_offset(offset)] = true;
        Entry old_entry = Super::insert(key, {offset, pc, pattern});
        Super::rp_insert(key);
        return old_entry;
    }

    Entry *erase(uint64_t region_number)
    {
        uint64_t key = this->build_key(region_number);
        return Super::erase(key);
    }

    string log()
    {
        vector<string> headers({"Region", "Offset", "Pattern"});
        return Super::log(headers);
    }

private:
    void write_data(Entry &entry, Table &table, int row)
    {
        uint64_t key = hash_index(entry.key, this->index_len);
        table.set_cell(row, 0, key);
        table.set_cell(row, 1, entry.data.offset);
        table.set_cell(row, 2, pattern_to_string(entry.data.pattern));
    }

    uint64_t build_key(uint64_t region_number)
    {
        uint64_t key = region_number & ((1ULL << 37) - 1);
        return hash_index(key, this->index_len);
    }

    int pattern_len;
};


class OffsetPatternTableData
{
public:
    vector<int> pattern;
};

class OffsetPatternTable : public LRUSetAssociativeCache<OffsetPatternTableData>
{
    typedef LRUSetAssociativeCache<OffsetPatternTableData> Super;

public:
    OffsetPatternTable(int size, int pattern_len, int tag_size,
                       int num_ways = 16, int max_conf = 32, 
                       int debug_level = 0, int cpu = 0)
        : Super(size, num_ways, debug_level), pattern_len(pattern_len), tag_size(tag_size),
          max_conf(max_conf), cpu(cpu)
    {
        if (this->debug_level >= 1)
            cerr << "OffsetPatternTable::OffsetPatternTable(size=" << size << ", pattern_len=" << pattern_len
                 << ", tag_size=" << tag_size 
                 << ", debug_level=" << debug_level << ", num_ways=" << num_ways << ")"
                 << dec << endl;
    }

    void insert(uint64_t address, uint64_t pc, vector<bool> pattern, bool is_degrade)
    {
        if (this->debug_level >= 2)
            cerr << "OffsetPatternTable::insert(" << hex << "address=0x" << address
                 << ", pattern=" << pattern_to_string(pattern) << ")" << dec << endl;
        int offset = __coarse_offset(__fine_offset(address));
        offset = is_degrade ? offset / PATTERN_DEGRADE_LEVEL : offset;
        pattern = my_rotate(pattern, -offset);
        uint64_t key = this->build_key(address, pc);
        Entry *entry = Super::find(key);
        assert(pattern[0]);
        if (entry)
        {
            int max_value = 0;
            auto &stored_pattern = entry->data.pattern; 
            for (int i = 0; i < this->pattern_len; i++)
            {
                pattern[i] ? ADD(stored_pattern[i], max_conf) : 0;
                if (i > 0 && max_value < stored_pattern[i]) {
                    max_value = stored_pattern[i];
                }
            }
            
            if (entry->data.pattern[0] == max_conf) {
                if (max_value < (1 << BACKOFF_TIMES)) {
                    entry->data.pattern[0] = max_value;
                }
                else 
                    for (auto &e : stored_pattern) {
                        e >>= BACKOFF_TIMES;
                    }
            }
            Super::rp_promote(key);
        }
        else
        {
            Super::insert(key, {pattern_convert(pattern)});
            Super::rp_insert(key);
        }
    }

    vector<OffsetPatternTableData> find(uint64_t pc, uint64_t block_number)
    {
        if (this->debug_level >= 2)
            cerr << "OffsetPatternTable::find(pc=0x" << hex << pc << ", address=0x" << block_number << ")" << dec << endl;
        uint64_t key = this->build_key(block_number, pc);
        Entry* entry = Super::find(key);
        vector<OffsetPatternTableData> matches;
        if (entry)
        {
            auto &cur_pattern = entry->data;
            matches.push_back(cur_pattern);
        }
        return matches;
    }

    string log()
    {
        vector<string> headers({"Key", "Pattern"});
        return Super::log(headers);
    }

private:
    void write_data(Entry &entry, Table &table, int row)
    {

        table.set_cell(row, 0, entry.key);
        table.set_cell(row, 1, pattern_to_string(entry.data.pattern));
    }

    virtual uint64_t build_key(uint64_t address, uint64_t pc)
    {
        uint64_t offset = __fine_offset(address);
        uint64_t key = offset & ((1 << this->tag_size) - 1);
        return key;
    }

protected:
    const int pattern_len;
    const int tag_size, cpu;
    const int max_conf;
};

class PCPatternTable : public OffsetPatternTable {
public:
    PCPatternTable(int size, int pattern_len, int tag_size,
                   int num_ways = 16, int max_conf = 32, 
                   int debug_level = 0, int cpu = 0) 
                    : OffsetPatternTable(size, pattern_len, tag_size, num_ways, max_conf, debug_level,cpu) {}
private:
    virtual uint64_t build_key(uint64_t address, uint64_t pc) override {
        return hash_index(pc, this->index_len) & ((1 << this->tag_size) - 1);
    }
};

class PrefetchBufferData
{
public:
    vector<int> pattern;
};

#define PS_CACHE_TYPE LRUSetAssociativeCache
class PrefetchBuffer : public PS_CACHE_TYPE<PrefetchBufferData>
{
    typedef PS_CACHE_TYPE<PrefetchBufferData> Super;

public:
    PrefetchBuffer(int size, int pattern_len, int debug_level = 0, int num_ways = 16)
        : Super(size, num_ways), pattern_len(pattern_len)
    {
        if (this->debug_level >= 1)
            cerr << "PrefetchBuffer::PrefetchBuffer(size=" << size << ", pattern_len=" << pattern_len
                 << ", debug_level=" << debug_level << ", num_ways=" << num_ways << ")" << dec << endl;
    }

    void insert(uint64_t region_number, vector<int> pattern)
    {
        if (this->debug_level >= 2)
            cerr << "PrefetchBuffer::insert(region_number=0x" << hex << region_number
                 << ", pattern=" << pattern_to_string(pattern) << ")" << dec << endl;
        uint64_t key = this->build_key(region_number);
        Super::insert(key, {pattern});
        Super::rp_insert(key);
    }

    int prefetch(CACHE *cache, uint64_t block_address)
    {
        if (this->debug_level >= 2)
        {
            cerr << "PrefetchBuffer::prefetch(cache=" << cache->NAME << ", block_address=0x" << hex << block_address
                 << ")" << dec << endl;
            cerr << "[PrefetchBuffer::prefetch] " << cache->PQ.occupancy << "/" << cache->PQ.SIZE
                 << " PQ entries occupied." << dec << endl;
            cerr << "[PrefetchBuffer::prefetch] " << cache->MSHR.occupancy << "/" << cache->MSHR.SIZE
                 << " MSHR entries occupied." << dec << endl;
        }
        uint64_t base_addr = block_address << BOTTOM_BITS;
        int region_offset = __coarse_offset(__fine_offset(block_address));
        uint64_t region_number = block_address >> OFFSET_BITS;
        uint64_t key = this->build_key(region_number);
        Entry *entry = Super::find(key);
        if (!entry)
        {
            if (this->debug_level >= 2)
                cerr << "[PrefetchBuffer::prefetch] No entry found." << dec << endl;
            return 0;
        }
        Super::rp_promote(key);
        int pf_issued = 0;
        vector<int> &pattern = entry->data.pattern;
        pattern[region_offset] = 0; 
        int pf_offset;
        DEBUG(cout << "[Prefetch Begin] base_addr " << hex << base_addr << ", " << dec;)
        for (int d = 1; d < this->pattern_len; d += 1)
        {
            for (int sgn = +1; sgn >= -1; sgn -= 2)
            {
                pf_offset = region_offset + sgn * d;
                if (0 <= pf_offset && pf_offset < this->pattern_len && pattern[pf_offset] > 0)
                {
                    DEBUG(cout << pf_offset << " ";)
                    uint64_t pf_address = (region_number * this->pattern_len + pf_offset) << LOG2_BLOCK_SIZE;
                    if (cache->PQ.occupancy + cache->MSHR.occupancy < cache->MSHR.SIZE - 1 &&
                        cache->PQ.occupancy < cache->PQ.SIZE)
                    {
                        int ok = cache->prefetch_line(0, base_addr, pf_address, pattern[pf_offset], 0);
                        pf_issued += ok;
                        pattern[pf_offset] = 0;
                    }
                    else
                    {
                        DEBUG(cout << endl;)
                        return pf_issued;
                    }
                }
            }
        }
        DEBUG(cout << endl;)
        Super::erase(key);
        return pf_issued;
    }

    string log()
    {
        vector<string> headers({"Region", "Pattern"});
        return Super::log(headers);
    }

private:
    void write_data(Entry &entry, Table &table, int row)
    {
        uint64_t key = hash_index(entry.key, this->index_len);
        table.set_cell(row, 0, key);
        table.set_cell(row, 1, pattern_to_string(entry.data.pattern));
    }

    uint64_t build_key(uint64_t region_number)
    {
        uint64_t key = region_number;
        return hash_index(key, this->index_len);
    }

    int pattern_len;
};

class PMP 
{
public:
     PMP(int pattern_len, int offset_width, int opt_size, int opt_max_conf, int opt_ways, int pc_width, 
          int ppt_size, int ppt_max_conf, int ppt_ways,int filter_table_size, int ft_way,
          int accumulation_table_size, int at_way, int pf_buffer_size, int pf_buffer_way,
          int debug_level = 0, int cpu = 0)
        : pattern_len(pattern_len),
          opt(opt_size, pattern_len, offset_width, opt_ways, opt_max_conf, debug_level, cpu),
          ppt(ppt_size, pattern_len/PATTERN_DEGRADE_LEVEL, pc_width, ppt_ways, ppt_max_conf, debug_level, cpu),
          filter_table(filter_table_size, debug_level, ft_way),
          accumulation_table(accumulation_table_size, pattern_len, debug_level, at_way),
          pf_buffer(pf_buffer_size, pattern_len, debug_level, pf_buffer_way), 
          debug_level(debug_level), cpu(cpu)
    {
        if (this->debug_level >= 1)
            cerr << " PMP:: PMP(pattern_len=" << pattern_len 
                 << ", filter_table_size=" << filter_table_size
                 << ", accumulation_table_size=" << accumulation_table_size 
                 << ", pf_buffer_size=" << pf_buffer_size
                 << ", debug_level=" << debug_level << ")" << endl;
    }


    void access(uint64_t block_number, uint64_t pc)
    {
        if (this->debug_level >= 2)
            cerr << "[ PMP] access(block_number=0x" << hex << block_number << ", pc=0x" << pc << ")" << dec << endl;

        uint64_t region_number = block_number >> OFFSET_BITS;
        int region_offset = __fine_offset(block_number);
        bool success = this->accumulation_table.set_pattern(region_number, __coarse_offset(region_offset));
        if (success)
            return;
        FilterTable::Entry *entry = this->filter_table.find(region_number);
        if (!entry)
        {

            this->filter_table.insert(region_number, region_offset, pc);
            vector<int> pattern = this->find_in_opt(pc, block_number);
            if (pattern.empty())
            {
                return;
            }

            this->pf_buffer.insert(region_number, pattern);
            return;
        }
        if (entry->data.offset != region_offset)
        {
            uint64_t region_number = hash_index(entry->key, this->filter_table.get_index_len());
            AccumulationTable::Entry victim =
                this->accumulation_table.insert(region_number, entry->data.pc, entry->data.offset);
            this->accumulation_table.set_pattern(region_number, __coarse_offset(region_offset));
            this->filter_table.erase(region_number);
            if (victim.valid)
            {
                this->insert_in_opt(victim);
            }
        }
    }

    void eviction(uint64_t block_number)
    {
        if (this->debug_level >= 2)
            cerr << "[ PMP] eviction(block_number=" << block_number << ")" << dec << endl;
        uint64_t region_number = block_number / this->pattern_len;
        this->filter_table.erase(region_number);
        AccumulationTable::Entry *entry = this->accumulation_table.erase(region_number);
        if (entry)
        {
            this->insert_in_opt(*entry);
        }
    }

    int prefetch(CACHE *cache, uint64_t block_number)
    {
        if (this->debug_level >= 2)
            cerr << " PMP::prefetch(cache=" << cache->NAME << ", block_number=" << hex << block_number << ")" << dec
                 << endl;
        int pf_issued = this->pf_buffer.prefetch(cache, block_number);
        if (this->debug_level >= 2)
            cerr << "[ PMP::prefetch] pf_issued=" << pf_issued << dec << endl;
        return pf_issued;
    }

    void set_debug_level(int debug_level)
    {
        this->filter_table.set_debug_level(debug_level);
        this->accumulation_table.set_debug_level(debug_level);
        this->opt.set_debug_level(debug_level);
        this->ppt.set_debug_level(debug_level);
        this->debug_level = debug_level;
    }

    void log()
    {

        cerr << "Filter table begin" << dec << endl;
        cerr << this->filter_table.log();
        cerr << "Filter table end" << endl;

        cerr << "Accumulation table begin" << dec << endl;
        cerr << this->accumulation_table.log();
        cerr << "Accumulation table end" << endl;

        cerr << "Offset pattern table begin" << dec << endl;
        cerr << this->opt.log();
        cerr << "Offset pattern table end" << endl;

        cerr << "PC pattern table begin" << dec << endl;
        cerr << this->ppt.log();
        cerr << "PC pattern table end" << endl;

        cerr << "Prefetch buffer begin" << dec << endl;
        cerr << this->pf_buffer.log();
        cerr << "Prefetch buffer end" << endl;
    }

private:

    vector<int> find_in_opt(uint64_t pc, uint64_t block_number)
    {
        if (this->debug_level >= 2)
        {
            cerr << "[ PMP] find_in_opt(pc=0x" << hex << pc << ", address=0x" << block_number << ")" << dec << endl;
        }
        vector<OffsetPatternTableData> matches = this->opt.find(pc, block_number);
        vector<OffsetPatternTableData> matches_pc = this->ppt.find(pc, block_number);
        vector<int> pattern;
        vector<int> pattern_pc;
        vector<int> result_pattern(this->pattern_len, 0);
        if (!matches.empty())
        {
            pattern = this->vote(matches);
            pattern_pc = this->vote(matches_pc, true);
            if (pattern_pc.empty()) {
                for (int i = 0; i < this->pattern_len; i++) {
                    result_pattern[i] = pattern[i] == FILL_L1 ? FILL_L2 : pattern[i] == FILL_L2 ? FILL_LLC : 0;
                }
            } else {
                for (int i = 0; i < this->pattern_len; i++) {
                    if (pattern[i] == FILL_L1 && pattern_pc[i/PATTERN_DEGRADE_LEVEL] == FILL_L1) {
                        result_pattern[i] = FILL_L1;
                    } else if (pattern[i] == FILL_L1 || pattern_pc[i/PATTERN_DEGRADE_LEVEL] == FILL_L1 || 
                               pattern[i] == FILL_L2 || pattern_pc[i/PATTERN_DEGRADE_LEVEL] == FILL_L2) {
                        result_pattern[i] = FILL_L2;
                    }
                }
            }
        } 

        int offset = __coarse_offset(__fine_offset(block_number));
        result_pattern = my_rotate(result_pattern, +offset);
        return result_pattern;
    }

    void insert_in_opt(const AccumulationTable::Entry &entry)
    {
        uint64_t region_number = hash_index(entry.key, this->accumulation_table.get_index_len());
        uint64_t address = (region_number << OFFSET_BITS) + entry.data.offset;
        if (this->debug_level >= 2)
        {
            cerr << "[ PMP] insert_in_opt(" << hex<< " address=0x" << address << ")" << dec << endl;
        }
        const vector<bool> &pattern = entry.data.pattern;
        if (count_bits(pattern_to_int(pattern)) != 1) {
            this->opt.insert(address, entry.data.pc, pattern, false);
            this->ppt.insert(address, entry.data.pc, pattern_degrade(pattern, PATTERN_DEGRADE_LEVEL), true);
        }
    }

    vector<int> vote(const vector<OffsetPatternTableData> &x, bool is_pc_opt=false)
    {
        if (this->debug_level >= 2)
            cerr << " PMP::vote(...)" << endl;
        int n = x.size();
        if (n == 0)
        {
            if (this->debug_level >= 2)
                cerr << "[ PMP::vote] There are no voters." << endl;
            return vector<int>();
        }

        if (this->debug_level >= 2)
        {
            cerr << "[ PMP::vote] Taking a vote among:" << endl;
            for (int i = 0; i < n; i += 1)
                cerr << "<" << setw(3) << i + 1 << "> " << pattern_to_string(x[i].pattern) << endl;
        }
        bool pf_flag = false;
        int pattern_len = is_pc_opt? this->pattern_len / PATTERN_DEGRADE_LEVEL : this->pattern_len;
        vector<int> res(pattern_len, 0);

        for (int i = 0; i < pattern_len; i += 1)
        {
            int cnt = 0;
            for (int j = 0; j < n; j += 1)
            {
                cnt += x[j].pattern[i];
            }
            double p = 1.0 * cnt / x[0].pattern[0];
            if (p > 1) {
                cout << "cnt:" << cnt << ",total:" << x[0].pattern[0] << endl;
                assert(p <= 1);
            }

            if (x[0].pattern[0] <= START_CONF) {
                break;
            }

            if (is_pc_opt) {
                if (p >= PC_L1D_THRESH)
                    res[i] = FILL_L1;
                else if (p >= PC_L2C_THRESH)
                    res[i] = FILL_L2;
                else if (p >= PC_LLC_THRESH)
                    res[i] = FILL_LLC;
                else
                    res[i] = 0;
            } else {
                if (p >= L1D_THRESH)
                    res[i] = FILL_L1;
                else if (p >= L2C_THRESH)
                    res[i] = FILL_L2;
                else if (p >= LLC_THRESH)
                    res[i] = FILL_LLC;
                else
                    res[i] = 0;
            }
        }
        if (this->debug_level >= 2)
        {
            cerr << "<res> " << pattern_to_string(res) << endl;
        }
        
        return res;
    }

    const double L1D_THRESH = 0.50;
    const double L2C_THRESH = 0.150;
    const double LLC_THRESH = 1; /* off */

    const double PC_L1D_THRESH = 0.50;
    const double PC_L2C_THRESH = 0.150;
    const double PC_LLC_THRESH = 1; /* off */

    /*======================*/

    int pattern_len;
    FilterTable filter_table;
    AccumulationTable accumulation_table;
    OffsetPatternTable opt;
    PCPatternTable ppt;
    PrefetchBuffer pf_buffer;
    int debug_level = 0;
    int cpu;
};

const int DEBUG_LEVEL = 0;

static vector< PMP> prefetchers;

void CACHE::l1d_prefetcher_initialize()
{
    const int PATTERN_LEN = (1 << IN_REGION_BITS)/BLOCK_SIZE;
    const int FT_SIZE = 64;                             
    const int FT_WAY = 8;                               
    const int AT_SIZE = 32;                             
    const int AT_WAY = 16;                              
    const int OPT_WAYS = 1;                               
    const int OPT_SIZE = (1 << OFFSET_BITS) * OPT_WAYS;
    const int OFFSET_MAX_CONF = 32;
    const int PPT_WAYS = 1;
    const int PPT_SIZE = (1 << PC_BITS) * PPT_WAYS;

    const int PC_MAX_CONF = 32;
    const int PF_BUFFER_SIZE = 16;    
    const int PF_BUFFER_WAY  = 16;

    prefetchers = vector< PMP>(
        NUM_CPUS,  PMP(PATTERN_LEN, 
                        OFFSET_BITS, OPT_SIZE, OFFSET_MAX_CONF, OPT_WAYS, 
                        PC_BITS, PPT_SIZE, PC_MAX_CONF, PPT_WAYS,
                        FT_SIZE, FT_WAY, 
                        AT_SIZE, AT_WAY, 
                        PF_BUFFER_SIZE, PF_BUFFER_WAY, 
                        DEBUG_LEVEL, cpu));
}


void CACHE::l1d_prefetcher_operate(uint64_t v_addr, uint64_t p_addr, uint64_t ip, uint8_t cache_hit, uint8_t type) {}

void CACHE::l1d_prefetcher_operate(uint64_t addr, uint64_t ip, uint8_t cache_hit, uint8_t type )
{
    if (DEBUG_LEVEL >= 2)
    {
        cerr << "CACHE::l1d_prefetcher_operate(addr=0x" << hex << addr << ", ip=0x" << ip << ", cache_hit=" << dec
             << (int)cache_hit << ", type=" << (int)type << ")" << dec << endl;
        cerr << "[CACHE::l1d_prefetcher_operate] CACHE{core=" << this->cpu << ", NAME=" << this->NAME << "}" << dec
             << endl;
    }

    if (type != LOAD)
        return;

    uint64_t block_number = addr >> BOTTOM_BITS;

    prefetchers[cpu].access(block_number, ip);

    prefetchers[cpu].prefetch(this, block_number);

    if (DEBUG_LEVEL >= 3)
    {
        prefetchers[cpu].log();
        cerr << "=======================================" << dec << endl;
    }
}

void CACHE::l1d_prefetcher_cache_fill(uint64_t addr, uint32_t set, uint32_t way, uint8_t prefetch, uint64_t evicted_addr, uint32_t metadata_in)
{
    uint64_t evicted_block_number = evicted_addr >> LOG2_BLOCK_SIZE;

    if (this->block[set][way].valid == 0)
        return;

    for (int i = 0; i < NUM_CPUS; i += 1)
    {
        if (!block[set][way].prefetch)
            prefetchers[i].eviction(evicted_block_number);
    }
}

void CACHE::l1d_prefetcher_final_stats()
{
    prefetchers[cpu].log();
}
