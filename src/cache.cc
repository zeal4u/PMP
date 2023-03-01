#include "cache.h"
#include "set.h"

namespace knob{
  bool measure_cache_acc = true;
  uint64_t measure_cache_acc_epoch = 1024;
}
uint64_t l2pf_access = 0;

#define EXTRACT_TYPE(metadata) ((metadata >> 8) & 15)

void InfinityCACHE::handle_fill()
{
  // handle fill
  uint32_t fill_cpu = (MSHR.next_fill_index == MSHR_SIZE) ? NUM_CPUS : MSHR.entry[MSHR.next_fill_index].cpu;
  if (fill_cpu == NUM_CPUS)
    return;

  if (MSHR.next_fill_cycle <= current_core_cycle[fill_cpu])
  {

#ifdef SANITY_CHECK
    if (MSHR.next_fill_index >= MSHR.SIZE)
      assert(0);
#endif

    uint32_t mshr_index = MSHR.next_fill_index;
    // find victim
    uint64_t key = MSHR.entry[mshr_index].address;

    uint8_t do_fill = 1;

    if (do_fill)
    {
      // update prefetcher
      if (cache_type == IS_L1I)
        l1i_prefetcher_cache_fill(fill_cpu, ((MSHR.entry[mshr_index].ip) >> LOG2_BLOCK_SIZE) << LOG2_BLOCK_SIZE, 0, 0, (MSHR.entry[mshr_index].type == PREFETCH) ? 1 : 0, ((blocks[key].ip) >> LOG2_BLOCK_SIZE) << LOG2_BLOCK_SIZE);
      if (cache_type == IS_L1D)
        // you can't get full address in low level cache
        l1d_prefetcher_cache_fill(MSHR.entry[mshr_index].full_addr, 0, 0, (MSHR.entry[mshr_index].type == PREFETCH) ? 1 : 0, blocks[key].address << LOG2_BLOCK_SIZE,
                                  MSHR.entry[mshr_index].pf_metadata);
      if (cache_type == IS_L2C)
        MSHR.entry[mshr_index].pf_metadata = l2c_prefetcher_cache_fill(MSHR.entry[mshr_index].address << LOG2_BLOCK_SIZE /* 这是准备替换的address */, 0, 0, (MSHR.entry[mshr_index].type == PREFETCH) ? 1 : 0,
                                                                       blocks[key].address << LOG2_BLOCK_SIZE /* 这是被替换的address */, MSHR.entry[mshr_index].pf_metadata);
      if (cache_type == IS_LLC)
      {
        cpu = fill_cpu;
        MSHR.entry[mshr_index].pf_metadata = llc_prefetcher_cache_fill(MSHR.entry[mshr_index].address << LOG2_BLOCK_SIZE, 0, 0, (MSHR.entry[mshr_index].type == PREFETCH) ? 1 : 0,
                                                                       blocks[key].address << LOG2_BLOCK_SIZE, MSHR.entry[mshr_index].pf_metadata);
        cpu = 0;
      }

      // COLLECT STATS
      sim_miss[fill_cpu][MSHR.entry[mshr_index].type]++;
      sim_access[fill_cpu][MSHR.entry[mshr_index].type]++;

      fill_cache(&MSHR.entry[mshr_index]);

      // RFO marks cache line dirty
      if (cache_type == IS_L1D)
      {
        if (MSHR.entry[mshr_index].type == RFO)
          blocks[key].dirty = 1;
      }

      // check fill level
      // 如果MSHR的fill level比当前的level小，则说明数据需要进一步向上返回
      if (MSHR.entry[mshr_index].fill_level < fill_level)
      {

        if (fill_level == FILL_L2)
        {
          if (MSHR.entry[mshr_index].fill_l1i)
          {
            upper_level_icache[fill_cpu]->return_data(&MSHR.entry[mshr_index]);
          }
          if (MSHR.entry[mshr_index].fill_l1d)
          {
            upper_level_dcache[fill_cpu]->return_data(&MSHR.entry[mshr_index]);
          }
        }
        else
        {
          if (MSHR.entry[mshr_index].instruction)
            upper_level_icache[fill_cpu]->return_data(&MSHR.entry[mshr_index]);
          if (MSHR.entry[mshr_index].is_data)
            upper_level_dcache[fill_cpu]->return_data(&MSHR.entry[mshr_index]);
        }
      }

      // update processed packets
      if (cache_type == IS_ITLB)
      {
        MSHR.entry[mshr_index].instruction_pa = blocks[key].data;
        if (PROCESSED.occupancy < PROCESSED.SIZE)
          PROCESSED.add_queue(&MSHR.entry[mshr_index]);
      }
      else if (cache_type == IS_DTLB)
      {
        MSHR.entry[mshr_index].data_pa = blocks[key].data;
        if (PROCESSED.occupancy < PROCESSED.SIZE)
          PROCESSED.add_queue(&MSHR.entry[mshr_index]);
      }
      else if (cache_type == IS_L1I)
      {
        if (PROCESSED.occupancy < PROCESSED.SIZE)
          PROCESSED.add_queue(&MSHR.entry[mshr_index]);
      }
      //else if (cache_type == IS_L1D) {
      else if ((cache_type == IS_L1D) && (MSHR.entry[mshr_index].type != PREFETCH))
      {
        if (PROCESSED.occupancy < PROCESSED.SIZE)
          PROCESSED.add_queue(&MSHR.entry[mshr_index]);
      }

      // 计算平均访问延时
      if (warmup_complete[fill_cpu] && (MSHR.entry[mshr_index].cycle_enqueued != 0))
      {
        uint64_t current_miss_latency = (current_core_cycle[fill_cpu] - MSHR.entry[mshr_index].cycle_enqueued);
        /*
		if(cache_type == IS_L1D)
		  {
		    cout << current_core_cycle[fill_cpu] << " - " << MSHR.entry[mshr_index].cycle_enqueued << " = " << current_miss_latency << " MSHR index: " << mshr_index << endl;
		  }
		*/
        total_miss_latency += current_miss_latency;
        miss_latency[cpu][MSHR.entry[mshr_index].type] += current_miss_latency;
      }

      MSHR.remove_queue(&MSHR.entry[mshr_index]);
      MSHR.num_returned--;

      update_fill_cycle();
    }
  }
}

void CACHE::handle_fill()
{
  // handle fill
  uint32_t fill_cpu = (MSHR.next_fill_index == MSHR_SIZE) ? NUM_CPUS : MSHR.entry[MSHR.next_fill_index].cpu;
  if (fill_cpu == NUM_CPUS)
    return;

  if (MSHR.next_fill_cycle <= current_core_cycle[fill_cpu])
  {

#ifdef SANITY_CHECK
    if (MSHR.next_fill_index >= MSHR.SIZE)
      assert(0);
#endif

    uint32_t mshr_index = MSHR.next_fill_index;
    // find victim
    uint32_t set = this->get_set(MSHR.entry[mshr_index].address), way;
    if (cache_type == IS_LLC)
    {
      way = llc_find_victim(fill_cpu, MSHR.entry[mshr_index].instr_id, set, block[set], MSHR.entry[mshr_index].ip, MSHR.entry[mshr_index].full_addr, MSHR.entry[mshr_index].type);
    }
    else
      way = find_victim(fill_cpu, MSHR.entry[mshr_index].instr_id, set, block[set], MSHR.entry[mshr_index].ip, MSHR.entry[mshr_index].full_addr, MSHR.entry[mshr_index].type);

#ifdef LLC_BYPASS
    if ((cache_type == IS_LLC) && (way == LLC_WAY))
    { // this is a bypass that does not fill the LLC

      // update replacement policy
      if (cache_type == IS_LLC)
      {
        llc_update_replacement_state(fill_cpu, set, way, MSHR.entry[mshr_index].full_addr, MSHR.entry[mshr_index].ip, 0, MSHR.entry[mshr_index].type, 0);
      }
      else
        update_replacement_state(fill_cpu, set, way, MSHR.entry[mshr_index].full_addr, MSHR.entry[mshr_index].ip, 0, MSHR.entry[mshr_index].type, 0);

      // COLLECT STATS
      sim_miss[fill_cpu][MSHR.entry[mshr_index].type]++;
      sim_access[fill_cpu][MSHR.entry[mshr_index].type]++;

      // check fill level
      if (MSHR.entry[mshr_index].fill_level < fill_level)
      {

        if (fill_level == FILL_L2)
        {
          if (MSHR.entry[mshr_index].fill_l1i)
          {
            upper_level_icache[fill_cpu]->return_data(&MSHR.entry[mshr_index]);
          }
          if (MSHR.entry[mshr_index].fill_l1d)
          {
            upper_level_dcache[fill_cpu]->return_data(&MSHR.entry[mshr_index]);
          }
        }
        else
        {
          if (MSHR.entry[mshr_index].instruction)
            upper_level_icache[fill_cpu]->return_data(&MSHR.entry[mshr_index]);
          if (MSHR.entry[mshr_index].is_data)
            upper_level_dcache[fill_cpu]->return_data(&MSHR.entry[mshr_index]);
        }
      }

      if (warmup_complete[fill_cpu] && (MSHR.entry[mshr_index].cycle_enqueued != 0))
      {
        uint64_t current_miss_latency = (current_core_cycle[fill_cpu] - MSHR.entry[mshr_index].cycle_enqueued);
        total_miss_latency += current_miss_latency;
        miss_latency[cpu][MSHR.entry[mshr_index].type] += current_miss_latency;
      }

      MSHR.remove_queue(&MSHR.entry[mshr_index]);
      MSHR.num_returned--;

      update_fill_cycle();

      return; // return here, no need to process further in this function
    }
#endif

    uint8_t do_fill = 1;

    // is this dirty?
    // need writeback if dirty
    if (block[set][way].dirty)
    {

      // check if the lower level WQ has enough room to keep this writeback request
      if (lower_level)
      {
        // 2 stand for WQ
        if (lower_level->get_occupancy(2, block[set][way].address) == lower_level->get_size(2, block[set][way].address))
        {

          // lower level WQ is full, cannot replace this victim
          do_fill = 0;
          lower_level->increment_WQ_FULL(block[set][way].address);
          STALL[MSHR.entry[mshr_index].type]++;

          DP(if (warmup_complete[fill_cpu])
             {
               cout << "[" << NAME << "] " << __func__ << "do_fill: " << +do_fill;
               cout << " lower level wq is full!"
                    << " fill_addr: " << hex << MSHR.entry[mshr_index].address;
               cout << " victim_addr: " << block[set][way].tag << dec << endl;
             });
        }
        else
        {
          PACKET writeback_packet;

          writeback_packet.fill_level = fill_level << 1;
          writeback_packet.cpu = fill_cpu;
          writeback_packet.address = block[set][way].address;
          writeback_packet.full_addr = block[set][way].full_addr;
          writeback_packet.data = block[set][way].data;
          writeback_packet.instr_id = MSHR.entry[mshr_index].instr_id;
          writeback_packet.ip = 0; // writeback does not have ip
          writeback_packet.type = WRITEBACK;
          writeback_packet.event_cycle = current_core_cycle[fill_cpu];

          lower_level->add_wq(&writeback_packet);
        }
      }
#ifdef SANITY_CHECK
      else
      {
        // This is LLC, so just throw this dirty cacheline
        // sanity check
        if (cache_type != IS_STLB)
          assert(0);
      }
#endif
    }

    if (do_fill)
    {
      // update prefetcher
      if (cache_type == IS_L1I)
        l1i_prefetcher_cache_fill(fill_cpu, ((MSHR.entry[mshr_index].ip) >> LOG2_BLOCK_SIZE) << LOG2_BLOCK_SIZE, set, way, (MSHR.entry[mshr_index].type == PREFETCH) ? 1 : 0, ((block[set][way].ip) >> LOG2_BLOCK_SIZE) << LOG2_BLOCK_SIZE);
      if (cache_type == IS_L1D)
        // you can't get full address in low level cache
        l1d_prefetcher_cache_fill(MSHR.entry[mshr_index].full_addr, set, way, (MSHR.entry[mshr_index].type == PREFETCH) ? 1 : 0, block[set][way].address << LOG2_BLOCK_SIZE,
                                  MSHR.entry[mshr_index].pf_metadata);
      if (cache_type == IS_L2C) {
      
        // cout << "L2C FULL ADDR:" << hex << MSHR.entry[mshr_index].full_addr << ", ADDR" << (MSHR.entry[mshr_index].address << LOG2_BLOCK_SIZE) << dec << endl;
#ifdef FULL_ADDR
        MSHR.entry[mshr_index].pf_metadata = l2c_prefetcher_cache_fill(MSHR.entry[mshr_index].full_addr /* 这是准备替换的address */, set, way, (MSHR.entry[mshr_index].type == PREFETCH) ? 1 : 0,
                                                                       block[set][way].full_addr/* 这是被替换的address */, MSHR.entry[mshr_index].pf_metadata);
#else                                                                       
        MSHR.entry[mshr_index].pf_metadata = l2c_prefetcher_cache_fill(MSHR.entry[mshr_index].address << LOG2_BLOCK_SIZE, set, way, (MSHR.entry[mshr_index].type == PREFETCH) ? 1 : 0,
                                                                       block[set][way].address << LOG2_BLOCK_SIZE, MSHR.entry[mshr_index].pf_metadata);
#endif                                                                    
      }
      if (cache_type == IS_LLC)
      {
        cpu = fill_cpu;
#ifdef FULL_ADDR
        MSHR.entry[mshr_index].pf_metadata = llc_prefetcher_cache_fill(MSHR.entry[mshr_index].full_addr, set, way, (MSHR.entry[mshr_index].type == PREFETCH) ? 1 : 0,
                                                                       block[set][way].full_addr, MSHR.entry[mshr_index].pf_metadata);
#else                                                                       
        MSHR.entry[mshr_index].pf_metadata = llc_prefetcher_cache_fill(MSHR.entry[mshr_index].address << LOG2_BLOCK_SIZE, set, way, (MSHR.entry[mshr_index].type == PREFETCH) ? 1 : 0,
                                                                       block[set][way].address << LOG2_BLOCK_SIZE, MSHR.entry[mshr_index].pf_metadata);
#endif                                                                    
        cpu = 0;
      }

      // update replacement policy
      if (cache_type == IS_LLC)
      {
        llc_update_replacement_state(fill_cpu, set, way, MSHR.entry[mshr_index].full_addr, MSHR.entry[mshr_index].ip, block[set][way].full_addr, MSHR.entry[mshr_index].type, 0);
      }
      else
        update_replacement_state(fill_cpu, set, way, MSHR.entry[mshr_index].full_addr, MSHR.entry[mshr_index].ip, block[set][way].full_addr, MSHR.entry[mshr_index].type, 0);

      // COLLECT STATS
      sim_miss[fill_cpu][MSHR.entry[mshr_index].type]++;
      sim_access[fill_cpu][MSHR.entry[mshr_index].type]++;

      fill_cache(set, way, &MSHR.entry[mshr_index]);

      // RFO marks cache line dirty
      if (cache_type == IS_L1D)
      {
        if (MSHR.entry[mshr_index].type == RFO)
          block[set][way].dirty = 1;
      }

      // check fill level
      // 如果MSHR的fill level比当前的level小，则说明数据需要进一步向上返回
      if (MSHR.entry[mshr_index].fill_level < fill_level)
      {

        if (fill_level == FILL_L2)
        {
          if (MSHR.entry[mshr_index].fill_l1i)
          {
            upper_level_icache[fill_cpu]->return_data(&MSHR.entry[mshr_index]);
          }
          if (MSHR.entry[mshr_index].fill_l1d)
          {
            upper_level_dcache[fill_cpu]->return_data(&MSHR.entry[mshr_index]);
          }
        }
        else
        {
          if (MSHR.entry[mshr_index].instruction)
            upper_level_icache[fill_cpu]->return_data(&MSHR.entry[mshr_index]);
          if (MSHR.entry[mshr_index].is_data)
            upper_level_dcache[fill_cpu]->return_data(&MSHR.entry[mshr_index]);
        }
      }

      // update processed packets
      if (cache_type == IS_ITLB)
      {
        MSHR.entry[mshr_index].instruction_pa = block[set][way].data;
        if (PROCESSED.occupancy < PROCESSED.SIZE)
          PROCESSED.add_queue(&MSHR.entry[mshr_index]);
      }
      else if (cache_type == IS_DTLB)
      {
        MSHR.entry[mshr_index].data_pa = block[set][way].data;
        if (PROCESSED.occupancy < PROCESSED.SIZE)
          PROCESSED.add_queue(&MSHR.entry[mshr_index]);
      }
      else if (cache_type == IS_L1I)
      {
        if (PROCESSED.occupancy < PROCESSED.SIZE)
          PROCESSED.add_queue(&MSHR.entry[mshr_index]);
      }
      //else if (cache_type == IS_L1D) {
      else if ((cache_type == IS_L1D) && (MSHR.entry[mshr_index].type != PREFETCH))
      {
        if (PROCESSED.occupancy < PROCESSED.SIZE)
          PROCESSED.add_queue(&MSHR.entry[mshr_index]);
      }

      // 计算平均访问延时
      if (warmup_complete[fill_cpu] && (MSHR.entry[mshr_index].cycle_enqueued != 0))
      {
        uint64_t current_miss_latency = (current_core_cycle[fill_cpu] - MSHR.entry[mshr_index].cycle_enqueued);
        /*
		if(cache_type == IS_L1D)
		  {
		    cout << current_core_cycle[fill_cpu] << " - " << MSHR.entry[mshr_index].cycle_enqueued << " = " << current_miss_latency << " MSHR index: " << mshr_index << endl;
		  }
		*/
        total_miss_latency += current_miss_latency;
        miss_latency[cpu][MSHR.entry[mshr_index].type] += current_miss_latency;
      }

      MSHR.remove_queue(&MSHR.entry[mshr_index]);
      MSHR.num_returned--;

      update_fill_cycle();
    }
  }
}

