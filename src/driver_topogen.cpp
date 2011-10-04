#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <string>
#include "rappel_common.h"
#include "transport.h"

const unsigned int MAX_NODES = 3920;
const unsigned int MAX_PAIRS_TO_EXAMINE = 10000;

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

int main(int argc, char** argv) {

  load_config(argc, argv);
  assert(globops.is_initialized());

  // initialize random number generator
  RNG* rng = RNG::get_instance();
  assert(rng != NULL);

  Topology* topology = Topology::get_instance();

  for (unsigned int i = 0; i < MAX_NODES; i++) {
    //printf("ADDDING NODE %u\n", i);
    // add node to physical topology
    topology->add_node(i);
  }

  for (unsigned int i = 0; i < MAX_PAIRS_TO_EXAMINE; i++) {

    unsigned int n1 = rng->rand_uint(0, MAX_NODES - 1);
    unsigned int n2 = rng->rand_uint(0, MAX_NODES - 1);
    if (n1 == n2) n2++;

    double delay = topology->network_delay(n1, n2) / (double) MILLI_SECONDS;
    printf("%u %u %0.4f\n", n1, n2, delay);
  }
  
  return 0;
}
