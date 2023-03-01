#ifndef _COMPONENT_H_
#define _COMPONENT_H_

#include "champsim.h"
#include "common.h"

#include <algorithm>
#include <unordered_map>
#include <functional>
#include <sstream>
#include <random>

class Table {
  public:
    Table(int width, int height) : width(width), height(height), cells(height, vector<string>(width)) {}

    void set_row(int row, const vector<string> &data, int start_col = 0) {
        // assert(data.size() + start_col == this->width);
        for (unsigned col = start_col; col < this->width; col += 1)
            this->set_cell(row, col, data[col]);
    }

    void set_col(int col, const vector<string> &data, int start_row = 0) {
        // assert(data.size() + start_row == this->height);
        for (unsigned row = start_row; row < this->height; row += 1)
            this->set_cell(row, col, data[row]);
    }

    void set_cell(int row, int col, string data) {
        // assert(0 <= row && row < (int)this->height);
        // assert(0 <= col && col < (int)this->width);
        this->cells[row][col] = data;
    }

    void set_cell(int row, int col, double data) {
        ostringstream oss;
        oss << setw(11) << fixed << setprecision(8) << data;
        this->set_cell(row, col, oss.str());
    }

    void set_cell(int row, int col, int64_t data) {
        ostringstream oss;
        oss << setw(11) << std::left << data;
        this->set_cell(row, col, oss.str());
    }

    void set_cell(int row, int col, int data) { this->set_cell(row, col, (int64_t)data); }

    void set_cell(int row, int col, uint64_t data) {
        ostringstream oss;
        oss << "0x" << setfill('0') << setw(16) << hex << data;
        this->set_cell(row, col, oss.str());
    }

    /**
     * @return The entire table as a string
     */
    string to_string() {
        vector<int> widths;
        for (unsigned i = 0; i < this->width; i += 1) {
            int max_width = 0;
            for (unsigned j = 0; j < this->height; j += 1)
                max_width = max(max_width, (int)this->cells[j][i].size());
            widths.push_back(max_width + 2);
        }
        string out;
        out += Table::top_line(widths);
        out += this->data_row(0, widths);
        for (unsigned i = 1; i < this->height; i += 1) {
            out += Table::mid_line(widths);
            out += this->data_row(i, widths);
        }
        out += Table::bot_line(widths);
        return out;
    }

    string data_row(int row, const vector<int> &widths) {
        string out;
        for (unsigned i = 0; i < this->width; i += 1) {
            string data = this->cells[row][i];
            data.resize(widths[i] - 2, ' ');
            out += " | " + data;
        }
        out += " |\n";
        return out;
    }

    static string top_line(const vector<int> &widths) { return Table::line(widths, "┌", "┬", "┐"); }

    static string mid_line(const vector<int> &widths) { return Table::line(widths, "├", "┼", "┤"); }

    static string bot_line(const vector<int> &widths) { return Table::line(widths, "└", "┴", "┘"); }

    static string line(const vector<int> &widths, string left, string mid, string right) {
        string out = " " + left;
        for (unsigned i = 0; i < widths.size(); i += 1) {
            int w = widths[i];
            for (int j = 0; j < w; j += 1)
                out += "─";
            if (i != widths.size() - 1)
                out += mid;
            else
                out += right;
        }
        return out + "\n";
    }

  private:
    unsigned width;
    unsigned height;
    vector<vector<string>> cells;
};