void InfinityCACHE::handle_writeback()
{
  // handle write
  uint32_t writeback_cpu = WQ.entry[WQ.head].cpu;
  if (writeback_cpu == NUM_CPUS)
    return;

  // handle the oldest entry
  if ((WQ.entry[WQ.head].event_cycle <= current_core_cycle[writeback_cpu]) && (WQ.occupancy > 0))
  {
    int index = WQ.head;

    // access cache
    uint64_t key = WQ.entry[index].address;

    if (blocks.find(key) != blocks.end())
    { // writeback hit (or RFO hit for L1D)
      // data already in cacheline

      // COLLECT STATS
      sim_hit[writeback_cpu][WQ.entry[index].type]++;
      sim_access[writeback_cpu][WQ.entry[index].type]++;

      // mark dirty
      blocks[key].dirty = 1;

      if (cache_type == IS_ITLB)
        WQ.entry[index].instruction_pa = blocks[key].data;
      else if (cache_type == IS_DTLB)
        WQ.entry[index].data_pa = blocks[key].data;
      else if (cache_type == IS_STLB)
        WQ.entry[index].data = blocks[key].data;

      // check fill level
      if (WQ.entry[index].fill_level < fill_level)
      {

        if (fill_level == FILL_L2)
        {
          if (WQ.entry[index].fill_l1i)
          {
            upper_level_icache[writeback_cpu]->return_data(&WQ.entry[index]);
          }
          if (WQ.entry[index].fill_l1d)
          {
            upper_level_dcache[writeback_cpu]->return_data(&WQ.entry[index]);
          }
        }
        else
        {
          if (WQ.entry[index].instruction)
            upper_level_icache[writeback_cpu]->return_data(&WQ.entry[index]);
          if (WQ.entry[index].is_data)
            upper_level_dcache[writeback_cpu]->return_data(&WQ.entry[index]);
        }
      }

      HIT[WQ.entry[index].type]++;
      ACCESS[WQ.entry[index].type]++;

      // remove this entry from WQ
      WQ.remove_queue(&WQ.entry[index]);
    }
    else
    { // writeback miss (or RFO miss for L1D)

      DP(if (warmup_complete[writeback_cpu])
         {
           cout << "[" << NAME << "] " << __func__ << " type: " << +WQ.entry[index].type << " miss";
           cout << " instr_id: " << WQ.entry[index].instr_id << " address: " << hex << WQ.entry[index].address;
           cout << " full_addr: " << WQ.entry[index].full_addr << dec;
           cout << " cycle: " << WQ.entry[index].event_cycle << endl;
         });

      if (cache_type == IS_L1D)
      { // RFO miss

        // check mshr
        uint8_t miss_handled = 1;
        int mshr_index = check_mshr(&WQ.entry[index]);

        // mshr_index can't be -2
        if (mshr_index == -2)
        {
          // this is a data/instruction collision in the MSHR, so we have to wait before we can allocate this miss
          miss_handled = 0;
        }
        else if ((mshr_index == -1) && (MSHR.occupancy < MSHR_SIZE))
        { // this is a new miss

          if (cache_type == IS_LLC)
          {
            // check to make sure the DRAM RQ has room for this LLC RFO miss
            if (lower_level->get_occupancy(1, WQ.entry[index].address) == lower_level->get_size(1, WQ.entry[index].address))
            {
              miss_handled = 0;
            }
            else
            {
              add_mshr(&WQ.entry[index]);
              lower_level->add_rq(&WQ.entry[index]);
            }
          }
          else
          {
            // add it to mshr (RFO miss)
            add_mshr(&WQ.entry[index]);

            // add it to the next level's read queue
            //if (lower_level) // L1D always has a lower level cache
            lower_level->add_rq(&WQ.entry[index]);
          }
        }
        else
        {
          if ((mshr_index == -1) && (MSHR.occupancy == MSHR_SIZE))
          { // not enough MSHR resource

            // cannot handle miss request until one of MSHRs is available
            miss_handled = 0;
            STALL[WQ.entry[index].type]++;
          }
          else if (mshr_index != -1)
          { // already in-flight miss

            // update fill_level
            if (WQ.entry[index].fill_level < MSHR.entry[mshr_index].fill_level)
              MSHR.entry[mshr_index].fill_level = WQ.entry[index].fill_level;

            if ((WQ.entry[index].fill_l1i) && (MSHR.entry[mshr_index].fill_l1i != 1))
            {
              MSHR.entry[mshr_index].fill_l1i = 1;
            }
            if ((WQ.entry[index].fill_l1d) && (MSHR.entry[mshr_index].fill_l1d != 1))
            {
              MSHR.entry[mshr_index].fill_l1d = 1;
            }

            // update request
            if (MSHR.entry[mshr_index].type == PREFETCH)
            {
              uint8_t prior_returned = MSHR.entry[mshr_index].returned;
              uint64_t prior_event_cycle = MSHR.entry[mshr_index].event_cycle;
              MSHR.entry[mshr_index] = WQ.entry[index];

              // in case request is already returned, we should keep event_cycle and retunred variables
              MSHR.entry[mshr_index].returned = prior_returned;
              MSHR.entry[mshr_index].event_cycle = prior_event_cycle;
            }

            MSHR_MERGED[WQ.entry[index].type]++;

            DP(if (warmup_complete[writeback_cpu])
               {
                 cout << "[" << NAME << "] " << __func__ << " mshr merged";
                 cout << " instr_id: " << WQ.entry[index].instr_id << " prior_id: " << MSHR.entry[mshr_index].instr_id;
                 cout << " address: " << hex << WQ.entry[index].address;
                 cout << " full_addr: " << WQ.entry[index].full_addr << dec;
                 cout << " cycle: " << WQ.entry[index].event_cycle << endl;
               });
          }
          else
          { // WE SHOULD NOT REACH HERE
            cerr << "[" << NAME << "] MSHR errors" << endl;
            assert(0);
          }
        }

        if (miss_handled)
        {

          MISS[WQ.entry[index].type]++;
          ACCESS[WQ.entry[index].type]++;

          // remove this entry from WQ
          WQ.remove_queue(&WQ.entry[index]);
        }
      }
      else
      {
        // find victim
        uint32_t set = this->get_set(WQ.entry[index].address), way;
#ifdef LLC_BYPASS
        if ((cache_type == IS_LLC) && (way == LLC_WAY))
        {
          cerr << "LLC bypassing for writebacks is not allowed!" << endl;
          assert(0);
        }
#endif

        uint8_t do_fill = 1;

        if (do_fill)
        {
          // COLLECT STATS
          sim_miss[writeback_cpu][WQ.entry[index].type]++;
          sim_access[writeback_cpu][WQ.entry[index].type]++;

          fill_cache(&WQ.entry[index]);

          // mark dirty
          blocks[key].dirty = 1;

          // check fill level
          if (WQ.entry[index].fill_level < fill_level)
          {

            if (fill_level == FILL_L2)
            {
              if (WQ.entry[index].fill_l1i)
              {
                upper_level_icache[writeback_cpu]->return_data(&WQ.entry[index]);
              }
              if (WQ.entry[index].fill_l1d)
              {
                upper_level_dcache[writeback_cpu]->return_data(&WQ.entry[index]);
              }
            }
            else
            {
              if (WQ.entry[index].instruction)
                upper_level_icache[writeback_cpu]->return_data(&WQ.entry[index]);
              if (WQ.entry[index].is_data)
                upper_level_dcache[writeback_cpu]->return_data(&WQ.entry[index]);
            }
          }

          MISS[WQ.entry[index].type]++;
          ACCESS[WQ.entry[index].type]++;

          // remove this entry from WQ
          WQ.remove_queue(&WQ.entry[index]);
        }
      }
    }
  }
}

