#ifndef _RAPPEL_COMMON_H
#define _RAPPEL_COMMON_H

#define SIMULATOR 100
#define NETWORK 101
#if COMPILE_FOR != SIMULATOR && COMPILE_FOR != NETWORK
#error ERROR: Macro variable COMPILE_FOR must be either SIMULATOR or NETWORK
#endif

#include <string>
#include <vector>
#include <cmath>
#include <boost/random.hpp>
#if COMPILE_FOR == NETWORK
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include "asio.hpp"
#endif

using namespace std;

/**********************************************************************/
// global typedefs
typedef long long Clock;
typedef unsigned int event_id_t;
class node_id_t;
typedef node_id_t feed_id_t;
typedef unsigned int debug_seq_t;

/**********************************************************************/
// hard constants

const Clock MICRO_SECOND = 1;
const Clock MICRO_SECONDS = MICRO_SECOND;

const Clock MILLI_SECOND = 1000 * MICRO_SECOND;
const Clock MILLI_SECONDS = MILLI_SECOND;

const Clock SECOND = 1000 * MILLI_SECOND;
const Clock SECONDS = SECOND;

const Clock MINUTE = 60 * SECOND;
const Clock MINUTES = MINUTE;

const Clock HOUR = 60 * MINUTE;
const Clock HOURS = HOUR;

const Clock DAY = 24 * HOUR;
const Clock DAYS = DAY;

const Clock WEEK = 7 * DAY;
const Clock WEEKS = WEEK;

const unsigned int MAX_STRING_LENGTH = 8192 ;

// AS topology
const Clock INF_DELAY = 120 * SECONDS;
const Clock DEF_CLEANUP_BUFFER = 30 * MINUTES; // TODO -- variablize

// transport/rappel
const unsigned int DEFAULT_RAPPEL_PORT = 2129;

// event
const event_id_t NO_SUCH_EVENT_ID = 8;
const unsigned int FINAL_SEQ_NUM = 999999;

// stats collection stuff
const Clock SESSION_PAD_TIME = 5 * MINUTES; // TODO -- variablize


/**********************************************************************/
// commonly used type: RNG

typedef boost::minstd_rand base_generator_type;

class RNG {
public:
  RNG(unsigned int);

  // singleton
  static RNG* get_instance();

  unsigned int rand_uint(unsigned int lo, unsigned int hi);
  unsigned long long rand_ulonglong(unsigned long long hi);
  unsigned int rand_uint_exp(unsigned int max, unsigned int base);

private:
  static RNG* _instance;
  base_generator_type generator;
};

/**********************************************************************/
// commonly used type: node_id_t (and related subcomponents)

#if COMPILE_FOR == SIMULATOR

typedef unsigned int rappel_address_t;
//typedef unsigned short rappel_port_t;

#elif COMPILE_FOR == NETWORK

// rappel_address_t has to be std::string because asio::ip::address is not serializable
typedef std::string rappel_address_t;
typedef unsigned short rappel_port_t;

#endif

class node_id_t {

public:
  node_id_t();
  //node_id_t::node_id_t(const node_id_t& rhs); // copy constructor
  node_id_t(const std::string& address);
#if COMPILE_FOR == SIMULATOR
  node_id_t(const unsigned int address);
#elif COMPILE_FOR == NETWORK
  node_id_t(const std::string& address, const rappel_port_t& port);
  node_id_t(asio::ip::tcp::endpoint remote);
#endif

  // accessors
  rappel_address_t get_address() const {
    return _address;
  }
#if COMPILE_FOR == NETWORK
  rappel_port_t get_port() const {
    return _port;
  }
#endif
  std::string str() const;

  // simple mutators
  void set_address(const rappel_address_t& address) {
    _address = address;
  }
#if COMPILE_FOR == NETWORK
  void set_port(const rappel_port_t& port) {
    _port = port;
  }
#endif

  // operators
  bool operator==(const node_id_t& rhs) const {
#if COMPILE_FOR == SIMULATOR
    return (_address == rhs._address);
#elif COMPILE_FOR == NETWORK
    return (_address == rhs._address && _port == rhs._port);
#endif
  }
  bool operator!=(const node_id_t& rhs) const {
#if COMPILE_FOR == SIMULATOR
    return (_address != rhs._address);
#elif COMPILE_FOR == NETWORK
    return (_address != rhs._address || _port != rhs._port);
#endif
  }
  bool operator<(const node_id_t& rhs) const {
#if COMPILE_FOR == SIMULATOR
    return (_address < rhs._address);
#elif COMPILE_FOR == NETWORK
    return (_address < rhs._address || (_address == rhs._address && _port < rhs._port));
#endif
  }
  bool operator>(const node_id_t& rhs) const {
#if COMPILE_FOR == SIMULATOR
    return (_address > rhs._address);
#elif COMPILE_FOR == NETWORK
    return (_address > rhs._address || (_address == rhs._address && _port > rhs._port));
#endif
  }

private:
  rappel_address_t _address;
#if COMPILE_FOR == NETWORK
  rappel_port_t _port;
#endif

#if COMPILE_FOR == NETWORK
  friend class boost::serialization::access;

  template<class Archive>
  void serialize(Archive& ar, const unsigned int version) {

    // serialize local objects
    ar & _address;
    ar & _port;
  }
#endif //COMPILE_FOR == NETWORK
};

extern const node_id_t NO_SUCH_NODE;

/**********************************************************************/
// commonly used type: IOService (for network implementation only)

#if COMPILE_FOR == NETWORK
class IOService {
public:

  asio::io_service& get_ios() {
    return _ios;
  }
  void run() {
    _ios.run();
  }
  
  // singleton
  static IOService* get_instance();

private:
  static IOService* _instance;
  asio::io_service _ios;
};
#endif // COMPILE_FOR

/**********************************************************************/
// global options: really inside config.h/cpp
#include "config.h"
extern Options globops;

#endif // _RAPPEL_COMMON_H
