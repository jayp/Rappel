#include <boost/lexical_cast.hpp>
#include "coordinate.h"

using namespace std;

Coordinate::Coordinate() {

  _rng = RNG::get_instance();
  assert(_rng != NULL);

  assert(globops.is_initialized());
  // lambda set to init value
  _lambda = globops.nc_lambda_init;

  // set up default coords: start off at random
  for (unsigned int i = 0; i < globops.nc_dims; i++) {

    _sys_coords.push_back(0);
    _app_coords.push_back(0);

    unsigned int c = _rng->rand_uint(0, 100); // between 0 and 100 millisecond
    _virt_coords.push_back(c);
  }

  // initial version of the app coords
  _version = 0;
}

void Coordinate::init() {
  assert(_version == 0);
  _version = 1;
}

std::string Coordinate::to_string(bool app_coords) const {
 
  std::string ret = "[v=" + boost::lexical_cast<std::string>(_version) + "] <";

  for (unsigned int i = 0; i < globops.nc_dims; i++) {
    if (i != 0) {
      ret += ", ";
    }

    double c = app_coords ? _app_coords[i] : _sys_coords[i];
    ret += boost::lexical_cast<std::string>(c);
  }
  ret += ">";

  return ret;
}

/**
 * returns the euclidean distance between this coordinate and a second coord
 * TODO: do we need to copy the coordinate ? send by pointer ?
 *       it is send by reference, it is not a copy (local renaming)
 */
double Coordinate::get_distance(const Coordinate& c) const {

  assert(_version > 0);
  assert(c._version > 0);

  double dist = vector_dist(_app_coords, c._app_coords);

#ifdef DEBUG2
  printf("get_distance():\n");
  printf("my coords: "); print();
  printf("other coords: "); c.print();
  printf("distance: %0.3f\n", dist);
#endif // DEBUG2

  return dist;
}

/**
 * update the network coordinate in respect to a new measured coordinate
 */
bool Coordinate::update(const Coordinate& c, double latency, bool force_sync) {

  assert(latency > 0.0);
  assert(_version > 0);

#ifdef USE_CONSTANT_NCS
  //_version++;
  return false;

#endif // USE_CONSTANT_NCS

  // calculate unit vector towards other host
  vector<double> dir;
  for (unsigned int i = 0; i < globops.nc_dims; i++) {
    // use foriegn app coords, but local sys coords
    dir.push_back(c._app_coords[i] - _sys_coords[i]);
  }
  
  double dir_length = vector_norm(dir);

  // if both c._app_coords and (local) _sys_coords are the same, the node
  // should move in a random direction
  while (dir_length == 0) {
    for (unsigned int i = 0; i < globops.nc_dims; i++) {
      dir[i] = _rng->rand_uint(0, 100); // between 0 and 100 milliseconds
    }
    dir_length = vector_norm(dir);
  }
  
  for (unsigned int i = 0; i < globops.nc_dims; i++) {
    assert(dir_length > 0);
    dir[i] /= dir_length;
  }

  // calculate distance to rest position of actual vector
  double diff = vector_dist(c._app_coords, _sys_coords) - latency;

  // reduce lambda at each sample, but no more than a minimum
  _lambda = min(globops.nc_lambda_min, _lambda - globops.nc_lambda_decr);

  // apply the force to modify our coordinate
  for (unsigned int i = 0; i < globops.nc_dims; i++) {
    _sys_coords[i] += dir[i] * (diff * _lambda);
    // assert(_sys_coords[i] > 0.0); NOT TRUE. can be negative.
  }

  // TODO: change app coords here if they differ from sys coords by more than
  // some pre-defined hard delta?
  if (force_sync || vector_dist(_sys_coords, _app_coords) >= globops.nc_sys_delta) {
    sync();
    return true;
  }

  return false;
}

double Coordinate::vector_norm(const vector<double>& a) const {
  double sum = 0.0;
  for (unsigned int i = 0; i < globops.nc_dims; i++) {
    sum += pow(a[i], 2);
  }
  assert(sum >= 0);
  return (sqrt(sum));
}

double Coordinate::vector_dist(const vector<double>& a, 
  const vector<double>& b) const {
  	
  double sum = 0.0;
  for (unsigned int i = 0; i < globops.nc_dims; i++) {
    sum += pow(a[i] - b[i], 2);
  }
  assert(sum >= 0.0);
  return sqrt(sum);
}

void Coordinate::sync() {

  assert(_version > 0);
  _app_coords = _sys_coords;
  _version++;
}

/**
 * outputs the coordinate on the given stream
 */
void Coordinate::to_stream(ofstream & out,const string& sep) const {

  for (vector<double>::const_iterator it_coord = _sys_coords.begin();
    it_coord != _sys_coords.end(); it_coord++) {

    out << (*it_coord) << sep;
  }

  for (vector<double>::const_iterator it_coord (_app_coords.begin());
    it_coord != _app_coords.end(); it_coord++) {

    out << (*it_coord) << sep;
  }
  
  for (vector<unsigned int>::const_iterator it_coord (_virt_coords.begin());
    it_coord != _virt_coords.end(); it_coord++) {

    out << (*it_coord) << sep;
  }
}
/**
 * outputs stream description of the coordinates
 */
void Coordinate::print_desc(ofstream & out, const string& sep) {

  for (unsigned int i = 0; i < globops.nc_dims; i++) {
    out << "S" << i << sep;
  }
  for (unsigned int i = 0; i < globops.nc_dims; i++) {
    out << "A" << i << sep;
  }
  for (unsigned int i = 0; i < globops.nc_dims; i++) {
    out << "V" << i << sep;
  }
}