template <class T> class InfiniteCache {
public:

    // Fake parameters for key generation
    InfiniteCache (int size, int num_ways, int debug_level = 0)
        : size(size), num_ways(num_ways), num_sets(size / num_ways), debug_level(debug_level) {
        /* calculate `index_len` (number of bits required to store the index) */
        for (int max_index = num_sets - 1; max_index > 0; max_index >>= 1)
            this->index_len += 1;
    }

    class Entry {
    public:
        uint64_t key;
        uint64_t index;
        uint64_t tag;
        bool valid;
        T data;
    };

    Entry *erase(uint64_t key) {
        Entry *entry = this->find(key);
        if (!entry)
            return nullptr;
        entry->valid = false;
        this->last_erased_entry = *entry;
        int num_erased = this->entries.erase(key);
        assert(num_erased == 1);
        return &this->last_erased_entry;
    }

    /**
     * @return The old state of the entry that was written to.
     */
    Entry insert(uint64_t key, const T &data) {
        Entry *entry = this->find(key);
        if (entry != nullptr) {
            Entry old_entry = *entry;
            entry->data = data;
            return old_entry;
        }
        entries[key] = { key, 0, key, true, data };
        return {};
    }

    Entry *find(uint64_t key) {
        auto it = this->entries.find(key);
        if (it == this->entries.end())
            return nullptr;
        Entry &entry = (*it).second;
        assert(entry.tag == key && entry.valid);
        return &entry;
    }

    /**
     * For debugging purposes.
     */
    string log(vector<string> headers, function<void(Entry &, Table &, int)> write_data) {
        Table table(headers.size(), entries.size() + 1);
        table.set_row(0, headers);
        unsigned i = 0;
        for (auto &x : this->entries)
            write_data(x.second, table, ++i);
        return table.to_string();
    }

    vector<Entry> get_valid_entries()
    {
        vector<Entry> res;
        res.reserve(this->entries.size());
        for (auto e: this->entries) {
            res.push_back(e.second);
        }
        return res;
    }

    string log(vector<string> headers)
    {
        vector<Entry> valid_entries = this->get_valid_entries();
        Table table(headers.size(), valid_entries.size() + 1);
        table.set_row(0, headers);
        for (unsigned i = 0; i < valid_entries.size(); i += 1)
            this->write_data(valid_entries[i], table, i + 1);
        return table.to_string();
    }

    int get_index_len() { return this->index_len; }
    void set_debug_level(int debug_level) { this->debug_level = debug_level; }

protected:
    virtual void write_data(Entry &entry, Table &table, int row) {}

    void rp_promote(uint64_t key) {}
    void rp_insert(uint64_t key) {}
    Entry last_erased_entry;
    unordered_map<uint64_t, Entry> entries;
    int debug_level = 0;
    int num_ways;
    int num_sets;
    int index_len;
    int size;
};

template <class T> class InfiniteWayCache {
public:

    // Fake parameters for key generation
    InfiniteWayCache (int size, int num_ways, int debug_level = 0)
        : size(size), num_ways(num_ways), num_sets(size / num_ways), 
          debug_level(debug_level), entries(size/num_ways) {
        /* calculate `index_len` (number of bits required to store the index) */
        for (int max_index = num_sets - 1; max_index > 0; max_index >>= 1)
            this->index_len += 1;
    }

    class Entry {
    public:
        uint64_t key;
        uint64_t index;
        uint64_t tag;
        bool valid;
        T data;
    };

    Entry *erase(uint64_t key) {
        Entry *entry = this->find(key);
        if (!entry)
            return nullptr;
        uint64_t tag = key / num_sets;
        uint64_t index = key % num_sets;
        entry->valid = false;
        this->last_erased_entry = *entry;
        int num_erased = this->entries[index].erase(tag);
        assert(num_erased == 1);
        return &this->last_erased_entry;
    }

    /**
     * @return The old state of the entry that was written to.
     */
    Entry insert(uint64_t key, const T &data) {
        Entry *entry = this->find(key);
        if (entry != nullptr) {
            Entry old_entry = *entry;
            entry->data = data;
            return old_entry;
        }
        uint64_t tag = key / num_sets;
        uint64_t index = key % num_sets;
        entries[index][tag] = { key, index, tag, true, data };
        return {};
    }

    Entry *find(uint64_t key) {
        uint64_t tag = key / num_sets;
        uint64_t index = key % num_sets;
        auto it = this->entries[index].find(tag);
        if (it == this->entries[index].end())
            return nullptr;
        Entry &entry = (*it).second;
        assert(entry.tag == tag && entry.valid);
        return &entry;
    }

    /**
     * For debugging purposes.
     */
    string log(vector<string> headers, function<void(Entry &, Table &, int)> write_data) {
        Table table(headers.size(), entries.size() + 1);
        table.set_row(0, headers);
        unsigned i = 0;
        for (auto &x : this->entries)
            write_data(x.second, table, ++i);
        return table.to_string();
    }

    vector<Entry> get_valid_entries()
    {
        vector<Entry> res;
        int size = 0;
        for (auto set: this->entries)
            size += set.size();
        res.reserve(size);
        for (auto set: this->entries) {
            for (auto e: set)
                res.push_back(e.second);
        }
        return res;
    }

    string log(vector<string> headers)
    {
        vector<Entry> valid_entries = this->get_valid_entries();
        Table table(headers.size(), valid_entries.size() + 1);
        table.set_row(0, headers);
        for (unsigned i = 0; i < valid_entries.size(); i += 1)
            this->write_data(valid_entries[i], table, i + 1);
        return table.to_string();
    }

    int get_index_len() { return this->index_len; }
    void set_debug_level(int debug_level) { this->debug_level = debug_level; }

protected:
    virtual void write_data(Entry &entry, Table &table, int row) {}

    void rp_promote(uint64_t key) {}
    void rp_insert(uint64_t key) {}
    Entry last_erased_entry;
    vector<unordered_map<uint64_t, Entry>> entries;
    int debug_level = 0;
    int num_ways;
    int num_sets;
    int index_len;
    int size;
};

