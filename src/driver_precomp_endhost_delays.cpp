#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <utility>
#include <algorithm>
/* Use POSIX headers. */
#include <getopt.h>
#include <unistd.h>
#include <iostream>
#include <fstream>

#include "topology.h"
#include "as_topology.h"

using namespace std;

const int NUM_HOSTS = 5000;

//ASTopology* as_topology = ASTopology::get_instance();

int
main (int argc, char *argv[])
{
  load_config(argc, argv); 
  assert(globops.is_initialized());

  printf("globops.sys_seed=%u\n", globops.sys_seed);

  RNG* rng = RNG::get_instance();
  assert(rng != NULL);

  Topology* topology = Topology::get_instance();
  assert(topology != NULL);
  
  // now do the job : for all 
  printf("Generating delay matrix for %d endhosts\n", NUM_HOSTS);

  for (int i = 0; i < NUM_HOSTS; i++) {
    topology->add_node(i);
  }
  
  for (int i = 0; i < NUM_HOSTS; i++) {
    for (int j = 0; j < NUM_HOSTS; j++) {
      if (j > 0) {
        printf(" ");
      }
      if (i != j) {
        unsigned int delay = topology->network_delay_native(i, j);
	printf("%u", delay);
      } else {
	printf("0");
      }
    }
    printf("\n");
  }
  
  exit (EXIT_SUCCESS);
}