void CACHE::handle_writeback()
{
  // handle write
  uint32_t writeback_cpu = WQ.entry[WQ.head].cpu;
  if (writeback_cpu == NUM_CPUS)
    return;

  // handle the oldest entry
  if ((WQ.entry[WQ.head].event_cycle <= current_core_cycle[writeback_cpu]) && (WQ.occupancy > 0))
  {
    int index = WQ.head;

    // access cache
    uint32_t set = this->get_set(WQ.entry[index].address);
    int way = check_hit(&WQ.entry[index]);

    if (way >= 0)
    { // writeback hit (or RFO hit for L1D)
      // data already in cacheline

      if (cache_type == IS_LLC)
      {
        llc_update_replacement_state(writeback_cpu, set, way, block[set][way].full_addr, WQ.entry[index].ip, 0, WQ.entry[index].type, 1);
      }
      else
        update_replacement_state(writeback_cpu, set, way, block[set][way].full_addr, WQ.entry[index].ip, 0, WQ.entry[index].type, 1);

      // COLLECT STATS
      sim_hit[writeback_cpu][WQ.entry[index].type]++;
      sim_access[writeback_cpu][WQ.entry[index].type]++;

      // mark dirty
      block[set][way].dirty = 1;

      if (cache_type == IS_ITLB)
        WQ.entry[index].instruction_pa = block[set][way].data;
      else if (cache_type == IS_DTLB)
        WQ.entry[index].data_pa = block[set][way].data;
      else if (cache_type == IS_STLB)
        WQ.entry[index].data = block[set][way].data;

      // check fill level
      if (WQ.entry[index].fill_level < fill_level)
      {

        if (fill_level == FILL_L2)
        {
          if (WQ.entry[index].fill_l1i)
          {
            upper_level_icache[writeback_cpu]->return_data(&WQ.entry[index]);
          }
          if (WQ.entry[index].fill_l1d)
          {
            upper_level_dcache[writeback_cpu]->return_data(&WQ.entry[index]);
          }
        }
        else
        {
          if (WQ.entry[index].instruction)
            upper_level_icache[writeback_cpu]->return_data(&WQ.entry[index]);
          if (WQ.entry[index].is_data)
            upper_level_dcache[writeback_cpu]->return_data(&WQ.entry[index]);
        }
      }

      HIT[WQ.entry[index].type]++;
      ACCESS[WQ.entry[index].type]++;

      // remove this entry from WQ
      WQ.remove_queue(&WQ.entry[index]);
    }
    else
    { // writeback miss (or RFO miss for L1D)

      DP(if (warmup_complete[writeback_cpu])
         {
           cout << "[" << NAME << "] " << __func__ << " type: " << +WQ.entry[index].type << " miss";
           cout << " instr_id: " << WQ.entry[index].instr_id << " address: " << hex << WQ.entry[index].address;
           cout << " full_addr: " << WQ.entry[index].full_addr << dec;
           cout << " cycle: " << WQ.entry[index].event_cycle << endl;
         });

      if (cache_type == IS_L1D)
      { // RFO miss

        // check mshr
        uint8_t miss_handled = 1;
        int mshr_index = check_mshr(&WQ.entry[index]);

        // mshr_index can't be -2
        if (mshr_index == -2)
        {
          // this is a data/instruction collision in the MSHR, so we have to wait before we can allocate this miss
          miss_handled = 0;
        }
        else if ((mshr_index == -1) && (MSHR.occupancy < MSHR_SIZE))
        { // this is a new miss

          if (cache_type == IS_LLC)
          {
            // check to make sure the DRAM RQ has room for this LLC RFO miss
            if (lower_level->get_occupancy(1, WQ.entry[index].address) == lower_level->get_size(1, WQ.entry[index].address))
            {
              miss_handled = 0;
            }
            else
            {
              add_mshr(&WQ.entry[index]);
              lower_level->add_rq(&WQ.entry[index]);
            }
          }
          else
          {
            // add it to mshr (RFO miss)
            add_mshr(&WQ.entry[index]);

            // add it to the next level's read queue
            //if (lower_level) // L1D always has a lower level cache
            lower_level->add_rq(&WQ.entry[index]);
          }
        }
        else
        {
          if ((mshr_index == -1) && (MSHR.occupancy == MSHR_SIZE))
          { // not enough MSHR resource

            // cannot handle miss request until one of MSHRs is available
            miss_handled = 0;
            STALL[WQ.entry[index].type]++;
          }
          else if (mshr_index != -1)
          { // already in-flight miss

            // update fill_level
            if (WQ.entry[index].fill_level < MSHR.entry[mshr_index].fill_level)
              MSHR.entry[mshr_index].fill_level = WQ.entry[index].fill_level;

            if ((WQ.entry[index].fill_l1i) && (MSHR.entry[mshr_index].fill_l1i != 1))
            {
              MSHR.entry[mshr_index].fill_l1i = 1;
            }
            if ((WQ.entry[index].fill_l1d) && (MSHR.entry[mshr_index].fill_l1d != 1))
            {
              MSHR.entry[mshr_index].fill_l1d = 1;
            }

            // update request
            if (MSHR.entry[mshr_index].type == PREFETCH)
            {
              uint8_t prior_returned = MSHR.entry[mshr_index].returned;
              uint64_t prior_event_cycle = MSHR.entry[mshr_index].event_cycle;
              MSHR.entry[mshr_index] = WQ.entry[index];

              // in case request is already returned, we should keep event_cycle and retunred variables
              MSHR.entry[mshr_index].returned = prior_returned;
              MSHR.entry[mshr_index].event_cycle = prior_event_cycle;
            }

            MSHR_MERGED[WQ.entry[index].type]++;

            DP(if (warmup_complete[writeback_cpu])
               {
                 cout << "[" << NAME << "] " << __func__ << " mshr merged";
                 cout << " instr_id: " << WQ.entry[index].instr_id << " prior_id: " << MSHR.entry[mshr_index].instr_id;
                 cout << " address: " << hex << WQ.entry[index].address;
                 cout << " full_addr: " << WQ.entry[index].full_addr << dec;
                 cout << " cycle: " << WQ.entry[index].event_cycle << endl;
               });
          }
          else
          { // WE SHOULD NOT REACH HERE
            cerr << "[" << NAME << "] MSHR errors" << endl;
            assert(0);
          }
        }

        if (miss_handled)
        {

          MISS[WQ.entry[index].type]++;
          ACCESS[WQ.entry[index].type]++;

          // remove this entry from WQ
          WQ.remove_queue(&WQ.entry[index]);
        }
      }
      else
      {
        // find victim
        uint32_t set = this->get_set(WQ.entry[index].address), way;
        if (cache_type == IS_LLC)
        {
          way = llc_find_victim(writeback_cpu, WQ.entry[index].instr_id, set, block[set], WQ.entry[index].ip, WQ.entry[index].full_addr, WQ.entry[index].type);
        }
        else
          way = find_victim(writeback_cpu, WQ.entry[index].instr_id, set, block[set], WQ.entry[index].ip, WQ.entry[index].full_addr, WQ.entry[index].type);

#ifdef LLC_BYPASS
        if ((cache_type == IS_LLC) && (way == LLC_WAY))
        {
          cerr << "LLC bypassing for writebacks is not allowed!" << endl;
          assert(0);
        }
#endif

        uint8_t do_fill = 1;

        // is this dirty?
        if (block[set][way].dirty)
        {

          // check if the lower level WQ has enough room to keep this writeback request
          if (lower_level)
          {
            if (lower_level->get_occupancy(2, block[set][way].address) == lower_level->get_size(2, block[set][way].address))
            {

              // lower level WQ is full, cannot replace this victim
              do_fill = 0;
              lower_level->increment_WQ_FULL(block[set][way].address);
              STALL[WQ.entry[index].type]++;

              DP(if (warmup_complete[writeback_cpu])
                 {
                   cout << "[" << NAME << "] " << __func__ << "do_fill: " << +do_fill;
                   cout << " lower level wq is full!"
                        << " fill_addr: " << hex << WQ.entry[index].address;
                   cout << " victim_addr: " << block[set][way].tag << dec << endl;
                 });
            }
            else
            {
              PACKET writeback_packet;

              writeback_packet.fill_level = fill_level << 1;
              writeback_packet.cpu = writeback_cpu;
              writeback_packet.address = block[set][way].address;
              writeback_packet.full_addr = block[set][way].full_addr;
              writeback_packet.data = block[set][way].data;
              writeback_packet.instr_id = WQ.entry[index].instr_id;
              writeback_packet.ip = 0;
              writeback_packet.type = WRITEBACK;
              writeback_packet.event_cycle = current_core_cycle[writeback_cpu];

              lower_level->add_wq(&writeback_packet);
            }
          }
#ifdef SANITY_CHECK
          else
          {
            // sanity check
            if (cache_type != IS_STLB)
              assert(0);
          }
#endif
        }

        if (do_fill)
        {
          // update prefetcher
          if (cache_type == IS_L1I)
            l1i_prefetcher_cache_fill(writeback_cpu, ((WQ.entry[index].ip) >> LOG2_BLOCK_SIZE) << LOG2_BLOCK_SIZE, set, way, 0, ((block[set][way].ip) >> LOG2_BLOCK_SIZE) << LOG2_BLOCK_SIZE);
          if (cache_type == IS_L1D)
            l1d_prefetcher_cache_fill(WQ.entry[index].full_addr, set, way, 0, block[set][way].address << LOG2_BLOCK_SIZE, WQ.entry[index].pf_metadata);
          else if (cache_type == IS_L2C)
#ifdef FULL_ADDR
            WQ.entry[index].pf_metadata = l2c_prefetcher_cache_fill(WQ.entry[index].full_addr, set, way, 0,
                                                                    block[set][way].full_addr, WQ.entry[index].pf_metadata);
#else
            WQ.entry[index].pf_metadata = l2c_prefetcher_cache_fill(WQ.entry[index].address << LOG2_BLOCK_SIZE, set, way, 0,
                                                                    block[set][way].address << LOG2_BLOCK_SIZE, WQ.entry[index].pf_metadata);
#endif
                                                                  
          if (cache_type == IS_LLC)
          {
            cpu = writeback_cpu;
#ifdef FULL_ADDR
            WQ.entry[index].pf_metadata = llc_prefetcher_cache_fill(WQ.entry[index].full_addr, set, way, 0,
                                                                    block[set][way].full_addr, WQ.entry[index].pf_metadata);
#else
            WQ.entry[index].pf_metadata = llc_prefetcher_cache_fill(WQ.entry[index].address << LOG2_BLOCK_SIZE, set, way, 0,
                                                                    block[set][way].address << LOG2_BLOCK_SIZE, WQ.entry[index].pf_metadata);
#endif
            cpu = 0;
          }

          // update replacement policy
          if (cache_type == IS_LLC)
          {
            llc_update_replacement_state(writeback_cpu, set, way, WQ.entry[index].full_addr, WQ.entry[index].ip, block[set][way].full_addr, WQ.entry[index].type, 0);
          }
          else
            update_replacement_state(writeback_cpu, set, way, WQ.entry[index].full_addr, WQ.entry[index].ip, block[set][way].full_addr, WQ.entry[index].type, 0);

          // COLLECT STATS
          sim_miss[writeback_cpu][WQ.entry[index].type]++;
          sim_access[writeback_cpu][WQ.entry[index].type]++;

          fill_cache(set, way, &WQ.entry[index]);

          // mark dirty
          block[set][way].dirty = 1;

          // check fill level
          if (WQ.entry[index].fill_level < fill_level)
          {

            if (fill_level == FILL_L2)
            {
              if (WQ.entry[index].fill_l1i)
              {
                upper_level_icache[writeback_cpu]->return_data(&WQ.entry[index]);
              }
              if (WQ.entry[index].fill_l1d)
              {
                upper_level_dcache[writeback_cpu]->return_data(&WQ.entry[index]);
              }
            }
            else
            {
              if (WQ.entry[index].instruction)
                upper_level_icache[writeback_cpu]->return_data(&WQ.entry[index]);
              if (WQ.entry[index].is_data)
                upper_level_dcache[writeback_cpu]->return_data(&WQ.entry[index]);
            }
          }

          MISS[WQ.entry[index].type]++;
          ACCESS[WQ.entry[index].type]++;

          // remove this entry from WQ
          WQ.remove_queue(&WQ.entry[index]);
        }
      }
    }
  }
}

void InfinityCACHE::handle_read()
{
  for (uint32_t i = 0; i < MAX_READ; i++)
  {

    uint32_t read_cpu = RQ.entry[RQ.head].cpu;
    if (read_cpu == NUM_CPUS)
      return;

    // handle the oldest entry
    if ((RQ.entry[RQ.head].event_cycle <= current_core_cycle[read_cpu]) && (RQ.occupancy > 0))
    {
      int index = RQ.head;

      // access cache
      uint64_t key = RQ.entry[index].address;

      if (blocks.find(key) != blocks.end())
      { // read hit

        if (cache_type == IS_ITLB)
        {
          RQ.entry[index].instruction_pa = blocks[key].data;
          if (PROCESSED.occupancy < PROCESSED.SIZE)
            PROCESSED.add_queue(&RQ.entry[index]);
        }
        else if (cache_type == IS_DTLB)
        {
          RQ.entry[index].data_pa = blocks[key].data;
          if (PROCESSED.occupancy < PROCESSED.SIZE)
            PROCESSED.add_queue(&RQ.entry[index]);
        }
        else if (cache_type == IS_STLB)
          RQ.entry[index].data = blocks[key].data;
        else if (cache_type == IS_L1I)
        {
          if (PROCESSED.occupancy < PROCESSED.SIZE)
            PROCESSED.add_queue(&RQ.entry[index]);
        }
        else if ((cache_type == IS_L1D) && (RQ.entry[index].type != PREFETCH))
        {
          if (PROCESSED.occupancy < PROCESSED.SIZE)
            PROCESSED.add_queue(&RQ.entry[index]);
        }

        // update prefetcher on load instruction
        if (RQ.entry[index].type == LOAD)
        {
          if (cache_type == IS_L1I)
            l1i_prefetcher_cache_operate(read_cpu, RQ.entry[index].ip, 1, blocks[key].prefetch);
          if (cache_type == IS_L1D)
            if (SUPPORT_VA)
              l1d_prefetcher_operate(RQ.entry[index].v_full_addr, RQ.entry[index].full_addr, RQ.entry[index].ip, 1, RQ.entry[index].type);
            else
              l1d_prefetcher_operate(RQ.entry[index].full_addr, RQ.entry[index].ip, 1, RQ.entry[index].type);
          else if (cache_type == IS_L2C)
#ifdef FULL_ADDR
            l2c_prefetcher_operate(blocks[key].full_addr, RQ.entry[index].ip, 1, RQ.entry[index].type, 0);
#else
            l2c_prefetcher_operate(blocks[key].address << LOG2_BLOCK_SIZE, RQ.entry[index].ip, 1, RQ.entry[index].type, 0);
#endif
          else if (cache_type == IS_LLC)
          {
            cpu = read_cpu;
#ifdef FULL_ADDR
            llc_prefetcher_operate(blocks[key].full_addr, RQ.entry[index].ip, 1, RQ.entry[index].type, 0);
#else
            llc_prefetcher_operate(blocks[key].address << LOG2_BLOCK_SIZE, RQ.entry[index].ip, 1, RQ.entry[index].type, 0);
#endif
            cpu = 0;
          }
        }

        // COLLECT STATS
        sim_hit[read_cpu][RQ.entry[index].type]++;
        sim_access[read_cpu][RQ.entry[index].type]++;

        // check fill level
        if (RQ.entry[index].fill_level < fill_level)
        {

          if (fill_level == FILL_L2)
          {
            if (RQ.entry[index].fill_l1i)
            {
              upper_level_icache[read_cpu]->return_data(&RQ.entry[index]);
            }
            if (RQ.entry[index].fill_l1d)
            {
              upper_level_dcache[read_cpu]->return_data(&RQ.entry[index]);
            }
          }
          else
          {
            if (RQ.entry[index].instruction)
              upper_level_icache[read_cpu]->return_data(&RQ.entry[index]);
            if (RQ.entry[index].is_data)
              upper_level_dcache[read_cpu]->return_data(&RQ.entry[index]);
          }
        }

        // update prefetch stats and reset prefetch bit
        // spp的部分逻辑可以省去，也可以直接利用cache自带的统计进行预取的优化工作
        if (blocks[key].prefetch)
        {
          pf_useful++;
          pf_useful_epoch++;
          // TODO
#ifdef MATRYOSHKA
          int type = EXTRACT_TYPE(blocks[key].pf_metadata);
          pref_useful[cpu][type]++;
#else
          int trigger_offset = (blocks[key].pf_metadata);
          pref_useful[cpu][trigger_offset]++;
#endif
          blocks[key].prefetch = 0;
        }
        blocks[key].used = 1;

        HIT[RQ.entry[index].type]++;
        ACCESS[RQ.entry[index].type]++;

        // remove this entry from RQ
        RQ.remove_queue(&RQ.entry[index]);
        reads_available_this_cycle--;
      }
      else
      { // read miss

        DP(if (warmup_complete[read_cpu])
           {
             cout << "[" << NAME << "] " << __func__ << " read miss";
             cout << " instr_id: " << RQ.entry[index].instr_id << " address: " << hex << RQ.entry[index].address;
             cout << " full_addr: " << RQ.entry[index].full_addr << dec;
             cout << " cycle: " << RQ.entry[index].event_cycle << endl;
           });

        // check mshr
        uint8_t miss_handled = 1;
        int mshr_index = check_mshr(&RQ.entry[index]);

        if (mshr_index == -2)
        {
          // this is a data/instruction collision in the MSHR, so we have to wait before we can allocate this miss
          miss_handled = 0;
        }
        else if ((mshr_index == -1) && (MSHR.occupancy < MSHR_SIZE))
        { // this is a new miss

          if (cache_type == IS_LLC)
          {
            // check to make sure the DRAM RQ has room for this LLC read miss
            if (lower_level->get_occupancy(1, RQ.entry[index].address) == lower_level->get_size(1, RQ.entry[index].address))
            {
              miss_handled = 0;
            }
            else
            {
              add_mshr(&RQ.entry[index]);
              if (lower_level)
              {
                lower_level->add_rq(&RQ.entry[index]);
              }
            }
          }
          else
          {
            // add it to mshr (read miss)
            add_mshr(&RQ.entry[index]);

            // add it to the next level's read queue
            if (lower_level)
              lower_level->add_rq(&RQ.entry[index]);
            else
            { // this is the last level
              if (cache_type == IS_STLB)
              {
                // TODO: need to differentiate page table walk and actual swap

                // emulate page table walk
                RQ.entry[index].v_full_addr = RQ.entry[index].full_addr;
                uint64_t pa = va_to_pa(read_cpu, RQ.entry[index].instr_id, RQ.entry[index].full_addr, RQ.entry[index].address, 0);

                RQ.entry[index].data = pa >> LOG2_PAGE_SIZE;
                RQ.entry[index].event_cycle = current_core_cycle[read_cpu];
                // TODO 这里不应该返回给上一级吗？这样相当于只是写入了STLB的MSHR
                return_data(&RQ.entry[index]);
              }
            }
          }
        }
        else
        {
          if ((mshr_index == -1) && (MSHR.occupancy == MSHR_SIZE))
          { // not enough MSHR resource

            // cannot handle miss request until one of MSHRs is available
            miss_handled = 0;
            STALL[RQ.entry[index].type]++;
          }
          else if (mshr_index != -1)
          { // already in-flight miss

            // mark merged consumer
            if (RQ.entry[index].type == RFO)
            {

              if (RQ.entry[index].tlb_access)
              {
                uint32_t sq_index = RQ.entry[index].sq_index;
                MSHR.entry[mshr_index].store_merged = 1;
                MSHR.entry[mshr_index].sq_index_depend_on_me.insert(sq_index);
                MSHR.entry[mshr_index].sq_index_depend_on_me.join(RQ.entry[index].sq_index_depend_on_me, SQ_SIZE);
              }

              if (RQ.entry[index].load_merged)
              {
                //uint32_t lq_index = RQ.entry[index].lq_index;
                MSHR.entry[mshr_index].load_merged = 1;
                //MSHR.entry[mshr_index].lq_index_depend_on_me[lq_index] = 1;
                MSHR.entry[mshr_index].lq_index_depend_on_me.join(RQ.entry[index].lq_index_depend_on_me, LQ_SIZE);
              }
            }
            else
            {
              if (RQ.entry[index].instruction)
              {
                uint32_t rob_index = RQ.entry[index].rob_index;
                MSHR.entry[mshr_index].instruction = 1; // add as instruction type
                MSHR.entry[mshr_index].instr_merged = 1;
                MSHR.entry[mshr_index].rob_index_depend_on_me.insert(rob_index);

                DP(if (warmup_complete[MSHR.entry[mshr_index].cpu])
                   {
                     cout << "[INSTR_MERGED] " << __func__ << " cpu: " << MSHR.entry[mshr_index].cpu << " instr_id: " << MSHR.entry[mshr_index].instr_id;
                     cout << " merged rob_index: " << rob_index << " instr_id: " << RQ.entry[index].instr_id << endl;
                   });

                if (RQ.entry[index].instr_merged)
                {
                  MSHR.entry[mshr_index].rob_index_depend_on_me.join(RQ.entry[index].rob_index_depend_on_me, ROB_SIZE);
                  DP(if (warmup_complete[MSHR.entry[mshr_index].cpu])
                     {
                       cout << "[INSTR_MERGED] " << __func__ << " cpu: " << MSHR.entry[mshr_index].cpu << " instr_id: " << MSHR.entry[mshr_index].instr_id;
                       cout << " merged rob_index: " << i << " instr_id: N/A" << endl;
                     });
                }
              }
              else
              {
                uint32_t lq_index = RQ.entry[index].lq_index;
                MSHR.entry[mshr_index].is_data = 1; // add as data type
                MSHR.entry[mshr_index].load_merged = 1;
                MSHR.entry[mshr_index].lq_index_depend_on_me.insert(lq_index);

                DP(if (warmup_complete[read_cpu])
                   {
                     cout << "[DATA_MERGED] " << __func__ << " cpu: " << read_cpu << " instr_id: " << RQ.entry[index].instr_id;
                     cout << " merged rob_index: " << RQ.entry[index].rob_index << " instr_id: " << RQ.entry[index].instr_id << " lq_index: " << RQ.entry[index].lq_index << endl;
                   });
                MSHR.entry[mshr_index].lq_index_depend_on_me.join(RQ.entry[index].lq_index_depend_on_me, LQ_SIZE);
                if (RQ.entry[index].store_merged)
                {
                  MSHR.entry[mshr_index].store_merged = 1;
                  MSHR.entry[mshr_index].sq_index_depend_on_me.join(RQ.entry[index].sq_index_depend_on_me, SQ_SIZE);
                }
              }
            }

            // update fill_level
            if (RQ.entry[index].fill_level < MSHR.entry[mshr_index].fill_level)
              MSHR.entry[mshr_index].fill_level = RQ.entry[index].fill_level;

            if ((RQ.entry[index].fill_l1i) && (MSHR.entry[mshr_index].fill_l1i != 1))
            {
              MSHR.entry[mshr_index].fill_l1i = 1;
            }
            if ((RQ.entry[index].fill_l1d) && (MSHR.entry[mshr_index].fill_l1d != 1))
            {
              MSHR.entry[mshr_index].fill_l1d = 1;
            }

            // update request
            if (MSHR.entry[mshr_index].type == PREFETCH)
            {
              uint8_t prior_returned = MSHR.entry[mshr_index].returned;
              uint64_t prior_event_cycle = MSHR.entry[mshr_index].event_cycle;
              MSHR.entry[mshr_index] = RQ.entry[index];

              // in case request is already returned, we should keep event_cycle and retunred variables
              MSHR.entry[mshr_index].returned = prior_returned;
              MSHR.entry[mshr_index].event_cycle = prior_event_cycle;
            }

            MSHR_MERGED[RQ.entry[index].type]++;

            DP(if (warmup_complete[read_cpu])
               {
                 cout << "[" << NAME << "] " << __func__ << " mshr merged";
                 cout << " instr_id: " << RQ.entry[index].instr_id << " prior_id: " << MSHR.entry[mshr_index].instr_id;
                 cout << " address: " << hex << RQ.entry[index].address;
                 cout << " full_addr: " << RQ.entry[index].full_addr << dec;
                 cout << " cycle: " << RQ.entry[index].event_cycle << endl;
               });
          }
          else
          { // WE SHOULD NOT REACH HERE
            cerr << "[" << NAME << "] MSHR errors" << endl;
            assert(0);
          }
        }

        if (miss_handled)
        {
          // update prefetcher on load instruction
          if (RQ.entry[index].type == LOAD)
          {
            if (cache_type == IS_L1I)
              l1i_prefetcher_cache_operate(read_cpu, RQ.entry[index].ip, 0, 0);
            if (cache_type == IS_L1D)
              if (SUPPORT_VA)
                l1d_prefetcher_operate(RQ.entry[index].v_full_addr, RQ.entry[index].full_addr, RQ.entry[index].ip, 0, RQ.entry[index].type);
              else
                l1d_prefetcher_operate(RQ.entry[index].full_addr, RQ.entry[index].ip, 0, RQ.entry[index].type);
            if (cache_type == IS_L2C)
#ifdef FULL_ADDR
              l2c_prefetcher_operate(RQ.entry[index].full_addr, RQ.entry[index].ip, 0, RQ.entry[index].type, 0);
#else
              l2c_prefetcher_operate(RQ.entry[index].address << LOG2_BLOCK_SIZE, RQ.entry[index].ip, 0, RQ.entry[index].type, 0);
#endif
            if (cache_type == IS_LLC)
            {
              cpu = read_cpu;
#ifdef FULL_ADDR
              llc_prefetcher_operate(RQ.entry[index].full_addr, RQ.entry[index].ip, 0, RQ.entry[index].type, 0);
#else
              llc_prefetcher_operate(RQ.entry[index].address << LOG2_BLOCK_SIZE, RQ.entry[index].ip, 0, RQ.entry[index].type, 0);
#endif
              cpu = 0;
            }
          }

          MISS[RQ.entry[index].type]++;
          ACCESS[RQ.entry[index].type]++;

          // remove this entry from RQ
          RQ.remove_queue(&RQ.entry[index]);
          reads_available_this_cycle--;
        }
      }
    }
    else
    {
      return;
    }

    if (reads_available_this_cycle == 0)
    {
      return;
    }
  }
}