template <class T> class SetAssociativeCache {
  public:
    class Entry {
      public:
        uint64_t key;
        uint64_t index;
        uint64_t tag;
        bool valid;
        T data;
    };

    SetAssociativeCache(int size, int num_ways, int debug_level = 0)
        : size(size), num_ways(num_ways), num_sets(size / num_ways), entries(num_sets, vector<Entry>(num_ways)),
          cams(num_sets, unordered_map<uint64_t, int>(num_ways)), debug_level(debug_level) {
        // assert(size % num_ways == 0);
        for (int i = 0; i < num_sets; i += 1)
            for (int j = 0; j < num_ways; j += 1) 
                entries[i][j].valid = false;
        /* calculate `index_len` (number of bits required to store the index) */
        for (int max_index = num_sets - 1; max_index > 0; max_index >>= 1)
            this->index_len += 1;
    }

    /**
     * Invalidates the entry corresponding to the given key.
     * @return A pointer to the invalidated entry
     */
    Entry *erase(uint64_t key) {
        Entry *entry = this->find(key);
        uint64_t index = key % this->num_sets;
        uint64_t tag = key / this->num_sets;
        auto &cam = cams[index];
        int num_erased = cam.erase(tag);
        if (entry)
            entry->valid = false;
        // assert(entry ? num_erased == 1 : num_erased == 0);
        return entry;
    }

    /**
     * @return The old state of the entry that was updated
     */
    Entry insert(uint64_t key, const T &data) {
        Entry *entry = this->find(key);
        if (entry != nullptr) {
            Entry old_entry = *entry;
            entry->data = data;
            return old_entry;
        }
        uint64_t index = key % this->num_sets;
        uint64_t tag = key / this->num_sets;
        vector<Entry> &set = this->entries[index];
        int victim_way = -1;
        for (int i = 0; i < this->num_ways; i += 1)
            if (!set[i].valid) {
                victim_way = i;
                break;
            }
        if (victim_way == -1) {
            victim_way = this->select_victim(index);
        }
        Entry &victim = set[victim_way];
        Entry old_entry = victim;
        victim = {key, index, tag, true, data};
        auto &cam = cams[index];
        if (old_entry.valid) {
            int num_erased = cam.erase(old_entry.tag);
            // assert(num_erased == 1);
        }
        cam[tag] = victim_way;
        return old_entry;
    }

    Entry *find(uint64_t key) {
        uint64_t index = key % this->num_sets;
        uint64_t tag = key / this->num_sets;
        auto &cam = cams[index];
        if (cam.find(tag) == cam.end())
            return nullptr;
        int way = cam[tag];
        Entry &entry = this->entries[index][way];
        // assert(entry.tag == tag && entry.valid);
        if (!entry.valid)
            return nullptr;
        return &entry;
    }

    void flush() {
        for (int i = 0; i < num_sets; i += 1) {
            cams[i].clear();
            for (int j = 0; j < num_ways; j += 1)
                entries[i][j].valid = false;
        }
    }

    /**
     * Creates a table with the given headers and populates the rows by calling `write_data` on all
     * valid entries contained in the cache. This function makes it easy to visualize the contents
     * of a cache.
     * @return The constructed table as a string
     */
    string log(vector<string> headers) {
        vector<Entry> valid_entries = this->get_valid_entries();
        Table table(headers.size(), valid_entries.size() + 1);
        table.set_row(0, headers);
        for (unsigned i = 0; i < valid_entries.size(); i += 1)
            this->write_data(valid_entries[i], table, i + 1);
        return table.to_string();
    }

    int get_index_len() { return this->index_len; }

    void set_debug_level(int debug_level) { this->debug_level = debug_level; }

  protected:
    /* should be overriden in children */
    virtual void write_data(Entry &entry, Table &table, int row) {}

    /**
     * @return The way of the selected victim
     */
    virtual int select_victim(uint64_t index) {
        /* random eviction policy if not overriden */
        return rand() % this->num_ways;
    }

    vector<Entry> get_valid_entries() {
        vector<Entry> valid_entries;
        for (int i = 0; i < num_sets; i += 1)
            for (int j = 0; j < num_ways; j += 1)
                if (entries[i][j].valid)
                    valid_entries.push_back(entries[i][j]);
        return valid_entries;
    }

    int size;
    int num_ways;
    int num_sets;
    int index_len = 0; /* in bits */
    vector<vector<Entry>> entries;
    vector<unordered_map<uint64_t, int>> cams;
    int debug_level = 0;
};

