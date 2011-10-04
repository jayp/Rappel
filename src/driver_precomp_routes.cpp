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

//const short maxsize = 8 ;
const char *output_file = "ROUTES";
ofstream output ;

ASTopology* __as_topology ;

/* String containing name the program is called with.  */
const char *program_name;

// static const struct option longopts[] =
// {
//   { "output", required_argument, NULL, 'o' },
//   { "help", no_argument, NULL, 'h' },
//   { "maxlen", required_argument, NULL, 'm' },
//   { NULL, 0, NULL, 0 }
// };

static void print_help (void);

int
main (int argc, char *argv[])
{
  short maxsize = 20 ;

  program_name = argv[0];
  int optc;
  unsigned int networks ;
  unsigned int i,j ;
  int lose = 0;

  load_config(argc,argv); 
  assert(globops.is_initialized());

  assert(sizeof(unsigned short) == 2); // i do not support 64 bits here

  // hardcoded parameters because of incompatibilities with load_config
//   while ((optc = getopt (argc, argv, "o:hm:")) != -1) {
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
//       case 'm':
// 	maxsize = atoi(optarg);
// 	break ;
//       default:
//         lose = 1;
//         break;
//       }
//   }

//   cerr << output_file << endl 
//        << maxsize << endl 
//        << endl ;

//   if (lose || (output_file==NULL && maxsize != -1)) {
//     if (optind < argc) {
//       fprintf (stderr, "%s: extra operand: %s\n",
// 	       program_name, argv[optind]);
//     }
//     fprintf (stderr, "Try `%s --help' for more information.\n",
//                program_name);
//     exit (EXIT_FAILURE);
//   }
  
  __as_topology = ASTopology::get_instance("topology");
  assert(__as_topology != NULL);
  
  cerr << "AS Topology instance created." << endl ;

  networks = __as_topology->get_num_networks();
  assert(networks > 0);
  
  assert (maxsize == -1 || maxsize > 0);
  
  cout << "Generating all pair delays for "
       << networks << " networks (in "
       << output_file << ")" << endl ;
  
  if (maxsize == -1) {
    cout << "calculating max size needed only" << endl ;
  } else {
    cout << "generating all pairs routes" << endl
	 << "WARNING : file size is " 
	 << (double)(((double)networks*(double)networks*(double)sizeof(unsigned short)*(double)maxsize)/
		     (double)((double)1024*(double)1024*(double)1024))
	 << " GB." << endl ;
    // open output file
    output.open(output_file,ios::binary);
    assert (output);
  }

  bool routes_were_truncated = false ;

  map <unsigned short,unsigned short> route_size_dist ;
  unsigned int bad_routes = 0 ; 

  for (i=0; i<networks; i++) {

    cout << "Generating routes for network " << i << " ..." << endl ;
 
    vector <pair< bool, vector <unsigned short> > > routes 
      = __as_topology->get_all_routes_details_from (i);

    // calculate the distribution of sizes
    vector <pair< bool, vector <unsigned short> > >::iterator 
      it_routes (routes.begin());
    for (;it_routes != routes.end(); it_routes++) {

      if (it_routes->first) {
	// no route exist
	bad_routes++ ;
	if (maxsize != -1) {
	  for (unsigned int j (0); j < maxsize ; j ++) {
	    output << static_cast<unsigned short>(0xFFFF);
	  }
	}
      } else {
	unsigned short s = it_routes->second.size();

	//      cerr << "size of route is " << s << endl ;
	//       cerr << "route details : " ;
	//       for (vector<unsigned short>::iterator it_toto (it_routes->begin());
	// 	   it_toto != it_routes->end(); it_toto++) {
	// 	cerr << *it_toto << " " ;
	//       }
	//       cerr << endl ;

	map <unsigned short,unsigned short>::iterator it_dist 
	  (route_size_dist.find(s));
	if (it_dist == route_size_dist.end() ) {
	  route_size_dist[s] = 1 ;
	} else {
	  // 	cerr << it_dist->second+1 << " routes of size " << s << endl ;
	  it_dist->second = it_dist->second + 1 ;
	}
      
	if (maxsize != -1) {
	  // output exactly the routes with no more than maxsize elements
	  for (unsigned int j (0); j < maxsize ; j ++) {
	    if (j < s) {
	      unsigned short elem = static_cast<unsigned short>(it_routes->second[j]);
	      output.write((char *)&elem,sizeof(unsigned short));
	      //output << static_cast<unsigned short>(it_routes->second[j]);
	    } else {
	      // padd with 111...111
	      unsigned short elem = 0xffff;
	      output.write((char *)&elem,sizeof(unsigned short));
	      //output << static_cast<unsigned short>(0xFFFF);
	    }
	  } 
	  if (s > maxsize) {
	    routes_were_truncated = true ;
	  }
	}
      }
    }
  }  

  // output the distribution of route sizes
  map <unsigned short,unsigned short>::iterator it_dist 
    (route_size_dist.begin());
  cout << "broken routes : " << bad_routes << endl ;
  for (;it_dist != route_size_dist.end(); it_dist++) {
    cout << "routes of size " << it_dist->first
	 << " : " << it_dist->second 
	 << endl ;
  }

  if (routes_were_truncated) {
    cout << "WARNING: maxsize =" 
	 << maxsize
	 << " is too small." << endl
	 << "Some routes were truncated." 
	 << endl ;
  }

  output.close();

  exit (EXIT_SUCCESS);
}

/* Print help info.  */

static void
print_help (void)
{
  printf("Usage: %s [OPTIONS]...\n", program_name);
  fputs ("Generate all pair route for rappel topology.\n", stdout);
  puts ("");
  fputs ("-h, --help          display this help and exit\n", stdout);
  fputs ("-o, --output=TEXT   set output file (will be large)\n", stdout);
  fputs ("-m, --maxlen=UINT   max. length of a route / -1 only computes routes\n", stdout);
}
