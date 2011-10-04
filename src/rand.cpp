#include <boost/random/mersenne_twister.hpp>
#include <boost/random/normal_distribution.hpp>
#include <boost/random/variate_generator.hpp>
#include <ctime>

int main()
{
    boost::mt19937 engine(static_cast<long unsigned int >(clock()));
    boost::normal_distribution<double > generator;
    boost::variate_generator<boost::mt19937, boost::normal_distribution <double> > binded(engine, generator);


    std::cout << binded() << std::endl;
    std::cout << binded() << std::endl;
    std::cout << binded() << std::endl;
    std::cout << binded() << std::endl;
    std::cout << binded() << std::endl;

return 0;
}