template <class T> class LRUSetAssociativeCache : public SetAssociativeCache<T> {
    typedef SetAssociativeCache<T> Super;

  public:
    LRUSetAssociativeCache(int size, int num_ways, int debug_level = 0)
        : Super(size, num_ways, debug_level), lru(this->num_sets, vector<uint64_t>(num_ways)) {}

    void set_mru(uint64_t key) { *this->get_lru(key) = this->t++; }

    void set_lru(uint64_t key) { *this->get_lru(key) = 0; }

    void rp_promote(uint64_t key) {set_mru(key);}

    void rp_insert(uint64_t key) {set_mru(key);}

  protected:
    /* @override */
    int select_victim(uint64_t index) {
        vector<uint64_t> &lru_set = this->lru[index];
        return min_element(lru_set.begin(), lru_set.end()) - lru_set.begin();
    }

    uint64_t *get_lru(uint64_t key) {
        uint64_t index = key % this->num_sets;
        uint64_t tag = key / this->num_sets;
        // assert(this->cams[index].count(tag) == 1);
        int way = this->cams[index][tag];
        return &this->lru[index][way];
    }

    vector<vector<uint64_t>> lru;
    uint64_t t = 1;
};

template<class T> 
class LFUSetAssociativeCache: public SetAssociativeCache<T> {
    typedef SetAssociativeCache<T> Super;

  public:
    LFUSetAssociativeCache(int size, int num_ways, int debug_level = 0)
        : Super(size, num_ways, debug_level), frq_(this->num_sets, vector<uint64_t>(num_ways)) {}

    void rp_promote(uint64_t key) { (*this->get_frequency(key))++;}

    void rp_insert(uint64_t key) { (*this->get_frequency(key)) = 1;}

  protected:
    /* @override */
    int select_victim(uint64_t index) {
        vector<uint64_t> &frq_set = this->frq_[index];
        return min_element(frq_set.begin(), frq_set.end()) - frq_set.begin();
    }

