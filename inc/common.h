#ifndef _COMMON_H_
#define _COMMON_H_

// ***************** borrow from IPCP *********************
#define S_TYPE 1    // stream
#define CS_TYPE 2   // constant stride
#define CPLX_TYPE 3 // complex stride
#define NL_TYPE 4   // next line


#define ADD(x, MAX) (x = x >= MAX ? x : x + 1)
#define ADD_ANY(x, y, MAX) (x = x + y >= MAX ? MAX : x + y)
#define TIME(x, y, MAX) (x = x * y > MAX ? MAX : x * y)
#define SHIFT(x, y, MAX) (x = (x << y) > MAX ? MAX : x << y)
#define ADD_BACKOFF(x, MAX) (x = (x == MAX ? x >> 1 : x + 1))
#define SUB(x, MIN) (x = (x <= MIN ? x : x - 1))
#define FLIP(x) (~x)

#include <stdint.h>
#include <bits/stdc++.h>
#include <vector>
#include <unordered_map>

using namespace std;

uint64_t get_hash(uint64_t key);
int transfer(int origin);
int count_bits(uint64_t a);
int count_bits(const vector<bool> &x);
uint64_t pattern_to_int(const vector<bool> &pattern);
vector<bool> pattern_convert2(const vector<int> &x);
vector<bool> pattern_convert2(const vector<uint32_t> &x);
vector<int> pattern_convert(const vector<bool> &x);
vector<bool> pattern_degrade(const vector<bool> &x, int level);

double jaccard_similarity(vector<bool> pattern1, vector<bool> pattern2) ;
double jaccard_similarity(vector<bool> pattern1, vector<int> pattern2) ;
int pattern_distance(uint64_t p1, uint64_t p2);
uint64_t hash_index(uint64_t key, int index_len) ;
uint32_t encode_metadata(int stride, uint16_t type, int spec_nl);
void gen_random(char *s, const int len) ;
uint32_t folded_xor(uint64_t value, uint32_t num_folds);

template <class T> string pattern_to_string(const vector<T> &pattern) {
    ostringstream oss;
    for (unsigned i = 0; i < pattern.size(); i += 1)
        oss << int(pattern[i]) << " ";
    return oss.str();
}

template <class T> std::string 
array_to_string(std::vector<T> array, bool hex = false, uint32_t size = 0)
{
    std::stringstream ss;
    if (size == 0) size = array.size();
    for(uint32_t index = 0; index < size; ++index)
    {
    	if(hex)
    	{
    		ss << std::hex << array[index] << std::dec;
    	}
    	else
    	{
    		ss << array[index];
    	}
        ss << ",";
    }
    return ss.str();
}

template <class T>
vector<T> my_rotate(const vector<T> &x, int n)
{
    vector<T> y;
    int len = x.size();
    if (len == 0)
        return y;
    n = n % len;
    for (int i = 0; i < len; i += 1)
        y.push_back(x[(i - n + len) % len]);
    return y;
}

class HashZoo
{
public:
	static uint32_t jenkins(uint32_t key);
	static uint32_t knuth(uint32_t key);
	static uint32_t murmur3(uint32_t key);
	static uint32_t jenkins32(uint32_t key);
	static uint32_t hash32shift(uint32_t key);
	static uint32_t hash32shiftmult(uint32_t key);
	static uint32_t hash64shift(uint32_t key);
	static uint32_t hash5shift(uint32_t key);
	static uint32_t hash7shift(uint32_t key);
	static uint32_t Wang6shift(uint32_t key);
	static uint32_t Wang5shift(uint32_t key);
	static uint32_t Wang4shift( uint32_t key);
	static uint32_t Wang3shift( uint32_t key);

    static uint32_t three_hybrid1(uint32_t key);
    static uint32_t three_hybrid2(uint32_t key);
    static uint32_t three_hybrid3(uint32_t key);
    static uint32_t three_hybrid4(uint32_t key);
    static uint32_t three_hybrid5(uint32_t key);
    static uint32_t three_hybrid6(uint32_t key);
    static uint32_t three_hybrid7(uint32_t key);
    static uint32_t three_hybrid8(uint32_t key);
    static uint32_t three_hybrid9(uint32_t key);
    static uint32_t three_hybrid10(uint32_t key);
    static uint32_t three_hybrid11(uint32_t key);
    static uint32_t three_hybrid12(uint32_t key);

    static uint32_t four_hybrid1(uint32_t key);
    static uint32_t four_hybrid2(uint32_t key);
    static uint32_t four_hybrid3(uint32_t key);
    static uint32_t four_hybrid4(uint32_t key);
    static uint32_t four_hybrid5(uint32_t key);
    static uint32_t four_hybrid6(uint32_t key);
    static uint32_t four_hybrid7(uint32_t key);
    static uint32_t four_hybrid8(uint32_t key);
    static uint32_t four_hybrid9(uint32_t key);
    static uint32_t four_hybrid10(uint32_t key);
    static uint32_t four_hybrid11(uint32_t key);
    static uint32_t four_hybrid12(uint32_t key);

    static uint32_t getHash(uint32_t selector, uint32_t key);
};

#endif