#include "cache.h"

#define maxRRPV 3

namespace Replacement
{
// These two are only for sampled sets (we use 64 sets)
#define NUM_LEADER_SETS 64
#define maxSHCTR 7
// #define SHCT_SIZE (1<<14)
#define SAT_INC(x, max) (x < max) ? x + 1 : x
#define SAT_DEC(x) (x > 0) ? x - 1 : x
    class ShipPP
    {
    public:
        ShipPP(int sets, int ways, int NUM_CORE, int SHCT_SIZE, int maxrrpv, int maxshctr)
            : sets(sets), ways(ways), shct_size(SHCT_SIZE), maxrrpv(maxrrpv), maxshctr(maxshctr),
              line_rrpv(sets, vector<uint32_t>(ways, maxrrpv)),
              is_prefetch(sets, vector<uint32_t>(ways, false)),
              fill_core(sets, vector<uint32_t>(ways)),
              line_reuse(sets, vector<uint32_t>(ways, false)),
              line_sig(sets, vector<uint64_t>(ways, 0)),
              ship_sample(sets),
              SHCT(NUM_CORE, vector<uint32_t>(SHCT_SIZE, 1))
        {
            int leaders = 0;

            while (leaders < NUM_LEADER_SETS)
            {
                int randval = rand() % sets;

                if (ship_sample[randval] == 0)
                {
                    ship_sample[randval] = 1;
                    leaders++;
                }
            }
        }

        void update_replacement_state(uint32_t cpu, uint32_t set, uint32_t way, uint64_t paddr,
                                      uint64_t PC, uint64_t victim_addr, uint32_t type, uint8_t hit)
        {
            uint32_t sig = line_sig[set][way];

            if (hit)
            { // update to REREF on hit
                if (type != WRITEBACK)
                {

                    if ((type == PREFETCH) && is_prefetch[set][way])
                    {
                        //                line_rrpv[set][way] = 0;

                        if ((ship_sample[set] == 1) && ((rand() % 100 < 5) || (NUM_CPUS == 4)))
                        {
                            uint32_t fill_cpu = fill_core[set][way];

                            SHCT[fill_cpu][sig] = SAT_INC(SHCT[fill_cpu][sig], maxSHCTR);
                            line_reuse[set][way] = true;
                        }
                    }
                    else
                    {
                        line_rrpv[set][way] = 0;

                        if (is_prefetch[set][way])
                        {
                            line_rrpv[set][way] = maxrrpv;
                            is_prefetch[set][way] = false;
                        }

                        if ((ship_sample[set] == 1) && (line_reuse[set][way] == 0))
                        {
                            uint32_t fill_cpu = fill_core[set][way];

                            SHCT[fill_cpu][sig] = SAT_INC(SHCT[fill_cpu][sig], maxSHCTR);
                            line_reuse[set][way] = false;
                        }
                    }
                }

                return;
            }

            //--- All of the below is done only on misses -------
            // remember signature of what is being inserted
            uint64_t use_PC = (type == PREFETCH) ? ((PC << 1) + 1) : (PC << 1);
            uint32_t new_sig = use_PC % shct_size;

            if (ship_sample[set] == 1)
            {
                uint32_t fill_cpu = fill_core[set][way];

                // update signature based on what is getting evicted
                if (line_reuse[set][way] == false)
                {
                    SHCT[fill_cpu][sig] = SAT_DEC(SHCT[fill_cpu][sig]);
                }
                else
                {
                    SHCT[fill_cpu][sig] = SAT_INC(SHCT[fill_cpu][sig], maxSHCTR);
                }

                line_reuse[set][way] = false;
                line_sig[set][way] = new_sig;
                fill_core[set][way] = cpu;
            }

            is_prefetch[set][way] = (type == PREFETCH);

            // Now determine the insertion prediciton

            uint32_t priority_rrpv = maxrrpv - 1; // default SHIP

            if (type == WRITEBACK)
            {
                line_rrpv[set][way] = maxrrpv;
            }
            else if (SHCT[cpu][new_sig] == 0)
            {
                line_rrpv[set][way] = (rand() % 100 >= 0) ? maxrrpv : priority_rrpv; // LowPriorityInstallMostly
            }
            else if (SHCT[cpu][new_sig] == 7)
            {
                line_rrpv[set][way] = (type == PREFETCH) ? 1 : 0; // HighPriority Install
            }
            else
            {
                line_rrpv[set][way] = priority_rrpv; // HighPriority Install
            }
        }

        uint32_t find_victim(uint32_t cpu, uint64_t instr_id, uint32_t set, const BLOCK *current_set, uint64_t PC, uint64_t paddr, uint32_t type)
        {
            // look for the maxRRPV line
            while (1)
            {
                for (int i = 0; i < ways; i++)
                    if (line_rrpv[set][i] == maxRRPV)
                    { // found victim
                        return i;
                    }

                for (int i = 0; i < ways; i++)
                    line_rrpv[set][i]++;
            }

            // WE SHOULD NOT REACH HERE
            assert(0);
            return 0;
        }

    private:
        vector<vector<uint32_t>> line_rrpv;
        vector<vector<uint32_t>> is_prefetch;
        vector<vector<uint32_t>> fill_core;

        vector<uint32_t> ship_sample;
        vector<vector<uint32_t>> line_reuse;
        vector<vector<uint64_t>> line_sig;

        // SHCT. Signature History Counter Table
        // per-core 16K entry. 14-bit signature = 16k entry. 3-bit per entry
        vector<vector<uint32_t>> SHCT;