void CACHE::handle_read()
{
  // handle read
  for (uint32_t i = 0; i < MAX_READ; i++)
  {

    uint32_t read_cpu = RQ.entry[RQ.head].cpu;
    if (read_cpu == NUM_CPUS)
      return;

    // handle the oldest entry
    if ((RQ.entry[RQ.head].event_cycle <= current_core_cycle[read_cpu]) && (RQ.occupancy > 0))
    {
      int index = RQ.head;

      // access cache
      uint32_t set = this->get_set(RQ.entry[index].address);
      int way = check_hit(&RQ.entry[index]);

      if (way >= 0)
      { // read hit
#ifdef MEASURE_COMPULSORY
        cmr_.record(RQ.entry[index].address, RQ.entry[index].type, true);
#endif
        if (cache_type == IS_ITLB)
        {
          RQ.entry[index].instruction_pa = block[set][way].data;
          if (PROCESSED.occupancy < PROCESSED.SIZE)
            PROCESSED.add_queue(&RQ.entry[index]);
        }
        else if (cache_type == IS_DTLB)
        {
          RQ.entry[index].data_pa = block[set][way].data;
          if (PROCESSED.occupancy < PROCESSED.SIZE)
            PROCESSED.add_queue(&RQ.entry[index]);
        }
        else if (cache_type == IS_STLB)
          RQ.entry[index].data = block[set][way].data;
        else if (cache_type == IS_L1I)
        {
          if (PROCESSED.occupancy < PROCESSED.SIZE)
            PROCESSED.add_queue(&RQ.entry[index]);
        }
        else if ((cache_type == IS_L1D) && (RQ.entry[index].type != PREFETCH))
        {
          if (PROCESSED.occupancy < PROCESSED.SIZE)
            PROCESSED.add_queue(&RQ.entry[index]);
        }

        // update prefetcher on load instruction
        if (RQ.entry[index].type == LOAD)
        {
          if (cache_type == IS_L1I)
            l1i_prefetcher_cache_operate(read_cpu, RQ.entry[index].ip, 1, block[set][way].prefetch);
          if (cache_type == IS_L1D)
            if (SUPPORT_VA)
              l1d_prefetcher_operate(RQ.entry[index].v_full_addr, RQ.entry[index].full_addr, RQ.entry[index].ip, 1, RQ.entry[index].type);
            else
              l1d_prefetcher_operate(RQ.entry[index].full_addr, RQ.entry[index].ip, 1, RQ.entry[index].type);
          else if (cache_type == IS_L2C)
#ifdef FULL_ADDR
            l2c_prefetcher_operate(block[set][way].full_addr, RQ.entry[index].ip, 1, RQ.entry[index].type, 0);
#else
            l2c_prefetcher_operate(block[set][way].address << LOG2_BLOCK_SIZE, RQ.entry[index].ip, 1, RQ.entry[index].type, 0);
#endif
          else if (cache_type == IS_LLC)
          {
            cpu = read_cpu;
#ifdef FULL_ADDR
            llc_prefetcher_operate(block[set][way].full_addr, RQ.entry[index].ip, 1, RQ.entry[index].type, 0);
#else
            llc_prefetcher_operate(block[set][way].address << LOG2_BLOCK_SIZE, RQ.entry[index].ip, 1, RQ.entry[index].type, 0);
#endif
            cpu = 0;
          }
        }

        // update replacement policy
        if (cache_type == IS_LLC)
        {
          llc_update_replacement_state(read_cpu, set, way, block[set][way].full_addr, RQ.entry[index].ip, 0, RQ.entry[index].type, 1);
        }
        else
          update_replacement_state(read_cpu, set, way, block[set][way].full_addr, RQ.entry[index].ip, 0, RQ.entry[index].type, 1);

        // COLLECT STATS
        sim_hit[read_cpu][RQ.entry[index].type]++;
        sim_access[read_cpu][RQ.entry[index].type]++;

        // check fill level
        if (RQ.entry[index].fill_level < fill_level)
        {
          if (fill_level == FILL_L2)
          {
            if (RQ.entry[index].fill_l1i)
            {
              upper_level_icache[read_cpu]->return_data(&RQ.entry[index]);
            }
            if (RQ.entry[index].fill_l1d)
            {
              upper_level_dcache[read_cpu]->return_data(&RQ.entry[index]);
            }
          }
          else
          {
            if (RQ.entry[index].instruction)
              upper_level_icache[read_cpu]->return_data(&RQ.entry[index]);
            if (RQ.entry[index].is_data)
              upper_level_dcache[read_cpu]->return_data(&RQ.entry[index]);
          }
        }

        // update prefetch stats and reset prefetch bit
        // spp的部分逻辑可以省去，也可以直接利用cache自带的统计进行预取的优化工作
        if (block[set][way].prefetch)
        {
#ifdef RECORD_INFO_OUTPUT
uint64_t address = block[set][way].address;
g_fout.fout << pf_addr_scores[address].pattern << "," << block[set][way].full_addr << "," << pf_addr_scores[address].pc << "," << pf_addr_scores[address].trigger_offset << ","
<< pf_addr_scores[address].score << "," << pf_addr_scores[address].cache_hit << ","<< 1 << endl;
#endif
#ifdef ANALYZE
          if (cache_type == IS_L1D) {
            g_fout.fout << block[set][way].full_addr << ",1" << endl;
          }
#endif
          pf_useful++;
          pf_useful_epoch++;
          // TODO
#ifdef MATRYOSHKA
          int type = EXTRACT_TYPE(block[set][way].pf_metadata);
          pref_useful[cpu][type]++;
#else
          int trigger_offset = (block[set][way].pf_metadata);
          pref_useful[cpu][trigger_offset]++;
#endif

          block[set][way].prefetch = 0;
        }
        block[set][way].used = 1;

        HIT[RQ.entry[index].type]++;
        ACCESS[RQ.entry[index].type]++;

        // remove this entry from RQ
        RQ.remove_queue(&RQ.entry[index]);
        reads_available_this_cycle--;
      }
      else
      { // read miss
#ifdef MEASURE_COMPULSORY
        cmr_.record(RQ.entry[index].address, RQ.entry[index].type, false);
#endif
        DP(if (warmup_complete[read_cpu])
           {
             cout << "[" << NAME << "] " << __func__ << " read miss";
             cout << " instr_id: " << RQ.entry[index].instr_id << " address: " << hex << RQ.entry[index].address;
             cout << " full_addr: " << RQ.entry[index].full_addr << dec;
             cout << " cycle: " << RQ.entry[index].event_cycle << endl;
           });

        // check mshr
        uint8_t miss_handled = 1;
        int mshr_index = check_mshr(&RQ.entry[index]);

        if (mshr_index == -2)
        {
          // this is a data/instruction collision in the MSHR, so we have to wait before we can allocate this miss
          miss_handled = 0;
        }
        else if ((mshr_index == -1) && (MSHR.occupancy < MSHR_SIZE))
        { // this is a new miss

          if (cache_type == IS_LLC)
          {
            // check to make sure the DRAM RQ has room for this LLC read miss
            if (lower_level->get_occupancy(1, RQ.entry[index].address) == lower_level->get_size(1, RQ.entry[index].address))
            {
              miss_handled = 0;
            }
            else
            {
              add_mshr(&RQ.entry[index]);
              if (lower_level)
              {
                lower_level->add_rq(&RQ.entry[index]);
              }
            }
          }
          else
          {
            // add it to mshr (read miss)
            add_mshr(&RQ.entry[index]);

            // add it to the next level's read queue
            if (lower_level)
              lower_level->add_rq(&RQ.entry[index]);
            else
            { // this is the last level
              if (cache_type == IS_STLB)
              {
                // TODO: need to differentiate page table walk and actual swap

                // emulate page table walk
                RQ.entry[index].v_full_addr = RQ.entry[index].full_addr;
                uint64_t pa = va_to_pa(read_cpu, RQ.entry[index].instr_id, RQ.entry[index].full_addr, RQ.entry[index].address, 0);

                RQ.entry[index].data = pa >> LOG2_PAGE_SIZE;
                RQ.entry[index].event_cycle = current_core_cycle[read_cpu];
                // TODO 这里不应该返回给上一级吗？这样相当于只是写入了STLB的MSHR
                return_data(&RQ.entry[index]);
              }
            }
          }
        }
        else
        {
          if ((mshr_index == -1) && (MSHR.occupancy == MSHR_SIZE))
          { // not enough MSHR resource

            // cannot handle miss request until one of MSHRs is available
            miss_handled = 0;
            STALL[RQ.entry[index].type]++;
          }
          else if (mshr_index != -1)
          { // already in-flight miss

            // mark merged consumer
            if (RQ.entry[index].type == RFO)
            {

              if (RQ.entry[index].tlb_access)
              {
                uint32_t sq_index = RQ.entry[index].sq_index;
                MSHR.entry[mshr_index].store_merged = 1;
                MSHR.entry[mshr_index].sq_index_depend_on_me.insert(sq_index);
                MSHR.entry[mshr_index].sq_index_depend_on_me.join(RQ.entry[index].sq_index_depend_on_me, SQ_SIZE);
              }

              if (RQ.entry[index].load_merged)
              {
                //uint32_t lq_index = RQ.entry[index].lq_index;
                MSHR.entry[mshr_index].load_merged = 1;
                //MSHR.entry[mshr_index].lq_index_depend_on_me[lq_index] = 1;
                MSHR.entry[mshr_index].lq_index_depend_on_me.join(RQ.entry[index].lq_index_depend_on_me, LQ_SIZE);
              }
            }
            else
            {
              if (RQ.entry[index].instruction)
              {
                uint32_t rob_index = RQ.entry[index].rob_index;
                MSHR.entry[mshr_index].instruction = 1; // add as instruction type
                MSHR.entry[mshr_index].instr_merged = 1;
                MSHR.entry[mshr_index].rob_index_depend_on_me.insert(rob_index);

                DP(if (warmup_complete[MSHR.entry[mshr_index].cpu])
                   {
                     cout << "[INSTR_MERGED] " << __func__ << " cpu: " << MSHR.entry[mshr_index].cpu << " instr_id: " << MSHR.entry[mshr_index].instr_id;
                     cout << " merged rob_index: " << rob_index << " instr_id: " << RQ.entry[index].instr_id << endl;
                   });

                if (RQ.entry[index].instr_merged)
                {
                  MSHR.entry[mshr_index].rob_index_depend_on_me.join(RQ.entry[index].rob_index_depend_on_me, ROB_SIZE);
                  DP(if (warmup_complete[MSHR.entry[mshr_index].cpu])
                     {
                       cout << "[INSTR_MERGED] " << __func__ << " cpu: " << MSHR.entry[mshr_index].cpu << " instr_id: " << MSHR.entry[mshr_index].instr_id;
                       cout << " merged rob_index: " << i << " instr_id: N/A" << endl;
                     });
                }
              }
              else
              {
                uint32_t lq_index = RQ.entry[index].lq_index;
                MSHR.entry[mshr_index].is_data = 1; // add as data type
                MSHR.entry[mshr_index].load_merged = 1;
                MSHR.entry[mshr_index].lq_index_depend_on_me.insert(lq_index);

                DP(if (warmup_complete[read_cpu])
                   {
                     cout << "[DATA_MERGED] " << __func__ << " cpu: " << read_cpu << " instr_id: " << RQ.entry[index].instr_id;
                     cout << " merged rob_index: " << RQ.entry[index].rob_index << " instr_id: " << RQ.entry[index].instr_id << " lq_index: " << RQ.entry[index].lq_index << endl;
                   });
                MSHR.entry[mshr_index].lq_index_depend_on_me.join(RQ.entry[index].lq_index_depend_on_me, LQ_SIZE);
                if (RQ.entry[index].store_merged)
                {
                  MSHR.entry[mshr_index].store_merged = 1;
                  MSHR.entry[mshr_index].sq_index_depend_on_me.join(RQ.entry[index].sq_index_depend_on_me, SQ_SIZE);
                }
              }
            }

            // update fill_level
            if (RQ.entry[index].fill_level < MSHR.entry[mshr_index].fill_level)
              MSHR.entry[mshr_index].fill_level = RQ.entry[index].fill_level;

            if ((RQ.entry[index].fill_l1i) && (MSHR.entry[mshr_index].fill_l1i != 1))
            {
              MSHR.entry[mshr_index].fill_l1i = 1;
            }
            if ((RQ.entry[index].fill_l1d) && (MSHR.entry[mshr_index].fill_l1d != 1))
            {
              MSHR.entry[mshr_index].fill_l1d = 1;
            }

            // update request
            if (MSHR.entry[mshr_index].type == PREFETCH)
            {
              uint8_t prior_returned = MSHR.entry[mshr_index].returned;
              uint64_t prior_event_cycle = MSHR.entry[mshr_index].event_cycle;
              MSHR.entry[mshr_index] = RQ.entry[index];

              // in case request is already returned, we should keep event_cycle and retunred variables
              MSHR.entry[mshr_index].returned = prior_returned;
              MSHR.entry[mshr_index].event_cycle = prior_event_cycle;
            }

            MSHR_MERGED[RQ.entry[index].type]++;

            DP(if (warmup_complete[read_cpu])
               {
                 cout << "[" << NAME << "] " << __func__ << " mshr merged";
                 cout << " instr_id: " << RQ.entry[index].instr_id << " prior_id: " << MSHR.entry[mshr_index].instr_id;
                 cout << " address: " << hex << RQ.entry[index].address;
                 cout << " full_addr: " << RQ.entry[index].full_addr << dec;
                 cout << " cycle: " << RQ.entry[index].event_cycle << endl;
               });
          }
          else
          { // WE SHOULD NOT REACH HERE
            cerr << "[" << NAME << "] MSHR errors" << endl;
            assert(0);
          }
        }

        if (miss_handled)
        {
          // update prefetcher on load instruction
          if (RQ.entry[index].type == LOAD)
          {
            if (cache_type == IS_L1I)
              l1i_prefetcher_cache_operate(read_cpu, RQ.entry[index].ip, 0, 0);
            if (cache_type == IS_L1D)
              if (SUPPORT_VA){
                l1d_prefetcher_operate(RQ.entry[index].v_full_addr, RQ.entry[index].full_addr, RQ.entry[index].ip, 0, RQ.entry[index].type);
              } else {
                l1d_prefetcher_operate(RQ.entry[index].full_addr, RQ.entry[index].ip, 0, RQ.entry[index].type);
              }
            if (cache_type == IS_L2C)
#ifdef FULL_ADDR
              l2c_prefetcher_operate(RQ.entry[index].full_addr, RQ.entry[index].ip, 0, RQ.entry[index].type, 0);
#else
              l2c_prefetcher_operate(RQ.entry[index].address << LOG2_BLOCK_SIZE, RQ.entry[index].ip, 0, RQ.entry[index].type, 0);
#endif
            if (cache_type == IS_LLC)
            {
              cpu = read_cpu;
#ifdef FULL_ADDR
              llc_prefetcher_operate(RQ.entry[index].full_addr, RQ.entry[index].ip, 0, RQ.entry[index].type, 0);
#else
              llc_prefetcher_operate(RQ.entry[index].address << LOG2_BLOCK_SIZE, RQ.entry[index].ip, 0, RQ.entry[index].type, 0);
#endif
              cpu = 0;
            }
            // TODO: add codes for TLB prefetching
          }

          MISS[RQ.entry[index].type]++;
          ACCESS[RQ.entry[index].type]++;

          // remove this entry from RQ
          RQ.remove_queue(&RQ.entry[index]);
          reads_available_this_cycle--;
        }
      }
    }
    else
    {
      return;
    }

    if (reads_available_this_cycle == 0)
    {
      return;
    }
  }
}