    uint64_t *get_frequency(uint64_t key) {
        uint64_t index = key % this->num_sets;
        uint64_t tag = key / this->num_sets;
        // assert(this->cams[index].count(tag) == 1);
        int way = this->cams[index][tag];
        return &this->frq_[index][way];
    }

    vector<vector<uint64_t>> frq_;
};

/**
 * @brief need test
 * 
 * @tparam T 
 */
template <class T> class DynIndexSetAssociativeCache : public SetAssociativeCache<T> {
    typedef SetAssociativeCache<T> Super;
  public:
    DynIndexSetAssociativeCache(int size, int num_ways, uint64_t dyn_index_mask, int max_dyn_index_score, int debug_level = 0) 
    : dynamic_index_(size/num_ways, -1), dyn_index_mask_(dyn_index_mask), max_dyn_index_score_(max_dyn_index_score) {}

    int get_index(uint64_t key) {
        if (dynamic_index_.cam.find(key) != dynamic_index_.cam.end()) {
            return dynamic_index_.cam[key];
        }
        return -1;
    }

    int update_dyn_index(uint64_t key) {
        vector<int>::iterator item = min_element(dynamic_index_.score.begin(), dynamic_index_.score.end());
        *item = 1;
        int index = std::distance(dynamic_index_.score, item); 
        dynamic_index_.cam[key] = index;
        return index;
    }

    typename Super::Entry *find(uint64_t key) {
        int index = get_index(key & dyn_index_mask_);
        if (index != -1) {
            uint64_t new_key = index | (key & ~(this->num_sets-1));
            return Super::find(new_key);
        } 

        return nullptr;
    }

    typename Super::Entry insert(uint64_t key, const T& data) {
        int index = get_index(key & dyn_index_mask_);
        if (index != -1) {
            uint64_t new_key = index | (key & ~(this->num_sets-1));
            ADD(dynamic_index_.score[index], max_dyn_index_score_);
            return Super::insert(new_key, data);
        } else {
            index = update_dyn_index(key & dyn_index_mask_);
            uint64_t new_key = index | (key & ~(this->num_sets-1));
            this->cams[index].clear();
            for (auto &e : this->entries[index]) {
                e.valid = false;
            }

            return Super::insert(new_key, data);
        }
    }

    typename Super::Entry erase(uint64_t key) {
        int index = get_index(key & dyn_index_mask_);
        if (index != -1) {
            uint64_t new_key = index | (key & ~(this->num_sets-1));
            return Super::erase(new_key);
        }
        return nullptr;       
    }

  private:
    struct{
        vector<int> scores;
        unordered_map<int, int> cam;
    } dynamic_index_; 

    uint64_t dyn_index_mask_;
    uint64_t max_dyn_index_score_;
};

template <class T> class SRRIPSetAssociativeCache : public SetAssociativeCache<T> {
    typedef SetAssociativeCache<T> Super;

  public:
    SRRIPSetAssociativeCache(int size, int num_ways, int debug_level = 0, int max_rrpv = 3)
        : Super(size, num_ways, debug_level), rrpv(this->num_sets, vector<uint64_t>(num_ways)), 
        max_rrpv(max_rrpv) {}

    void rp_promote(uint64_t key) {*this->get_rrpv(key) = 0;}

    void rp_insert(uint64_t key) {*this->get_rrpv(key) = 2;}

  protected:
    /* @override */
    int select_victim(uint64_t index) {
        vector<uint64_t> &rrpv_set = this->rrpv[index];
        for (;;) {
            for (int i = 0; i < this->num_ways; i++) {
                if (rrpv_set[i] >= max_rrpv) {
                    return i;
                }
            }
            aging(index);
        } 
    }

    uint64_t *get_rrpv(uint64_t key) {
        uint64_t index = key % this->num_sets;
        uint64_t tag = key / this->num_sets;
        // assert(this->cams[index].count(tag) == 1);
        int way = this->cams[index][tag];
        return &this->rrpv[index][way];
    }
  private:
    void aging(uint64_t index) {
        vector<uint64_t> &rrpv_set = this->rrpv[index];
        for (auto &r:rrpv_set) {
            ADD(r, max_rrpv);
        }
    }

    vector<vector<uint64_t>> rrpv;
    int max_rrpv;
};

