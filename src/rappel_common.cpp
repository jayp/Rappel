#include "rappel_common.h"
#include "config.h"

RNG* RNG::_instance = NULL; // initialize pointer
#if COMPILE_FOR == NETWORK
IOService* IOService::_instance = NULL;
#endif


// non-primitive global variables
//const std::string ZHANG_TOPOLOGY_DATE = "20060615";
const node_id_t NO_SUCH_NODE;

node_id_t::node_id_t()
#if COMPILE_FOR == SIMULATOR
  : _address(1112223334) {
#elif COMPILE_FOR == NETWORK
  : _address("NOT.A.REAL.IP123OK"), _port(DEFAULT_RAPPEL_PORT) {
#endif
}

#if COMPILE_FOR == SIMULATOR
node_id_t::node_id_t(const unsigned int address)
 : _address(address) {
}
#endif

node_id_t::node_id_t(const std::string& address)
#if COMPILE_FOR == SIMULATOR
  {
  _address = atoi(address.c_str());
#elif COMPILE_FOR == NETWORK
  : _address(address), _port(DEFAULT_RAPPEL_PORT) {
  unsigned int colon_pos = address.find(':');
  if (colon_pos != std::string::npos) {
    _address = address.substr(0, colon_pos);
    _port = atoi(address.substr(colon_pos + 1).c_str());
  }
#endif
}

#if COMPILE_FOR == NETWORK
node_id_t::node_id_t(const std::string& address, const rappel_port_t& port)
  : _address(address), _port(port) {
}
#endif

#if COMPILE_FOR == NETWORK
node_id_t::node_id_t(asio::ip::tcp::endpoint remote)
  : _address(remote.address().to_string()), _port(remote.port()) {
}
#endif

std::string node_id_t::str() const {
  char buffer[100];

#if COMPILE_FOR == SIMULATOR
  // Etienne: modified to output only adress for simulation
  snprintf(buffer, 99, "%u", _address);
  //  snprintf(buffer, 99, "%u:%hu", _address, _port);
#elif COMPILE_FOR == NETWORK
  snprintf(buffer, 99, "%s:%hu", _address.c_str(), _port);
#endif

 return std::string(buffer);
}

/**
 * Initialize the random number generator with a seed value
 */
RNG::RNG(unsigned int seed) {

  generator.seed(seed);
}

/**
 * Generate a singleton of the random number generator
 *
 * @return the singleton class of the random number generator
 */
RNG* RNG::get_instance() {

  if (_instance == NULL) {  
    _instance = new RNG(globops.sys_seed); // create sole instance
  }

return _instance; // address of sole instance
}

/**
 * Generate a unsigned int random number -- really only supports 31 bit
 * numbers.
 *
 * @param low lower-bound of the random number (inclusive)
 * @param hi upper-bound of the random number (inclusive)
 * @return a random number between low and hi
 */
unsigned int RNG::rand_uint(unsigned int low, unsigned int hi) {

  boost::uniform_int<unsigned int> uni_dist(low, hi);
  boost::variate_generator<base_generator_type&,
    boost::uniform_int<unsigned int> > uni(generator, uni_dist);

return (unsigned int) uni();
}

/**
 * Generate a unsigned long long random number -- really only supports 62 bit
 * numbers.
 *
 * @param hi upper-bound of the random number (inclusive)
 * @return a random number between 0 and hi (both inclusive)
 */
unsigned long long RNG::rand_ulonglong(unsigned long long hi) {

  boost::uniform_int<unsigned long long> uni_dist(0, hi);
  boost::variate_generator<base_generator_type&,
    boost::uniform_int<unsigned long long> > uni(generator, uni_dist);

return (unsigned int) uni();
}

/**
 * @return 0 with the lowest probability, max with the highest probability
 */
unsigned int RNG::rand_uint_exp(unsigned int max, unsigned int base) {

  assert(base > 1);
  vector<unsigned long long> limits;

  unsigned long long new_max = 1;
  for (unsigned int i = 0; i <= max; i++) {
    if (limits.size() == 0) {
      limits.push_back(new_max);
    } else {
      new_max *= base;
      limits.push_back(limits[limits.size() - 1] + new_max);
    }
  }

  unsigned long long rand_num = rand_ulonglong(limits[limits.size() -1]);
  assert(rand_num <= limits[limits.size() - 1]);

  for (unsigned int i = 0; i < limits.size(); i++) {
    if (rand_num <= limits[i]) {
      return i;
    }
  }

  assert(false);
  return 0; 
}

#if COMPILE_FOR == NETWORK
IOService* IOService::get_instance() {

  if (_instance == NULL) {
    _instance = new IOService(); // create sole instance
    assert(_instance != NULL);
  }
      
  return _instance; // address of sole instance
}
#endif