void InfinityCACHE::handle_prefetch()
{
  // handle prefetch

  for (uint32_t i = 0; i < MAX_READ; i++)
  {

    uint32_t prefetch_cpu = PQ.entry[PQ.head].cpu;
    if (prefetch_cpu == NUM_CPUS)
      return;

    // handle the oldest entry
    if ((PQ.entry[PQ.head].event_cycle <= current_core_cycle[prefetch_cpu]) && (PQ.occupancy > 0))
    {
      int index = PQ.head;

      // access cache
      uint32_t set = this->get_set(PQ.entry[index].address);
      int way = check_hit(&PQ.entry[index]);
      uint64_t key = PQ.entry[index].address;

      if (way >= 0)
      { // prefetch hit

        // COLLECT STATS
        sim_hit[prefetch_cpu][PQ.entry[index].type]++;
        sim_access[prefetch_cpu][PQ.entry[index].type]++;
        // run prefetcher on prefetches from higher caches
        if (PQ.entry[index].pf_origin_level < fill_level)
        {
          if (cache_type == IS_L1D)
            if (SUPPORT_VA)
              l1d_prefetcher_operate(PQ.entry[index].v_full_addr, PQ.entry[index].full_addr, PQ.entry[index].ip, 1, PREFETCH);
            else
              l1d_prefetcher_operate(PQ.entry[index].full_addr, PQ.entry[index].ip, 1, PREFETCH);
          else if (cache_type == IS_L2C)
#ifdef FULL_ADDR
            PQ.entry[index].pf_metadata = l2c_prefetcher_operate(blocks[key].full_addr, PQ.entry[index].ip, 1, PREFETCH, PQ.entry[index].pf_metadata);
#else
            PQ.entry[index].pf_metadata = l2c_prefetcher_operate(blocks[key].address << LOG2_BLOCK_SIZE, PQ.entry[index].ip, 1, PREFETCH, PQ.entry[index].pf_metadata);
#endif
          else if (cache_type == IS_LLC)
          {
            cpu = prefetch_cpu;
#ifdef FULL_ADDR
            PQ.entry[index].pf_metadata = llc_prefetcher_operate(blocks[key].full_addr, PQ.entry[index].ip, 1, PREFETCH, PQ.entry[index].pf_metadata);
#else
            PQ.entry[index].pf_metadata = llc_prefetcher_operate(blocks[key].address << LOG2_BLOCK_SIZE, PQ.entry[index].ip, 1, PREFETCH, PQ.entry[index].pf_metadata);
#endif
            cpu = 0;
          }
        }

        // check fill level
        if (PQ.entry[index].fill_level < fill_level)
        {

          if (fill_level == FILL_L2)
          {
            if (PQ.entry[index].fill_l1i)
            {
              upper_level_icache[prefetch_cpu]->return_data(&PQ.entry[index]);
            }
            if (PQ.entry[index].fill_l1d)
            {
              upper_level_dcache[prefetch_cpu]->return_data(&PQ.entry[index]);
            }
          }
          else
          {
            if (PQ.entry[index].instruction)
              upper_level_icache[prefetch_cpu]->return_data(&PQ.entry[index]);
            if (PQ.entry[index].is_data)
              upper_level_dcache[prefetch_cpu]->return_data(&PQ.entry[index]);
          }
        }

        HIT[PQ.entry[index].type]++;
        ACCESS[PQ.entry[index].type]++;

        // remove this entry from PQ
        PQ.remove_queue(&PQ.entry[index]);
        reads_available_this_cycle--;
      }
      else
      { // prefetch miss

        DP(if (warmup_complete[prefetch_cpu])
           {
             cout << "[" << NAME << "] " << __func__ << " prefetch miss";
             cout << " instr_id: " << PQ.entry[index].instr_id << " address: " << hex << PQ.entry[index].address;
             cout << " full_addr: " << PQ.entry[index].full_addr << dec << " fill_level: " << PQ.entry[index].fill_level;
             cout << " cycle: " << PQ.entry[index].event_cycle << endl;
           });

        // check mshr
        uint8_t miss_handled = 1;
        int mshr_index = check_mshr(&PQ.entry[index]);

        if (mshr_index == -2)
        {
          // this is a data/instruction collision in the MSHR, so we have to wait before we can allocate this miss
          miss_handled = 0;
        }
        else if ((mshr_index == -1) && (MSHR.occupancy < MSHR_SIZE))
        { // this is a new miss

          DP(if (warmup_complete[PQ.entry[index].cpu])
             {
               cout << "[" << NAME << "_PQ] " << __func__ << " want to add instr_id: " << PQ.entry[index].instr_id << " address: " << hex << PQ.entry[index].address;
               cout << " full_addr: " << PQ.entry[index].full_addr << dec;
               cout << " occupancy: " << lower_level->get_occupancy(3, PQ.entry[index].address) << " SIZE: " << lower_level->get_size(3, PQ.entry[index].address) << endl;
             });

          // first check if the lower level PQ is full or not
          // this is possible since multiple prefetchers can exist at each level of caches
          if (lower_level)
          {
            if (cache_type == IS_LLC)
            {
              if (lower_level->get_occupancy(1, PQ.entry[index].address) == lower_level->get_size(1, PQ.entry[index].address))
                miss_handled = 0;
              else
              {

                // run prefetcher on prefetches from higher caches
                if (PQ.entry[index].pf_origin_level < fill_level)
                {
                  if (cache_type == IS_LLC)
                  {
                    cpu = prefetch_cpu;
#ifdef FULL_ADDR
                    PQ.entry[index].pf_metadata = llc_prefetcher_operate(PQ.entry[index].full_addr, PQ.entry[index].ip, 0, PREFETCH, PQ.entry[index].pf_metadata);
#else
                    PQ.entry[index].pf_metadata = llc_prefetcher_operate(PQ.entry[index].address << LOG2_BLOCK_SIZE, PQ.entry[index].ip, 0, PREFETCH, PQ.entry[index].pf_metadata);
#endif
                    cpu = 0;
                  }
                }

                // add it to MSHRs if this prefetch miss will be filled to this cache level
                if (PQ.entry[index].fill_level <= fill_level)
                  add_mshr(&PQ.entry[index]);

                lower_level->add_rq(&PQ.entry[index]); // add it to the DRAM RQ
              }
            }
            else
            {
              if (lower_level->get_occupancy(3, PQ.entry[index].address) == lower_level->get_size(3, PQ.entry[index].address))
                miss_handled = 0;
              else
              {

                // run prefetcher on prefetches from higher caches
                if (PQ.entry[index].pf_origin_level < fill_level)
                {
                  if (cache_type == IS_L1D)
                    if (SUPPORT_VA)
                      l1d_prefetcher_operate(PQ.entry[index].v_full_addr, PQ.entry[index].full_addr, PQ.entry[index].ip, 0, PREFETCH);
                    else
                      l1d_prefetcher_operate(PQ.entry[index].full_addr, PQ.entry[index].ip, 0, PREFETCH);
                  if (cache_type == IS_L2C)
#ifdef FULL_ADDR
                    PQ.entry[index].pf_metadata = l2c_prefetcher_operate(PQ.entry[index].full_addr, PQ.entry[index].ip, 0, PREFETCH, PQ.entry[index].pf_metadata);
#else
                    PQ.entry[index].pf_metadata = l2c_prefetcher_operate(PQ.entry[index].address << LOG2_BLOCK_SIZE, PQ.entry[index].ip, 0, PREFETCH, PQ.entry[index].pf_metadata);
#endif
                }

                // add it to MSHRs if this prefetch miss will be filled to this cache level
                if (PQ.entry[index].fill_level <= fill_level)
                  add_mshr(&PQ.entry[index]);

                lower_level->add_pq(&PQ.entry[index]); // add it to the lower level's PQ
              }
            }
          }
        }
        else
        {
          if ((mshr_index == -1) && (MSHR.occupancy == MSHR_SIZE))
          { // not enough MSHR resource

            // TODO: should we allow prefetching with lower fill level at this case?

            // cannot handle miss request until one of MSHRs is available
            miss_handled = 0;
            STALL[PQ.entry[index].type]++;
          }
          else if (mshr_index != -1)
          { // already in-flight miss
            // int type = EXTRACT_TYPE(PQ.entry[index].pf_metadata);
            if (!MSHR.entry[mshr_index].prefetched) {
#ifdef MATRYOSHKA
              int type = EXTRACT_TYPE(PQ.entry[index].pf_metadata);
              pref_late[cpu][type]++;
#else
              int trigger_offset = (PQ.entry[index].pf_metadata);
              pref_late[cpu][trigger_offset]++;
#endif
              pf_late++;
            }

            // no need to update request except fill_level
            // update fill_level
            if (PQ.entry[index].fill_level < MSHR.entry[mshr_index].fill_level)
              MSHR.entry[mshr_index].fill_level = PQ.entry[index].fill_level;

            if ((PQ.entry[index].fill_l1i) && (MSHR.entry[mshr_index].fill_l1i != 1))
            {
              MSHR.entry[mshr_index].fill_l1i = 1;
            }
            if ((PQ.entry[index].fill_l1d) && (MSHR.entry[mshr_index].fill_l1d != 1))
            {
              MSHR.entry[mshr_index].fill_l1d = 1;
            }

            MSHR_MERGED[PQ.entry[index].type]++;

            DP(if (warmup_complete[prefetch_cpu])
               {
                 cout << "[" << NAME << "] " << __func__ << " mshr merged";
                 cout << " instr_id: " << PQ.entry[index].instr_id << " prior_id: " << MSHR.entry[mshr_index].instr_id;
                 cout << " address: " << hex << PQ.entry[index].address;
                 cout << " full_addr: " << PQ.entry[index].full_addr << dec << " fill_level: " << MSHR.entry[mshr_index].fill_level;
                 cout << " cycle: " << MSHR.entry[mshr_index].event_cycle << endl;
               });
          }
          else
          { // WE SHOULD NOT REACH HERE
            cerr << "[" << NAME << "] MSHR errors" << endl;
            assert(0);
          }
        }

        if (miss_handled)
        {

          DP(if (warmup_complete[prefetch_cpu])
             {
               cout << "[" << NAME << "] " << __func__ << " prefetch miss handled";
               cout << " instr_id: " << PQ.entry[index].instr_id << " address: " << hex << PQ.entry[index].address;
               cout << " full_addr: " << PQ.entry[index].full_addr << dec << " fill_level: " << PQ.entry[index].fill_level;
               cout << " cycle: " << PQ.entry[index].event_cycle << endl;
             });

          MISS[PQ.entry[index].type]++;
          ACCESS[PQ.entry[index].type]++;

          // remove this entry from PQ
          PQ.remove_queue(&PQ.entry[index]);
          reads_available_this_cycle--;
        }
      }
    }
    else
    {
      return;
    }

    if (reads_available_this_cycle == 0)
    {
      return;
    }
  }
}