template <class T> class BIPSetAssociativeCache : public SetAssociativeCache<T> {
    typedef SetAssociativeCache<T> Super;

  public:
    BIPSetAssociativeCache(int size, int num_ways, int debug_level = 0, double epsilon=0.1)
        : Super(size, num_ways, debug_level), lru(this->num_sets, vector<uint64_t>(num_ways)), 
        b_dist(epsilon) {}

    void set_mru(uint64_t key) { *this->get_lru(key) = this->t++; }

    void set_lru(uint64_t key) { *this->get_lru(key) = 0; }

    void rp_promote(uint64_t key) {set_mru(key);}

    void rp_insert(uint64_t key) { *this->get_lru(key) = b_dist(engine) ? t : t/2;}

  protected:
    /* @override */
    int select_victim(uint64_t index) {
        vector<uint64_t> &lru_set = this->lru[index];
        return min_element(lru_set.begin(), lru_set.end()) - lru_set.begin();
    }

    uint64_t *get_lru(uint64_t key) {
        uint64_t index = key % this->num_sets;
        uint64_t tag = key / this->num_sets;
        // assert(this->cams[index].count(tag) == 1);
        int way = this->cams[index][tag];
        return &this->lru[index][way];
    }

    vector<vector<uint64_t>> lru;
    uint64_t t = 1;

    default_random_engine engine;
    bernoulli_distribution b_dist;
};

template <class T> class BRRIPSetAssociativeCache : public SetAssociativeCache<T> {
    typedef SetAssociativeCache<T> Super;

  public:
    BRRIPSetAssociativeCache(int size, int num_ways, int debug_level = 0, int max_rrpv = 3, double epsilon = 0.1)
        : Super(size, num_ways, debug_level), rrpv(this->num_sets, vector<uint64_t>(num_ways)), 
        max_rrpv(max_rrpv), b_dist(epsilon) {}

    void rp_promote(uint64_t key) {*this->get_rrpv(key) = 0;}

    void rp_insert(uint64_t key) {*this->get_rrpv(key) = b_dist(engine) ? 2 : 3;}

  protected:
    /* @override */
    int select_victim(uint64_t index) {
        vector<uint64_t> &rrpv_set = this->rrpv[index];
        for (;;) {
            for (int i = 0; i < this->num_ways; i++) {
                if (rrpv_set[i] >= max_rrpv) {
                    return i;
                }
            }
            aging(index);
        } 
    }

    uint64_t *get_rrpv(uint64_t key) {
        uint64_t index = key % this->num_sets;
        uint64_t tag = key / this->num_sets;
        // assert(this->cams[index].count(tag) == 1);
        int way = this->cams[index][tag];
        return &this->rrpv[index][way];
    }

  private:
    void aging(uint64_t index) {
        vector<uint64_t> &rrpv_set = this->rrpv[index];
        for (auto &r:rrpv_set) {
            ADD(r, max_rrpv);
        }
    }

    vector<vector<uint64_t>> rrpv;
    int max_rrpv;
    default_random_engine engine;
    bernoulli_distribution b_dist;
};

template <class T> class NMRUSetAssociativeCache : public SetAssociativeCache<T> {
    typedef SetAssociativeCache<T> Super;

public:
    NMRUSetAssociativeCache(int size, int num_ways) : Super(size, num_ways), mru(this->num_sets) {}

    void set_mru(uint64_t key) {
        uint64_t index = key % this->num_sets;
        uint64_t tag = key / this->num_sets;
        int way = this->cams[index][tag];
        this->mru[index] = way;
    }

protected:
    /* @override */
    int select_victim(uint64_t index) {
        int way = rand() % (this->num_ways - 1);
        if (way >= mru[index])
            way += 1;
        return way;
    }

    vector<int> mru;
};

