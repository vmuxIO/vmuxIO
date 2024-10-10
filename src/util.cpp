#include <sys/syslog.h>
#include <boost/random.hpp>
#include "util.hpp"

int LOG_LEVEL = LOG_DEBUG;

#define BOOST_NO_EXCEPTIONS
#include <boost/throw_exception.hpp>
void boost::throw_exception(std::exception const & e, boost::source_location const& src){
  die("%s: %s\n", src.to_string().c_str(), e.what());
}
void boost::throw_exception(std::exception const & e){
  die("%s\n", e.what());
}

uint64_t Util::rand() {
  boost::random::mt19937 gen;
  static boost::random::uniform_int_distribution<uint64_t> dist(0, -1);
  auto mac = dist(gen);
  return mac;
}