void CACHE::handle_prefetch()
{
  // handle prefetch

  for (uint32_t i = 0; i < MAX_READ; i++)
  {

    uint32_t prefetch_cpu = PQ.entry[PQ.head].cpu;
    if (prefetch_cpu == NUM_CPUS)
      return;

    // handle the oldest entry
    if ((PQ.entry[PQ.head].event_cycle <= current_core_cycle[prefetch_cpu]) && (PQ.occupancy > 0))
    {
      int index = PQ.head;

      // access cache
      uint32_t set = this->get_set(PQ.entry[index].address);
      int way = check_hit(&PQ.entry[index]);

      if (way >= 0)
      { // prefetch hit

#ifdef PREFETCH_AFFECT_REPL
        // update replacement policy
        // TODO: we can add method to support customized replacement policies for TLB
        if (cache_type == IS_LLC)
        {
          llc_update_replacement_state(prefetch_cpu, set, way, block[set][way].full_addr, PQ.entry[index].ip, 0, PQ.entry[index].type, 1);
        }
        else
          update_replacement_state(prefetch_cpu, set, way, block[set][way].full_addr, PQ.entry[index].ip, 0, PQ.entry[index].type, 1);
#endif

        // COLLECT STATS
        sim_hit[prefetch_cpu][PQ.entry[index].type]++;
        sim_access[prefetch_cpu][PQ.entry[index].type]++;

        if(l1d_prefetcher_prefetch_hit && cache_type == IS_L1D)
        {
            l1d_prefetcher_prefetch_hit(cpu, block[set][way].address<<LOG2_BLOCK_SIZE, PQ.entry[index].ip, PQ.entry[index].pf_metadata);
        }
        else if(l2c_prefetcher_prefetch_hit && cache_type == IS_L2C)
        {
            l2c_prefetcher_prefetch_hit(cpu, block[set][way].address<<LOG2_BLOCK_SIZE, PQ.entry[index].ip, PQ.entry[index].pf_metadata);
        }
        else if(llc_prefetcher_prefetch_hit && cache_type == IS_LLC)
        {
            llc_prefetcher_prefetch_hit(cpu, block[set][way].address<<LOG2_BLOCK_SIZE, PQ.entry[index].ip, PQ.entry[index].pf_metadata);
        }

        // run prefetcher on prefetches from higher caches
        if (PQ.entry[index].pf_origin_level < fill_level)
        {
          if (cache_type == IS_L1D)
            if (SUPPORT_VA)
              l1d_prefetcher_operate(PQ.entry[index].v_full_addr, PQ.entry[index].full_addr, PQ.entry[index].ip, 1, PREFETCH);
            else
              l1d_prefetcher_operate(PQ.entry[index].full_addr, PQ.entry[index].ip, 1, PREFETCH);
          else if (cache_type == IS_L2C)
#ifdef FULL_ADDR
            PQ.entry[index].pf_metadata = l2c_prefetcher_operate(block[set][way].full_addr, PQ.entry[index].ip, 1, PREFETCH, PQ.entry[index].pf_metadata);
#else
            PQ.entry[index].pf_metadata = l2c_prefetcher_operate(block[set][way].address << LOG2_BLOCK_SIZE, PQ.entry[index].ip, 1, PREFETCH, PQ.entry[index].pf_metadata);
#endif
          else if (cache_type == IS_LLC)
          {
            cpu = prefetch_cpu;
#ifdef FULL_ADDR
            PQ.entry[index].pf_metadata = llc_prefetcher_operate(block[set][way].full_addr, PQ.entry[index].ip, 1, PREFETCH, PQ.entry[index].pf_metadata);
#else
            PQ.entry[index].pf_metadata = llc_prefetcher_operate(block[set][way].address << LOG2_BLOCK_SIZE, PQ.entry[index].ip, 1, PREFETCH, PQ.entry[index].pf_metadata);
#endif
            cpu = 0;
          }
          // TODO: add prefetch methods for TLB
        }

        // check fill level
        if (PQ.entry[index].fill_level < fill_level)
        {

          if (fill_level == FILL_L2)
          {
            if (PQ.entry[index].fill_l1i)
            {
              upper_level_icache[prefetch_cpu]->return_data(&PQ.entry[index]);
            }
            if (PQ.entry[index].fill_l1d)
            {
              upper_level_dcache[prefetch_cpu]->return_data(&PQ.entry[index]);
            }
          }
          else
          {
            if (PQ.entry[index].instruction)
              upper_level_icache[prefetch_cpu]->return_data(&PQ.entry[index]);
            if (PQ.entry[index].is_data)
              upper_level_dcache[prefetch_cpu]->return_data(&PQ.entry[index]);
          }
        }

        HIT[PQ.entry[index].type]++;
        ACCESS[PQ.entry[index].type]++;

        // remove this entry from PQ
        PQ.remove_queue(&PQ.entry[index]);
        reads_available_this_cycle--;
      }
      else
      { // prefetch miss

        DP(if (warmup_complete[prefetch_cpu])
           {
             cout << "[" << NAME << "] " << __func__ << " prefetch miss";
             cout << " instr_id: " << PQ.entry[index].instr_id << " address: " << hex << PQ.entry[index].address;
             cout << " full_addr: " << PQ.entry[index].full_addr << dec << " fill_level: " << PQ.entry[index].fill_level;
             cout << " cycle: " << PQ.entry[index].event_cycle << endl;
           });

        // check mshr
        uint8_t miss_handled = 1;
        int mshr_index = check_mshr(&PQ.entry[index]);

        if (mshr_index == -2)
        {
          // this is a data/instruction collision in the MSHR, so we have to wait before we can allocate this miss
          miss_handled = 0;
        }
        else if ((mshr_index == -1) && (MSHR.occupancy < MSHR_SIZE))
        { // this is a new miss

          DP(if (warmup_complete[PQ.entry[index].cpu])
             {
               cout << "[" << NAME << "_PQ] " << __func__ << " want to add instr_id: " << PQ.entry[index].instr_id << " address: " << hex << PQ.entry[index].address;
               cout << " full_addr: " << PQ.entry[index].full_addr << dec;
               cout << " occupancy: " << lower_level->get_occupancy(3, PQ.entry[index].address) << " SIZE: " << lower_level->get_size(3, PQ.entry[index].address) << endl;
             });

          // first check if the lower level PQ is full or not
          // this is possible since multiple prefetchers can exist at each level of caches
          if (lower_level)
          {
            if (cache_type == IS_LLC)
            {
              if (lower_level->get_occupancy(1, PQ.entry[index].address) == lower_level->get_size(1, PQ.entry[index].address))
                miss_handled = 0;
              else
              {

                // run prefetcher on prefetches from higher caches
                if (PQ.entry[index].pf_origin_level < fill_level)
                {
                  cpu = prefetch_cpu;
#ifdef FULL_ADDR
                  PQ.entry[index].pf_metadata = llc_prefetcher_operate(PQ.entry[index].full_addr, 
                                                                       PQ.entry[index].ip, 0, PREFETCH, PQ.entry[index].pf_metadata);
#else
                  PQ.entry[index].pf_metadata = llc_prefetcher_operate(PQ.entry[index].address << LOG2_BLOCK_SIZE, 
                                                                       PQ.entry[index].ip, 0, PREFETCH, PQ.entry[index].pf_metadata);
#endif
                  cpu = 0;
                }

                // add it to MSHRs if this prefetch miss will be filled to this cache level
                if (PQ.entry[index].fill_level <= fill_level)
                  add_mshr(&PQ.entry[index]);

                lower_level->add_rq(&PQ.entry[index]); // add it to the DRAM RQ
              }
            }
            else
            {
              if (lower_level->get_occupancy(3, PQ.entry[index].address) == lower_level->get_size(3, PQ.entry[index].address))
                miss_handled = 0;
              else
              {

                // run prefetcher on prefetches from higher caches
                if (PQ.entry[index].pf_origin_level < fill_level)
                {
                  if (cache_type == IS_L1D)
                    if (SUPPORT_VA)
                      l1d_prefetcher_operate(PQ.entry[index].v_full_addr, PQ.entry[index].full_addr, PQ.entry[index].ip, 0, PREFETCH);
                    else
                      l1d_prefetcher_operate(PQ.entry[index].full_addr, PQ.entry[index].ip, 0, PREFETCH);
                  if (cache_type == IS_L2C)
#ifdef FULL_ADDR
                    PQ.entry[index].pf_metadata = l2c_prefetcher_operate(PQ.entry[index].full_addr, PQ.entry[index].ip, 0, PREFETCH, PQ.entry[index].pf_metadata);
#else
                    PQ.entry[index].pf_metadata = l2c_prefetcher_operate(PQ.entry[index].address << LOG2_BLOCK_SIZE, PQ.entry[index].ip, 0, PREFETCH, PQ.entry[index].pf_metadata);
#endif
                  // TODO: add logic for TLB
                }

                // add it to MSHRs if this prefetch miss will be filled to this cache level
                if (PQ.entry[index].fill_level <= fill_level)
                  add_mshr(&PQ.entry[index]);

                lower_level->add_pq(&PQ.entry[index]); // add it to the lower level's PQ
              }
            }
          }
        }
        else
        {
          if ((mshr_index == -1) && (MSHR.occupancy == MSHR_SIZE))
          { // not enough MSHR resource

            // TODO: should we allow prefetching with lower fill level at this case?

            // cannot handle miss request until one of MSHRs is available
            miss_handled = 0;
            STALL[PQ.entry[index].type]++;
          }
          else if (mshr_index != -1)
          { // already in-flight miss
            if (!MSHR.entry[mshr_index].prefetched) {
#ifdef MATRYOSHKA
              int type = EXTRACT_TYPE(PQ.entry[index].pf_metadata);
              pref_late[cpu][type]++;
#else
              int trigger_offset = (PQ.entry[index].pf_metadata);
              pref_late[cpu][trigger_offset]++;
              pf_late++;
#endif
            }

            // no need to update request except fill_level
            // update fill_level
            if (PQ.entry[index].fill_level < MSHR.entry[mshr_index].fill_level)
              MSHR.entry[mshr_index].fill_level = PQ.entry[index].fill_level;

            if ((PQ.entry[index].fill_l1i) && (MSHR.entry[mshr_index].fill_l1i != 1))
            {
              MSHR.entry[mshr_index].fill_l1i = 1;
            }
            if ((PQ.entry[index].fill_l1d) && (MSHR.entry[mshr_index].fill_l1d != 1))
            {
              MSHR.entry[mshr_index].fill_l1d = 1;
            }

            MSHR_MERGED[PQ.entry[index].type]++;

            DP(if (warmup_complete[prefetch_cpu])
               {
                 cout << "[" << NAME << "] " << __func__ << " mshr merged";
                 cout << " instr_id: " << PQ.entry[index].instr_id << " prior_id: " << MSHR.entry[mshr_index].instr_id;
                 cout << " address: " << hex << PQ.entry[index].address;
                 cout << " full_addr: " << PQ.entry[index].full_addr << dec << " fill_level: " << MSHR.entry[mshr_index].fill_level;
                 cout << " cycle: " << MSHR.entry[mshr_index].event_cycle << endl;
               });
          }
          else
          { // WE SHOULD NOT REACH HERE
            cerr << "[" << NAME << "] MSHR errors" << endl;
            assert(0);
          }
        }

        if (miss_handled)
        {

          DP(if (warmup_complete[prefetch_cpu])
             {
               cout << "[" << NAME << "] " << __func__ << " prefetch miss handled";
               cout << " instr_id: " << PQ.entry[index].instr_id << " address: " << hex << PQ.entry[index].address;
               cout << " full_addr: " << PQ.entry[index].full_addr << dec << " fill_level: " << PQ.entry[index].fill_level;
               cout << " cycle: " << PQ.entry[index].event_cycle << endl;
             });

          MISS[PQ.entry[index].type]++;
          ACCESS[PQ.entry[index].type]++;

          // remove this entry from PQ
          PQ.remove_queue(&PQ.entry[index]);
          reads_available_this_cycle--;
        }
      }
    }
    else
    {
      return;
    }

    if (reads_available_this_cycle == 0)
    {
      return;
    }
  }
}

void CACHE::operate()
{
  this->handle_fill();
  this->handle_writeback();
  this->reads_available_this_cycle = MAX_READ;
  this->handle_read();

  if (PQ.occupancy && (reads_available_this_cycle > 0))
    this->handle_prefetch();
  
  handle_prefetch_feedback();
}

uint32_t CACHE::get_set(uint64_t address)
{
  return (uint32_t)(address & ((1 << lg2(NUM_SET)) - 1));
}

// uint32_t CACHE1::get_set(uint64_t address)
// {
//   return (uint32_t)((address >> LOG2_BLOCK_SIZE) & ((1 << lg2(NUM_SET)) - 1));
// }

uint32_t CACHE::get_way(uint64_t address, uint32_t set)
{
  for (uint32_t way = 0; way < NUM_WAY; way++)
  {
    if (block[set][way].valid && (block[set][way].tag == address))
      return way;
  }

  return NUM_WAY;
}

