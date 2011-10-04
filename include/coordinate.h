#ifndef _COORDINATE_H
#define _COORDINATE_H

#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <vector>
#include <utility>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <string>
#include <boost/random.hpp>
#include "rappel_common.h"
#if COMPILE_FOR == NETWORK
#include <boost/serialization/vector.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#endif

using namespace std ;

class Coordinate {
public:

  // constructor(s)
  Coordinate();
  void init();

  // accessors
  std::string to_string(bool app_coords = true) const;
  double get_distance(const Coordinate& c) const;
  unsigned int get_version() const {
    return _version;
  }
  double get_lambda() const {
    return _lambda;
  }
  vector<double>  get_app_coords() const {
    return _app_coords;
  }
  // print sys, app, virt coords in this order on one line separated by tabulations
  void to_stream(ofstream &, const string &) const;

  // statuc funcitons
  static void print_desc(ofstream &, const string &);

  // mutators
  void set_app_coords(const vector<double>& app_coords, const unsigned int version) {
    assert(app_coords.size() == globops.nc_dims);
    assert(version >= _version);
    _app_coords = app_coords;
    _version = version;
  }
  bool update(const Coordinate& c, double latency, bool force_sync);
  void sync();

private:
  double _lambda;
  vector<double> _sys_coords; // sys coordinates  
  vector<double> _app_coords; // app coordinates  
  vector<unsigned int> _virt_coords; // virt coordinates  
  unsigned int _version;

  // utility singleton
  RNG* _rng;
   
  // utility functions
  double vector_norm(const vector<double>& a) const;
  double vector_dist(const vector<double>& a, const vector<double>& b) const;

#if COMPILE_FOR == NETWORK
  friend class boost::serialization::access;

  // serialization code
  template<class Archive>
  void serialize(Archive & ar, const unsigned int version) {

     // ar & _lambda;
     // ar & _sys_coords // NO NEED TO SEND THIS
     ar & _app_coords;
     // ar & _virt_coords; // NO NEED TO SEND THIS
     ar & _version;
  }
#endif //COMPILE_FOR == NETWORK
};

#endif // _COORDINATE_H
