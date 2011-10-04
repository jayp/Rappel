/**
 * Route analyzer : 
 * - takes as argument the file where 
 */
#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <fstream>
#include <map>

#define ELEM_PER_ROUTE 16

using namespace std;

const unsigned int num_networks = 28541 ;
const string routes_filename = "/local/eriviere/ROUTES";

int main (int argc, char ** argv) {
  if (argc != 2) {
    cerr << "hey dude there can be only one argument: the stats_routes_all filename" << endl ;
    exit(1);
  }
  string input_filename (argv[1]);

  ofstream oo ("tototo",ios::binary);

  unsigned short ii = 0xffff;

  oo.write((char *)&ii,sizeof(unsigned short));
  oo.write((char *)&ii,sizeof(unsigned short));

//   for (unsigned short i = 1 ; i <= 0x1000 ; i *= 2){
//     oo.write((char *)i,sizeof(unsigned short));
//     //    oo << i ;
//   }
// /
    //   oo << static_cast<unsigned int>(0xffff);
//   oo << static_cast<unsigned int>(0xFFFF);
  oo.close();
  exit(1);

  ifstream input ;
  input.open(input_filename.c_str());

  ifstream routes ;
  routes.open(routes_filename.c_str(),ios::binary);

  assert (input && routes);

  map <unsigned short, unsigned int> router_costs ;

  while (!input.eof()) {
    unsigned short src ;
    unsigned short dst ;
    input >> src ;
    input >> dst ;
    unsigned int used ;
    input >> used ;

    src = 2;
    dst = 2 ;

    // find the element in the routes file
    unsigned long long s = ((src * num_networks)+ dst) 
      * sizeof(unsigned short) 
      * ELEM_PER_ROUTE ;
    
    char cost[ELEM_PER_ROUTE*sizeof(unsigned short)];
    
    routes.seekg(s);
    routes.read((char *)cost, ELEM_PER_ROUTE*sizeof (unsigned short));

    for (unsigned int i(0); i < ELEM_PER_ROUTE; i++) {
      unsigned short *elem = (unsigned short *) cost + (i*sizeof (unsigned short));
      cout << *elem << " ";
    }
    cout << endl ;
    cout << "ushort :" << sizeof(unsigned short) << endl;
    cout << "0xffff " << static_cast<unsigned short>(0xFFFF) << endl ;
  }
  input.close();
}