void InfinityCACHE::fill_cache(PACKET *packet)
{
#ifdef SANITY_CHECK
  if (cache_type == IS_ITLB)
  {
    if (packet->data == 0)
      assert(0);
  }

  if (cache_type == IS_DTLB)
  {
    if (packet->data == 0)
      assert(0);
  }

  if (cache_type == IS_STLB)
  {
    if (packet->data == 0)
      assert(0);
  }
#endif
  uint64_t key = packet->address;
  if (blocks[key].prefetch && (blocks[key].used == 0)) {
    pf_useless++;
  }

  if (blocks[key].valid == 0)
    blocks[key].valid = 1;
  blocks[key].dirty = 0;
  blocks[key].prefetch = (packet->type == PREFETCH) ? 1 : 0;
  blocks[key].used = 0;

  if (blocks[key].prefetch)
  {
    pf_fill++;
    /**
     * @brief extract the prefetch type from metadata
     *        only work for ipcp 2020
     */
#ifdef MATRYOSHKA
    int type = EXTRACT_TYPE(packet->pf_metadata);
    pref_filled[cpu][type]++;
#else
    int trigger_offset = (blocks[key].pf_metadata);
    pref_filled[cpu][trigger_offset]++;
#endif
  }

  blocks[key].delta = packet->delta;
  blocks[key].depth = packet->depth;
  blocks[key].signature = packet->signature;
  blocks[key].confidence = packet->confidence;

  blocks[key].tag = packet->address;
  blocks[key].address = packet->address;
  blocks[key].full_addr = packet->full_addr;
  blocks[key].data = packet->data;
  blocks[key].ip = packet->ip;
  blocks[key].cpu = packet->cpu;
  blocks[key].instr_id = packet->instr_id;
  blocks[key].pf_metadata = packet->pf_metadata;

  // DP(if (warmup_complete[packet->cpu]) {
  //   cout << "[" << NAME << "] " << __func__ << " set: " << set << " way: " << way;
  //   cout << " lru: " << block[set][way].lru << " tag: " << hex << block[set][way].tag << " full_addr: " << block[set][way].full_addr;
  //   cout << " data: " << block[set][way].data << dec << endl; });
}
void CACHE::fill_cache(uint32_t set, uint32_t way, PACKET *packet)
{
#ifdef SANITY_CHECK
  if (cache_type == IS_ITLB)
  {
    if (packet->data == 0)
      assert(0);
  }

  if (cache_type == IS_DTLB)
  {
    if (packet->data == 0)
      assert(0);
  }

  if (cache_type == IS_STLB)
  {
    if (packet->data == 0)
      assert(0);
  }
#endif
  if (block[set][way].prefetch && (block[set][way].used == 0)) {

#ifdef RECORD_INFO_OUTPUT
uint64_t address = block[set][way].address;
g_fout.fout << pf_addr_scores[address].pattern << "," << block[set][way].full_addr << "," << pf_addr_scores[address].pc << "," << pf_addr_scores[address].trigger_offset << ","
<< pf_addr_scores[address].score << "," << pf_addr_scores[address].cache_hit << ","<< 0 << endl;
#endif

#ifdef ANALYZE
    if (cache_type == IS_L1D) {
      g_fout.fout << block[set][way].full_addr << ",0" << endl;
    }
#endif
    pf_useless++;
  }

  if (block[set][way].valid == 0)
    block[set][way].valid = 1;
  block[set][way].dirty = 0;
  block[set][way].prefetch = (packet->type == PREFETCH) ? (packet->pf_metadata != 0 ? packet->pf_metadata : 1) : 0;
  block[set][way].used = 0;

  if (block[set][way].prefetch)
  {
    pf_fill++;
    pf_filled_epoch++;
    /**
     * @brief extract the prefetch type from metadata
     *        only work for ipcp 2020
     */
#ifdef MATRYOHSKA
    int type = EXTRACT_TYPE(packet->pf_metadata);
    pref_filled[cpu][type]++;
#else
    int trigger_offset = packet->pf_metadata;
    pref_filled[cpu][trigger_offset]++;
#endif
  }

  block[set][way].delta = packet->delta;
  block[set][way].depth = packet->depth;
  block[set][way].signature = packet->signature;
  block[set][way].confidence = packet->confidence;

  block[set][way].tag = packet->address;
  block[set][way].address = packet->address;
  block[set][way].full_addr = packet->full_addr;
  block[set][way].data = packet->data;
  block[set][way].ip = packet->ip;
  block[set][way].cpu = packet->cpu;
  block[set][way].instr_id = packet->instr_id;
  block[set][way].pf_metadata = packet->pf_metadata;

  DP(if (warmup_complete[packet->cpu])
     {
       cout << "[" << NAME << "] " << __func__ << " set: " << set << " way: " << way;
       cout << " lru: " << block[set][way].lru << " tag: " << hex << block[set][way].tag << " full_addr: " << block[set][way].full_addr;
       cout << " data: " << block[set][way].data << dec << endl;
     });
}

int CACHE::check_hit(PACKET *packet)
{
  uint32_t set = this->get_set(packet->address);
  int match_way = -1;

  if (NUM_SET < set)
  {
    cerr << "[" << NAME << "_ERROR] " << __func__ << " invalid set index: " << set << " NUM_SET: " << NUM_SET;
    cerr << " address: " << hex << packet->address << " full_addr: " << packet->full_addr << dec;
    cerr << " event: " << packet->event_cycle << endl;
    assert(0);
  }

  // hit
  for (uint32_t way = 0; way < NUM_WAY; way++)
  {
    if (block[set][way].valid && (block[set][way].tag == packet->address))
    {

      match_way = way;

      DP(if (warmup_complete[packet->cpu])
         {
           cout << "[" << NAME << "] " << __func__ << " instr_id: " << packet->instr_id << " type: " << +packet->type << hex << " addr: " << packet->address;
           cout << " full_addr: " << packet->full_addr << " tag: " << block[set][way].tag << " data: " << block[set][way].data << dec;
           cout << " set: " << set << " way: " << way << " lru: " << block[set][way].lru;
           cout << " event: " << packet->event_cycle << " cycle: " << current_core_cycle[cpu] << endl;
         });

      break;
    }
  }

  return match_way;
}

int CACHE::invalidate_entry(uint64_t inval_addr)
{
  uint32_t set = this->get_set(inval_addr);
  int match_way = -1;

  if (NUM_SET < set)
  {
    cerr << "[" << NAME << "_ERROR] " << __func__ << " invalid set index: " << set << " NUM_SET: " << NUM_SET;
    cerr << " inval_addr: " << hex << inval_addr << dec << endl;
    assert(0);
  }

  // invalidate
  for (uint32_t way = 0; way < NUM_WAY; way++)
  {
    if (block[set][way].valid && (block[set][way].tag == inval_addr))
    {

      block[set][way].valid = 0;

      match_way = way;

      DP(if (warmup_complete[cpu])
         {
           cout << "[" << NAME << "] " << __func__ << " inval_addr: " << hex << inval_addr;
           cout << " tag: " << block[set][way].tag << " data: " << block[set][way].data << dec;
           cout << " set: " << set << " way: " << way << " lru: " << block[set][way].lru << " cycle: " << current_core_cycle[cpu] << endl;
         });

      break;
    }
  }

  return match_way;
}

int CACHE::add_rq(PACKET *packet)
{
  // check for the latest wirtebacks in the write queue
  int wq_index = WQ.check_queue(packet);
  if (wq_index != -1)
  {

    // check fill level
    if (packet->fill_level < fill_level)
    {

      packet->data = WQ.entry[wq_index].data;

      if (fill_level == FILL_L2)
      {
        if (packet->fill_l1i)
        {
          upper_level_icache[packet->cpu]->return_data(packet);
        }
        if (packet->fill_l1d)
        {
          upper_level_dcache[packet->cpu]->return_data(packet);
        }
      }
      else
      {
        if (packet->instruction)
          upper_level_icache[packet->cpu]->return_data(packet);
        if (packet->is_data)
          upper_level_dcache[packet->cpu]->return_data(packet);
      }
    }

#ifdef SANITY_CHECK
    if (cache_type == IS_ITLB)
      assert(0);
    else if (cache_type == IS_DTLB)
      assert(0);
    else if (cache_type == IS_L1I)
      assert(0);
#endif
    // update processed packets
    if ((cache_type == IS_L1D) && (packet->type != PREFETCH))
    {
      if (PROCESSED.occupancy < PROCESSED.SIZE)
        PROCESSED.add_queue(packet);

      DP(if (warmup_complete[packet->cpu])
         {
           cout << "[" << NAME << "_RQ] " << __func__ << " instr_id: " << packet->instr_id << " found recent writebacks";
           cout << hex << " read: " << packet->address << " writeback: " << WQ.entry[wq_index].address << dec;
           cout << " index: " << MAX_READ << " rob_signal: " << packet->rob_signal << endl;
         });
    }

    HIT[packet->type]++;
    ACCESS[packet->type]++;

    WQ.FORWARD++;
    RQ.ACCESS++;

    return -1;
  }

  // check for duplicates in the read queue
  int index = RQ.check_queue(packet);
  if (index != -1)
  {

    if (packet->instruction)
    {
      uint32_t rob_index = packet->rob_index;
      RQ.entry[index].rob_index_depend_on_me.insert(rob_index);
      RQ.entry[index].instruction = 1; // add as instruction type
      RQ.entry[index].instr_merged = 1;

      DP(if (warmup_complete[packet->cpu])
         {
           cout << "[INSTR_MERGED] " << __func__ << " cpu: " << packet->cpu << " instr_id: " << RQ.entry[index].instr_id;
           cout << " merged rob_index: " << rob_index << " instr_id: " << packet->instr_id << endl;
         });
    }
    else
    {
      // mark merged consumer
      if (packet->type == RFO)
      {

        uint32_t sq_index = packet->sq_index;
        RQ.entry[index].sq_index_depend_on_me.insert(sq_index);
        RQ.entry[index].store_merged = 1;
      }
      else
      {
        uint32_t lq_index = packet->lq_index;
        RQ.entry[index].lq_index_depend_on_me.insert(lq_index);
        RQ.entry[index].load_merged = 1;

        DP(if (warmup_complete[packet->cpu])
           {
             cout << "[DATA_MERGED] " << __func__ << " cpu: " << packet->cpu << " instr_id: " << RQ.entry[index].instr_id;
             cout << " merged rob_index: " << packet->rob_index << " instr_id: " << packet->instr_id << " lq_index: " << packet->lq_index << endl;
           });
      }
      RQ.entry[index].is_data = 1; // add as data type
    }

    if ((packet->fill_l1i) && (RQ.entry[index].fill_l1i != 1))
    {
      RQ.entry[index].fill_l1i = 1;
    }
    if ((packet->fill_l1d) && (RQ.entry[index].fill_l1d != 1))
    {
      RQ.entry[index].fill_l1d = 1;
    }

    RQ.MERGED++;
    RQ.ACCESS++;

    return index; // merged index
  }

  // check occupancy
  if (RQ.occupancy == RQ_SIZE)
  {
    RQ.FULL++;

    return -2; // cannot handle this request
  }

  // if there is no duplicate, add it to RQ
  index = RQ.tail;

#ifdef SANITY_CHECK
  if (RQ.entry[index].address != 0)
  {
    cerr << "[" << NAME << "_ERROR] " << __func__ << " is not empty index: " << index;
    cerr << " address: " << hex << RQ.entry[index].address;
    cerr << " full_addr: " << RQ.entry[index].full_addr << dec << endl;
    assert(0);
  }
#endif

  RQ.entry[index] = *packet;

  // ADD LATENCY
  if (RQ.entry[index].event_cycle < current_core_cycle[packet->cpu])
    RQ.entry[index].event_cycle = current_core_cycle[packet->cpu] + LATENCY;
  else
    RQ.entry[index].event_cycle += LATENCY;

  RQ.occupancy++;
  RQ.tail++;
  if (RQ.tail >= RQ.SIZE)
    RQ.tail = 0;

  DP(if (warmup_complete[RQ.entry[index].cpu])
     {
       cout << "[" << NAME << "_RQ] " << __func__ << " instr_id: " << RQ.entry[index].instr_id << " address: " << hex << RQ.entry[index].address;
       cout << " full_addr: " << RQ.entry[index].full_addr << dec;
       cout << " type: " << +RQ.entry[index].type << " head: " << RQ.head << " tail: " << RQ.tail << " occupancy: " << RQ.occupancy;
       cout << " event: " << RQ.entry[index].event_cycle << " current: " << current_core_cycle[RQ.entry[index].cpu] << endl;
     });

  if (packet->address == 0)
    assert(0);

  RQ.TO_CACHE++;
  RQ.ACCESS++;

  return -1;
}

int CACHE::add_wq(PACKET *packet)
{
  // check for duplicates in the write queue
  int index = WQ.check_queue(packet);
  if (index != -1)
  {

    WQ.MERGED++;
    WQ.ACCESS++;

    return index; // merged index
  }

  // sanity check
  if (WQ.occupancy >= WQ.SIZE)
    assert(0);

  // if there is no duplicate, add it to the write queue
  index = WQ.tail;
  if (WQ.entry[index].address != 0)
  {
    cerr << "[" << NAME << "_ERROR] " << __func__ << " is not empty index: " << index;
    cerr << " address: " << hex << WQ.entry[index].address;
    cerr << " full_addr: " << WQ.entry[index].full_addr << dec << endl;
    assert(0);
  }

  WQ.entry[index] = *packet;

  // ADD LATENCY
  if (WQ.entry[index].event_cycle < current_core_cycle[packet->cpu])
    WQ.entry[index].event_cycle = current_core_cycle[packet->cpu] + LATENCY;
  else
    WQ.entry[index].event_cycle += LATENCY;

  WQ.occupancy++;
  WQ.tail++;
  if (WQ.tail >= WQ.SIZE)
    WQ.tail = 0;

  DP(if (warmup_complete[WQ.entry[index].cpu])
     {
       cout << "[" << NAME << "_WQ] " << __func__ << " instr_id: " << WQ.entry[index].instr_id << " address: " << hex << WQ.entry[index].address;
       cout << " full_addr: " << WQ.entry[index].full_addr << dec;
       cout << " head: " << WQ.head << " tail: " << WQ.tail << " occupancy: " << WQ.occupancy;
       cout << " data: " << hex << WQ.entry[index].data << dec;
       cout << " event: " << WQ.entry[index].event_cycle << " current: " << current_core_cycle[WQ.entry[index].cpu] << endl;
     });

  WQ.TO_CACHE++;
  WQ.ACCESS++;

  return -1;
}

int CACHE::prefetch_line(uint64_t ip, uint64_t base_addr, uint64_t pf_addr, int pf_fill_level, uint32_t prefetch_metadata)
{
  pf_requested++;

  if (PQ.occupancy < PQ.SIZE)
  {
    if ((base_addr >> LOG2_PAGE_SIZE) == (pf_addr >> LOG2_PAGE_SIZE))
    {
      PACKET pf_packet;
      pf_packet.fill_level = pf_fill_level;
      pf_packet.pf_origin_level = fill_level;
      if (pf_fill_level == FILL_L1)
      {
        pf_packet.fill_l1d = 1;
      }
      pf_packet.pf_metadata = prefetch_metadata;
      pf_packet.cpu = cpu;
      //pf_packet.data_index = LQ.entry[lq_index].data_index;
      //pf_packet.lq_index = lq_index;
      pf_packet.address = pf_addr >> LOG2_BLOCK_SIZE;
      pf_packet.full_addr = pf_addr;
      //pf_packet.instr_id = LQ.entry[lq_index].instr_id;
      //pf_packet.rob_index = LQ.entry[lq_index].rob_index;
      pf_packet.ip = ip;
      pf_packet.type = PREFETCH;
      pf_packet.event_cycle = current_core_cycle[cpu];
      pf_packet.prefetched = 1;
      // give a dummy 0 as the IP of a prefetch
      add_pq(&pf_packet);

      pf_issued++;

      return 1;
    }
  }

  return 0;
}

int CACHE::kpc_prefetch_line(uint64_t base_addr, uint64_t pf_addr, int pf_fill_level, int delta, int depth, int signature, int confidence, uint32_t prefetch_metadata)
{
  if (PQ.occupancy < PQ.SIZE)
  {
    if ((base_addr >> LOG2_PAGE_SIZE) == (pf_addr >> LOG2_PAGE_SIZE))
    {

      PACKET pf_packet;
      pf_packet.fill_level = pf_fill_level;
      pf_packet.pf_origin_level = fill_level;
      if (pf_fill_level == FILL_L1)
      {
        pf_packet.fill_l1d = 1;
      }
      pf_packet.pf_metadata = prefetch_metadata;
      pf_packet.cpu = cpu;
      //pf_packet.data_index = LQ.entry[lq_index].data_index;
      //pf_packet.lq_index = lq_index;
      pf_packet.address = pf_addr >> LOG2_BLOCK_SIZE;
      pf_packet.full_addr = pf_addr;
      //pf_packet.instr_id = LQ.entry[lq_index].instr_id;
      //pf_packet.rob_index = LQ.entry[lq_index].rob_index;
      pf_packet.ip = 0;
      pf_packet.type = PREFETCH;
      pf_packet.delta = delta;
      pf_packet.depth = depth;
      pf_packet.signature = signature;
      pf_packet.confidence = confidence;
      pf_packet.event_cycle = current_core_cycle[cpu];

      // give a dummy 0 as the IP of a prefetch
      add_pq(&pf_packet);

      pf_issued++;

      return 1;
    }
  }

  return 0;
}

