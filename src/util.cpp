#include <sys/syslog.h>
#include "util.hpp"

int LOG_LEVEL = LOG_DEBUG;

#include <boost/throw_exception.hpp>
void boost::throw_exception(std::exception const & e, boost::source_location const& src){
  die("%s: %s\n", src.to_string().c_str(), e.what());
}
