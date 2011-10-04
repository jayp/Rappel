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

using namespace std;

/* String containing name the program is called with.  */
const char *program_name;

// static const struct option longopts[] =
// {
//   { "output", required_argument, NULL, 'o' },
//   { "help", no_argument, NULL, 'h' },
//   { NULL, 0, NULL, 0 }
// };

static void print_help (void);
static void print_version (void);

// the topology itself
// Topology* __topology ; // deprecated
ASTopology* as_topology ;

int
main (int argc, char *argv[])
{
  int optc;
  int lose = 0;
  unsigned int networks ;
  unsigned int i,j ;
  const char *output_file = "fastpair-new" ;

  program_name = argv[0];

  // Etienne: for simplicity i use static in-code defined paramaters

  // while ((optc = getopt_long (argc, argv, "oh", longopts, NULL)) != -1) {
//     switch (optc)
//       {
// 	/* --help exit immediately,
// 	   per GNU coding standards.  */
//       case 'o':
//         output_file = optarg;
//         break;
//       case 'h':
//         print_help ();
//         exit (EXIT_SUCCESS);
//         break;
//       default:
//         lose = 1;
//         break;
//       }
//   }

//   if (lose || optind < argc || networks == -1) {
//     {
//       /* Print error message and exit.  */
//       if (optind < argc)
//         fprintf (stderr, "%s: extra operand: %s\n",
// 		 program_name, argv[optind]);
//       fprintf (stderr, "Try `%s --help' for more information.\n",
//                program_name);
//       exit (EXIT_FAILURE);
//     }
//   }
  
  load_config(argc,argv); 
  assert(globops.is_initialized());

  //__topology = Topology::get_instance();
  as_topology = ASTopology::get_instance();
  assert(as_topology != NULL);
  
  networks = as_topology->get_num_networks();
  assert(networks > 0);
  
  // now do the job : for all 
  printf("Generating all pair delays for %u networks (in %s)\n",
	 networks,
	 output_file);
  printf("WARNING : file size is %g GB.\n",
	 (double)((networks*(networks+1)/2.0*sizeof(unsigned char))/
		  (1024*1024*1024)));
  
  std::ofstream outfile;
  outfile.open(output_file,ios::out | ios::binary); 
  
  cerr << "generating delay pairs for " << networks << " networks ..." << endl ;
  
  for (i = 0; i < networks; i++) {
    vector<Clock> costs = as_topology->get_all_routes_from(i);
    cerr << "Generating costs for network " << i << " ..." << endl ;
    for (j = 0; j <= i; j++) {
      unsigned int cost_in_ms = costs[j] / MILLI_SECONDS;
      unsigned char small_cost = cost_in_ms < 255 ? cost_in_ms : 255;
      outfile.write((char *)(&small_cost),(long)sizeof(unsigned char));
    }
  }
  
  outfile.close();

  exit (EXIT_SUCCESS);
}

/* Print help info.  */

static void
print_help (void)
{
  printf("Usage: %s [OPTIONS]...\n", program_name);
  fputs ("Generate all pair route delays for rappel topology.\n", stdout);
  puts ("");
  fputs ("-h, --help          display this help and exit\n", stdout);
  fputs ("-o, --output=TEXT   set output file (will be large)\n", stdout);
}