        int sets, ways, shct_size, maxrrpv, maxshctr;
    };
}

static uint32_t rrpv[L1D_SET][L1D_WAY] = {0};

uint32_t rrpv_victim(uint32_t cpu, uint64_t instr_id, uint32_t set, const BLOCK *current_set, uint64_t ip, uint64_t full_addr, uint32_t type)
{
    while (1)
    {
        for (int i = 0; i < LLC_WAY; i++)
            if (rrpv[set][i] == maxRRPV)
                return i;

        for (int i = 0; i < LLC_WAY; i++)
            rrpv[set][i]++;
    }

    // WE SHOULD NOT REACH HERE
    assert(0);
    return 0;
}

void rrpv_update(uint32_t cpu, uint32_t set, uint32_t way, uint64_t full_addr, uint64_t ip, uint64_t victim_addr, uint32_t type, uint8_t hit)
{
    if (hit)
        rrpv[set][way] = 0;
    else
        rrpv[set][way] = maxRRPV - 1;
}

Replacement::ShipPP ship(L1D_SET, L1D_WAY, NUM_CPUS, 1<<14, 3, 7);

uint32_t CACHE::find_victim(uint32_t cpu, uint64_t instr_id, uint32_t set, const BLOCK *current_set, uint64_t ip, uint64_t full_addr, uint32_t type)
{
    // baseline LRU replacement policy for other caches
    // if (cache_type == IS_L1D)
    // {
    //     // return rrpv_victim(cpu, instr_id, set, current_set, ip, full_addr, type);
    //     return ship.find_victim(cpu, instr_id, set, current_set, ip, full_addr, type);
    // }

    return lru_victim(cpu, instr_id, set, current_set, ip, full_addr, type);
}

void CACHE::update_replacement_state(uint32_t cpu, uint32_t set, uint32_t way, uint64_t full_addr, uint64_t ip, uint64_t victim_addr, uint32_t type, uint8_t hit)
{
    if (type == WRITEBACK)
    {
        if (hit) // wrietback hit does not update LRU state
            return;
    }
    // TODO: 这里可以添加针对L1D的定制化替换算法
    // if (cache_type == IS_L1D)
    // {
    //     // return rrpv_update(cpu, set, way, full_addr, ip, victim_addr, type, hit);
    //     return ship.update_replacement_state(cpu, set, way, full_addr, ip, victim_addr, type, hit);
    // }

    // if (type == PREFETCH) {
    //     return lru_update_prefetch(set, way);
    // } else {
    return lru_update(set, way);
    // }
}

uint32_t CACHE::lru_victim(uint32_t cpu, uint64_t instr_id, uint32_t set, const BLOCK *current_set, uint64_t ip, uint64_t full_addr, uint32_t type)
{
    uint32_t way = 0;

    // fill invalid line first
    for (way = 0; way < NUM_WAY; way++)
    {
        if (block[set][way].valid == false)
        {

            DP(if (warmup_complete[cpu]) {
            cout << "[" << NAME << "] " << __func__ << " instr_id: " << instr_id << " invalid set: " << set << " way: " << way;
            cout << hex << " address: " << (full_addr>>LOG2_BLOCK_SIZE) << " victim address: " << block[set][way].address << " data: " << block[set][way].data;
            cout << dec << " lru: " << block[set][way].lru << endl; });

            break;
        }
    }

    // LRU victim
    if (way == NUM_WAY)
    {
        for (way = 0; way < NUM_WAY; way++)
        {
            if (block[set][way].lru == NUM_WAY - 1)
            {

                DP(if (warmup_complete[cpu]) {
                cout << "[" << NAME << "] " << __func__ << " instr_id: " << instr_id << " replace set: " << set << " way: " << way;
                cout << hex << " address: " << (full_addr>>LOG2_BLOCK_SIZE) << " victim address: " << block[set][way].address << " data: " << block[set][way].data;
                cout << dec << " lru: " << block[set][way].lru << endl; });

                break;
            }
        }
    }

    if (way == NUM_WAY)
    {
        cerr << "[" << NAME << "] " << __func__ << " no victim! set: " << set << endl;
        assert(0);
    }

    return way;
}

void CACHE::lru_update_prefetch(uint32_t set, uint32_t way)
{
    // update lru replacement state
    if (block[set][way].lru <= NUM_WAY/2)
        return;
    for (uint32_t i = 0; i < NUM_WAY; i++)
    {
        if (block[set][i].lru >= NUM_WAY/2 && block[set][i].lru < block[set][way].lru)
        {
            block[set][i].lru++;
        }
    }
    block[set][way].lru = NUM_WAY/2; // promote to the half MRU position
}

void CACHE::lru_update(uint32_t set, uint32_t way)
{
    // update lru replacement state
    for (uint32_t i = 0; i < NUM_WAY; i++)
    {
        if (block[set][i].lru < block[set][way].lru)
        {
            block[set][i].lru++;
        }
    }
    block[set][way].lru = 0; // promote to the MRU position
}

void CACHE::replacement_final_stats()
{
}

#ifdef NO_CRC2_COMPILE
void InitReplacementState()
{
}

uint32_t GetVictimInSet(uint32_t cpu, uint32_t set, const BLOCK *current_set, uint64_t PC, uint64_t paddr, uint32_t type)
{
    return 0;
}

void UpdateReplacementState(uint32_t cpu, uint32_t set, uint32_t way, uint64_t paddr, uint64_t PC, uint64_t victim_addr, uint32_t type, uint8_t hit)
{
}

void PrintStats_Heartbeat()
{
}

void PrintStats()
{
}
#endif