int CACHE::add_pq(PACKET *packet)
{
  // check for the latest wirtebacks in the write queue
  int wq_index = WQ.check_queue(packet);
  if (wq_index != -1)
  {

    // check fill level
    if (packet->fill_level < fill_level)
    {

      packet->data = WQ.entry[wq_index].data;

      if (fill_level == FILL_L2)
      {
        if (packet->fill_l1i)
        {
          upper_level_icache[packet->cpu]->return_data(packet);
        }
        if (packet->fill_l1d)
        {
          upper_level_dcache[packet->cpu]->return_data(packet);
        }
      }
      else
      {
        if (packet->instruction)
          upper_level_icache[packet->cpu]->return_data(packet);
        if (packet->is_data)
          upper_level_dcache[packet->cpu]->return_data(packet);
      }
    }

    HIT[packet->type]++;
    ACCESS[packet->type]++;

    WQ.FORWARD++;
    PQ.ACCESS++;

    return -1;
  }

  // check for duplicates in the PQ
  int index = PQ.check_queue(packet);
  if (index != -1)
  {
    if (packet->fill_level < PQ.entry[index].fill_level)
    {
      PQ.entry[index].fill_level = packet->fill_level;
    }
    if ((packet->instruction == 1) && (PQ.entry[index].instruction != 1))
    {
      PQ.entry[index].instruction = 1;
    }
    if ((packet->is_data == 1) && (PQ.entry[index].is_data != 1))
    {
      PQ.entry[index].is_data = 1;
    }
    if ((packet->fill_l1i) && (PQ.entry[index].fill_l1i != 1))
    {
      PQ.entry[index].fill_l1i = 1;
    }
    if ((packet->fill_l1d) && (PQ.entry[index].fill_l1d != 1))
    {
      PQ.entry[index].fill_l1d = 1;
    }

    PQ.MERGED++;
    PQ.ACCESS++;

    return index; // merged index
  }

  // check occupancy
  if (PQ.occupancy == PQ_SIZE)
  {
    PQ.FULL++;

    DP(if (warmup_complete[packet->cpu]) { cout << "[" << NAME << "] cannot process add_pq since it is full" << endl; });
    return -2; // cannot handle this request
  }

  // if there is no duplicate, add it to PQ
  index = PQ.tail;

#ifdef SANITY_CHECK
  if (PQ.entry[index].address != 0)
  {
    cerr << "[" << NAME << "_ERROR] " << __func__ << " is not empty index: " << index;
    cerr << " address: " << hex << PQ.entry[index].address;
    cerr << " full_addr: " << PQ.entry[index].full_addr << dec << endl;
    assert(0);
  }
#endif

  PQ.entry[index] = *packet;

  // ADD LATENCY
  if (PQ.entry[index].event_cycle < current_core_cycle[packet->cpu])
    PQ.entry[index].event_cycle = current_core_cycle[packet->cpu] + LATENCY;
  else
    PQ.entry[index].event_cycle += LATENCY;

  PQ.occupancy++;
  PQ.tail++;
  if (PQ.tail >= PQ.SIZE)
    PQ.tail = 0;

  DP(if (warmup_complete[PQ.entry[index].cpu])
     {
       cout << "[" << NAME << "_PQ] " << __func__ << " instr_id: " << PQ.entry[index].instr_id << " address: " << hex << PQ.entry[index].address;
       cout << " full_addr: " << PQ.entry[index].full_addr << dec;
       cout << " type: " << +PQ.entry[index].type << " head: " << PQ.head << " tail: " << PQ.tail << " occupancy: " << PQ.occupancy;
       cout << " event: " << PQ.entry[index].event_cycle << " current: " << current_core_cycle[PQ.entry[index].cpu] << endl;
     });

  if (packet->address == 0)
    assert(0);

  PQ.TO_CACHE++;
  PQ.ACCESS++;

  return -1;
}

/**
 * @brief return data back to MSHR
 * 
 * @param packet 
 */
void CACHE::return_data(PACKET *packet)
{
  // check MSHR information
  int mshr_index = check_mshr(packet);

  // sanity check
  if (mshr_index == -1)
  {
    cerr << "[" << NAME << "_MSHR] " << __func__ << " instr_id: " << packet->instr_id << " cannot find a matching entry!";
    cerr << " full_addr: " << hex << packet->full_addr;
    cerr << " address: " << packet->address << dec;
    cerr << " event: " << packet->event_cycle << " current: " << current_core_cycle[packet->cpu] << endl;
    assert(0);
  }

  // MSHR holds the most updated information about this request
  // no need to do memcpy
  MSHR.num_returned++;
  MSHR.entry[mshr_index].returned = COMPLETED;
  MSHR.entry[mshr_index].data = packet->data;
  MSHR.entry[mshr_index].pf_metadata = packet->pf_metadata;

  // ADD LATENCY
  if (MSHR.entry[mshr_index].event_cycle < current_core_cycle[packet->cpu])
    MSHR.entry[mshr_index].event_cycle = current_core_cycle[packet->cpu] + LATENCY;
  else
    MSHR.entry[mshr_index].event_cycle += LATENCY;

  // index of MSHR will be updated too
  update_fill_cycle();

  DP(if (warmup_complete[packet->cpu])
     {
       cout << "[" << NAME << "_MSHR] " << __func__ << " instr_id: " << MSHR.entry[mshr_index].instr_id;
       cout << " address: " << hex << MSHR.entry[mshr_index].address << " full_addr: " << MSHR.entry[mshr_index].full_addr;
       cout << " data: " << MSHR.entry[mshr_index].data << dec << " num_returned: " << MSHR.num_returned;
       cout << " index: " << mshr_index << " occupancy: " << MSHR.occupancy;
       cout << " event: " << MSHR.entry[mshr_index].event_cycle << " current: " << current_core_cycle[packet->cpu] << " next: " << MSHR.next_fill_cycle << endl;
     });
}

void CACHE::update_fill_cycle()
{
  // update next_fill_cycle
  uint64_t min_cycle = UINT64_MAX;
  uint32_t min_index = MSHR.SIZE;
  for (uint32_t i = 0; i < MSHR.SIZE; i++)
  {
    // fast the oldest entry in MSHR
    if ((MSHR.entry[i].returned == COMPLETED) && (MSHR.entry[i].event_cycle < min_cycle))
    {
      min_cycle = MSHR.entry[i].event_cycle;
      min_index = i;
    }

    DP(if (warmup_complete[MSHR.entry[i].cpu])
       {
         cout << "[" << NAME << "_MSHR] " << __func__ << " checking instr_id: " << MSHR.entry[i].instr_id;
         cout << " address: " << hex << MSHR.entry[i].address << " full_addr: " << MSHR.entry[i].full_addr;
         cout << " data: " << MSHR.entry[i].data << dec << " returned: " << +MSHR.entry[i].returned << " fill_level: " << MSHR.entry[i].fill_level;
         cout << " index: " << i << " occupancy: " << MSHR.occupancy;
         cout << " event: " << MSHR.entry[i].event_cycle << " current: " << current_core_cycle[MSHR.entry[i].cpu] << " next: " << MSHR.next_fill_cycle << endl;
       });
  }

  MSHR.next_fill_cycle = min_cycle;
  MSHR.next_fill_index = min_index;
  if (min_index < MSHR.SIZE)
  {

    DP(if (warmup_complete[MSHR.entry[min_index].cpu])
       {
         cout << "[" << NAME << "_MSHR] " << __func__ << " instr_id: " << MSHR.entry[min_index].instr_id;
         cout << " address: " << hex << MSHR.entry[min_index].address << " full_addr: " << MSHR.entry[min_index].full_addr;
         cout << " data: " << MSHR.entry[min_index].data << dec << " num_returned: " << MSHR.num_returned;
         cout << " event: " << MSHR.entry[min_index].event_cycle << " current: " << current_core_cycle[MSHR.entry[min_index].cpu] << " next: " << MSHR.next_fill_cycle << endl;
       });
  }
}

/**
 * @brief check and return the index of entry in MSHR
 * 
 * @param packet 
 * @return int 
 */
int CACHE::check_mshr(PACKET *packet)
{
  // search mshr
  //bool instruction_and_data_collision = false;

  for (uint32_t index = 0; index < MSHR_SIZE; index++)
  {
    if (MSHR.entry[index].address == packet->address)
    {
      //if(MSHR.entry[index].instruction != packet->instruction)
      //  {
      //    instruction_and_data_collision = true;
      //  }
      //else
      //  {
      DP(if (warmup_complete[packet->cpu])
         {
           cout << "[" << NAME << "_MSHR] " << __func__ << " same entry instr_id: " << packet->instr_id << " prior_id: " << MSHR.entry[index].instr_id;
           cout << " address: " << hex << packet->address;
           cout << " full_addr: " << packet->full_addr << dec << endl;
         });

      return index;
      //  }
    }
  }

  //if(instruction_and_data_collision) // remove instruction-and-data collision safeguard
  //  {
  //return -2;
  //  }

  DP(if (warmup_complete[packet->cpu])
     {
       cout << "[" << NAME << "_MSHR] " << __func__ << " new address: " << hex << packet->address;
       cout << " full_addr: " << packet->full_addr << dec << endl;
     });

  DP(if (warmup_complete[packet->cpu] && (MSHR.occupancy == MSHR_SIZE))
     {
       cout << "[" << NAME << "_MSHR] " << __func__ << " mshr is full";
       cout << " instr_id: " << packet->instr_id << " mshr occupancy: " << MSHR.occupancy;
       cout << " address: " << hex << packet->address;
       cout << " full_addr: " << packet->full_addr << dec;
       cout << " cycle: " << current_core_cycle[packet->cpu] << endl;
     });

  return -1;
}

void CACHE::add_mshr(PACKET *packet)
{
  uint32_t index = 0;

  packet->cycle_enqueued = current_core_cycle[packet->cpu];

  // search mshr
  for (index = 0; index < MSHR_SIZE; index++)
  {
    if (MSHR.entry[index].address == 0)
    {

      MSHR.entry[index] = *packet;
      MSHR.entry[index].returned = INFLIGHT;
      MSHR.occupancy++;

      DP(if (warmup_complete[packet->cpu])
         {
           cout << "[" << NAME << "_MSHR] " << __func__ << " instr_id: " << packet->instr_id;
           cout << " address: " << hex << packet->address << " full_addr: " << packet->full_addr << dec;
           cout << " index: " << index << " occupancy: " << MSHR.occupancy << endl;
         });

      break;
    }
  }
}

uint32_t CACHE::get_occupancy(uint8_t queue_type, uint64_t address)
{
  if (queue_type == 0)
    return MSHR.occupancy;
  else if (queue_type == 1)
    return RQ.occupancy;
  else if (queue_type == 2)
    return WQ.occupancy;
  else if (queue_type == 3)
    return PQ.occupancy;

  return 0;
}

uint32_t CACHE::get_size(uint8_t queue_type, uint64_t address)
{
  if (queue_type == 0)
    return MSHR.SIZE;
  else if (queue_type == 1)
    return RQ.SIZE;
  else if (queue_type == 2)
    return WQ.SIZE;
  else if (queue_type == 3)
    return PQ.SIZE;

  return 0;
}

void CACHE::increment_WQ_FULL(uint64_t address)
{
  WQ.FULL++;
}

bool CACHE::is_in_cache(uint64_t addr)
{
  uint64_t set = this->get_set(addr), way = this->get_way(addr, set);
  return way != NUM_WAY;
}

bool CACHE::print_timeliness_stat()
{
  uint64_t total_request = 0, total_useful = 0, total_late = 0;

  for (int i = 0; i < 5; i++)
  {
    total_request += pref_filled[cpu][i];
    total_useful += pref_useful[cpu][i];
    total_late += pref_late[cpu][i];
  }

  cout << "stream: " << endl;
  cout << "stream:pref_filled: " << pref_filled[cpu][1] << endl;
  cout << "stream:pref_useful: " << pref_useful[cpu][1] << endl;
  cout << "stream:pref_late: " << pref_late[cpu][1] << endl;
  cout << endl;

  cout << "CS: " << endl;
  cout << "CS:pref_filled: " << pref_filled[cpu][2] << endl;
  cout << "CS:pref_useful: " << pref_useful[cpu][2] << endl;
  cout << "CS:pref_late: " << pref_late[cpu][2] << endl;
  cout << endl;

  cout << "CPLX: " << endl;
  cout << "CPLX:pref_filled: " << pref_filled[cpu][3] << endl;
  cout << "CPLX:pref_useful: " << pref_useful[cpu][3] << endl;
  cout << "CPLX:pref_late: " << pref_late[cpu][3] << endl;
  cout << endl;

  cout << "NL_L1: " << endl;
  cout << "NL:pref_filled: " << pref_filled[cpu][4] << endl;
  cout << "NL:pref_useful: " << pref_useful[cpu][4] << endl;
  cout << "NL:pref_late: " << pref_late[cpu][4] << endl;
  cout << endl;

  cout << "total_filled: " << pf_fill << endl;
  cout << "total_useful: " << pf_useful << endl;
  cout << "total_late: " << total_late << endl;

  cout << endl;
}


void CACHE::broadcast_bw(uint8_t bw_level)
{
    /* boradcast to all the attached prefetchers */
    this->cur_bw_level = bw_level;
    switch(cache_type)
    {
        // case IS_L1I:
        //     break;
        // case IS_L1D:
        //     l1d_prefetcher_broadcast_bw(bw_level);
        //     break;
        // case IS_L2C:
        //     l2c_prefetcher_broadcast_bw(bw_level);
        //     break;
        // case IS_LLC:
        //     llc_prefetcher_broadcast_bw(bw_level);
        //     break;
    }

    /* recursively broadcast to higher caches */
    CACHE *cache = NULL;
    for(uint32_t core = 0; core < NUM_CPUS; ++core)
    {
        if(upper_level_dcache[core])
        {
            cache = (CACHE*)upper_level_dcache[core];
            cache->broadcast_bw(bw_level);
        }
        if(upper_level_icache[core] && upper_level_icache[core] != upper_level_dcache[core])
        {
            cache = (CACHE*)upper_level_icache[core];
            cache->broadcast_bw(bw_level);
        }
    }
}

double 
CACHE::get_intime_pref_accuray() 
{
  return double(pf_useful) / pf_fill;
}

double
CACHE::get_period_pref_accuray()
{
  return acc_level/10;
}

void 
CACHE::broadcast_ipc(uint8_t ipc)
{
  this->cur_ipc = ipc;
}

void CACHE::handle_prefetch_feedback()
{
    uint32_t this_epoch_accuracy = 0, acc_level = 0;

    cycle++;
    if(knob::measure_cache_acc && cycle >= next_measure_cycle)
    {
        this_epoch_accuracy = pf_filled_epoch ? 100*(float)pf_useful_epoch/pf_filled_epoch : 0; 
        pref_acc = (pref_acc + this_epoch_accuracy) / 2; // have some hysterisis
        acc_level = (pref_acc / ((float)100/CACHE_ACC_LEVELS)); // quantize into 8 buckets
        if(acc_level >= CACHE_ACC_LEVELS) acc_level = (CACHE_ACC_LEVELS - 1); // corner cases

        pf_useful_epoch = 0;
        pf_filled_epoch = 0;
        next_measure_cycle = cycle + knob::measure_cache_acc_epoch;

        total_acc_epochs++;
        acc_epoch_hist[acc_level]++;

        this->acc_level = acc_level;

        // compute overpridiction
        uint64_t total_load_miss = 0;
        for (int i = 0; i < NUM_CPUS; i++) {
          total_load_miss += sim_miss[i][LOAD];
        }
        uint32_t this_epoch_overp = total_load_miss != last_total_load_miss ? 100*(pf_useless - last_period_useless)/(total_load_miss - last_total_load_miss) : 0;
        pref_overp = (pref_overp + this_epoch_overp)/2;
        this->overprediction_level = (pref_overp/(float)100/CACHE_ACC_LEVELS);
        if (this->overprediction_level >= CACHE_ACC_LEVELS)
          this->overprediction_level = CACHE_ACC_LEVELS - 1;

        last_total_load_miss = total_load_miss;
        last_period_useless = pf_useless;
    }
}