template <class T> class LRUFullyAssociativeCache : public LRUSetAssociativeCache<T> {
    typedef LRUSetAssociativeCache<T> Super;

public:
    LRUFullyAssociativeCache(int size) : Super(size, size) {}
};

template <class T> class NMRUFullyAssociativeCache : public NMRUSetAssociativeCache<T> {
    typedef NMRUSetAssociativeCache<T> Super;

public:
    NMRUFullyAssociativeCache(int size) : Super(size, size) {}
};

template <class T> class DirectMappedCache : public SetAssociativeCache<T> {
    typedef SetAssociativeCache<T> Super;

public:
    DirectMappedCache(int size) : Super(size, 1) {}
};

/** End Of Cache Framework **/

class ShiftRegister {
public:
    /* the maximum total capacity of this shift register is 64 bits */
    ShiftRegister(unsigned size = 4) : size(size), width(64 / size) {}

    void insert(int x) {
        x &= (1 << this->width) - 1;
        this->reg = (this->reg << this->width) | x;
    }

    /**
     * @return Returns raw buffer data in range [le, ri).
     * Note that more recent data have a smaller index.
     */
    uint64_t get_code(unsigned le, unsigned ri) {
        assert(0 <= le && le < this->size);
        assert(le < ri && ri <= this->size);
        uint64_t mask = (1ull << (this->width * (ri - le))) - 1ull;
        return (this->reg >> (le * this->width)) & mask;
    }

    /**
     * @return Returns integer value of data at specified index.
     */
    int get_value(int i) {
        int x = this->get_code(i, i + 1);
        /* sign extend */
        int d = 32 - this->width;
        return (x << d) >> d;
    }
    
    bool all_is_same_value() {
        for (size_t i = 0; i < size - 1; i++) {
            if (get_value(i) != get_value(i+1))
                return false;
        }
        return true;
    }

private:
    unsigned size;
    unsigned width;
    uint64_t reg = 0;
};

class SaturatingCounter {
public:
    SaturatingCounter(int size = 2, int value=0) : size(size), max((1 << size) - 1), cnt(value) {}

    int inc() {
        this->cnt += 1;
        if (this->cnt > this->max)
            this->cnt = this->max;
        return this->cnt;
    }

    int dec() {
        this->cnt -= 1;
        if (this->cnt < 0)
            this->cnt = 0;
        return this->cnt;
    }

    int get_cnt() { return this->cnt; }

    bool operator==(int value) {
        return cnt == value;
    }

    bool operator>(SaturatingCounter& other) {
        return cnt > other.cnt;
    }

    bool operator>=(SaturatingCounter& other) {
        return cnt >= other.cnt;
    }

    bool operator<(SaturatingCounter& other) {
        return cnt < other.cnt;
    }

    bool operator<=(SaturatingCounter& other) {
        return cnt <= other.cnt;
    }

private:
    int size, max, cnt = 0;
};

template<class C> class AddrMappingCache: public LRUSetAssociativeCache<std::vector<C>> {
    typedef LRUSetAssociativeCache<std::vector<C>> Super;
public:
    AddrMappingCache(int size, int num_ways, int entry_size)
    : Super(size, num_ways), entry_size(entry_size)
    {}
   
    uint64_t get_entry_group_key(uint64_t addr)
    {
        return addr / entry_size;
    }

    uint64_t get_entry_offset(uint64_t addr)
    {
        return addr % entry_size;
    }  

    C* 
    get_mapping_entry(uint64_t addr)
    {
        uint64_t key = get_entry_group_key(addr);
        uint64_t offset = get_entry_offset(addr);
        auto entry = this->find(key);
        if (entry) {
            return &entry->data[offset];
        } else {
            return nullptr;
        }
    }

protected:
    int entry_size;
};
#endif