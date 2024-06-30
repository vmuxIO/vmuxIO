#ifdef DEBUG_LAN
#define DEBUG_LOG_LAN(args) std::cout << args << logger::endl;
#else
#define DEBUG_LOG_LAN(args)
#endif

#ifdef DEBUG_PTP
#define DEBUG_LOG_PTP(args) std::cout << args << std::endl;
#else
#define DEBUG_LOG_PTP(args)
#endif

#ifdef DEBUG_DEV
#define DEBUG_LOG_DEV(args) std::cout << args << logger::endl;
#else
#define DEBUG_LOG_DEV(args)
#endif
