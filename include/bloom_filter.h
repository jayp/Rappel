/*
 **************************************************************************
 *                                                                        *
 *          General Purpose Hash Function Algorithms Library              *
 *                                                                        *
 * Author: Arash Partow - 2002                                            *
 * Class: Bloom Filter                                                    *
 * URL: http://www.partow.net                                             *
 * URL: http://www.partow.net/programming/hashfunctions/index.html        *
 *                                                                        *
 * Copyright notice:                                                      *
 * Free use of the General Purpose Hash Function Algorithms Library is    *
 * permitted under the guidelines and in accordance with the most current *
 * version of the Common Public License.                                  *
 * http://www.opensource.org/licenses/cpl.php                             *
 *                                                                        *
 **************************************************************************
*/

#ifndef INCLUDE_BLOOM_FILTER_H
#define INCLUDE_BLOOM_FILTER_H

#include <string>
#include "rappel_common.h"
#if COMPILE_FOR == NETWORK
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#endif

using namespace std;

// 8 bits in 1 char(unsigned)
const unsigned int CHAR_SIZE_IN_BITS = 0x08;    

const unsigned char BIT_MASK[8] = {
  0x01, //00000001
  0x02, //00000010
  0x04, //00000100
  0x08, //00001000
  0x10, //00010000
  0x20, //00100000
  0x40, //01000000
  0x80  //10000000
};

const unsigned int BITS_IN_CHAR[256] = {
  0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4, 1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, //31
  1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, //63
  1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, //95
  2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7, //127
  1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, //159
  2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7, //191
  2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7, //223
  3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7, 4, 5, 5, 6, 5, 6, 6, 7, 5, 6, 6, 7, 6, 7, 7, 8  //255
};

typedef unsigned int (*HashFunction)(const string&);

unsigned int RSHash  (const string& str);
unsigned int JSHash  (const string& str);
unsigned int PJWHash (const string& str);
unsigned int ELFHash (const string& str);
unsigned int BKDRHash(const string& str);
unsigned int SDBMHash(const string& str);
unsigned int DJBHash (const string& str);
unsigned int DEKHash (const string& str);
unsigned int APHash  (const string& str);

const HashFunction hash_funcs[] = {
  RSHash,
  JSHash,
  PJWHash,
  BKDRHash,
  SDBMHash,
  DJBHash,
  DEKHash,
  APHash,
};

class BloomFilter {

public:

  BloomFilter() {
    assert(globops.is_initialized());
    _table_size = globops.bloom_size;
    _hash_table.resize(_table_size, 0);
    _version = 0;
  }

  void init() {
    assert(_version == 0);
    _version = 1;
  }

  unsigned int get_version() const {
    return _version;
  }

  void insert(const string& key) {

    for (unsigned int i = 0; i < globops.bloom_hash_funcs; i++) {

      unsigned int hash = hash_funcs[i](key) % (_table_size * CHAR_SIZE_IN_BITS);
      unsigned int index = hash / CHAR_SIZE_IN_BITS;

      _hash_table[index] = (unsigned char)
        (_hash_table[index] | BIT_MASK[hash % CHAR_SIZE_IN_BITS]);
    }

    assert(contains(key));
    _version++;
  }

  bool contains(const string& key) const {

    for (size_t i = 0; i < globops.bloom_hash_funcs; i++) {
      
      unsigned int hash = hash_funcs[i](key) % (_table_size * CHAR_SIZE_IN_BITS);
      unsigned int bit  = hash % CHAR_SIZE_IN_BITS;
      unsigned int index = hash / CHAR_SIZE_IN_BITS;

      // if any of the globops.bloom_hash_funcs are off, return false
      if ((_hash_table[index] & BIT_MASK[bit]) != BIT_MASK[bit]) {
        return false;
      }
    }
    
    return true;
  }
  
  unsigned int get_intersection_size(const BloomFilter& compare) const {

    unsigned int ret = 0;
    for (unsigned int i = 0; i < _table_size; i++) {

      unsigned char a = _hash_table[i];
      unsigned char b = compare._hash_table[i];
      unsigned char merged = a & b;
      ret += BITS_IN_CHAR[merged];
    }
    return ret;
  }

  unsigned int get_union_size(const BloomFilter& compare) const {

    unsigned int ret = 0;
    for (unsigned int i = 0; i < _table_size; i++) {
      
      unsigned char a = _hash_table[i];
      unsigned char b = compare._hash_table[i];
      unsigned char merged = a | b;
      ret += BITS_IN_CHAR[merged];
    }
    return ret;
  }

  unsigned int active_bits() const {

    unsigned int ret = 0;
    for (unsigned int i = 0; i < _table_size; i++) {
      
      unsigned char a = _hash_table[i];
      ret += BITS_IN_CHAR[a];
    }
    return ret;
  }

  unsigned char get_byte(unsigned int pos) const {
    assert(pos < _table_size) ;
    return (_hash_table[pos]);
  }

  bool contains_bit(unsigned int bit_id) const {
    assert(bit_id < _table_size * 8);
    return (_hash_table[bit_id / CHAR_SIZE_IN_BITS] & BIT_MASK[bit_id % CHAR_SIZE_IN_BITS]);
  }

  unsigned int get_table_size() const {
    return _table_size;
  }

 private:
  
  vector<unsigned char> _hash_table;
  unsigned int _table_size;
  unsigned int _version;
  
#if COMPILE_FOR == NETWORK
  friend class boost::serialization::access;

  // serialization code
  template<class Archive>
  void serialize(Archive & ar, const unsigned int version) {
      ar & _hash_table;
      ar & _table_size;
      ar & _version;
  }
#endif //COMPILE_FOR == NETWORK
};

#endif
