#ifdef DEBUG_LAN
#define DEBUG_LOG_LAN(args) std::cout << args << logger::endl;
#else
#define DEBUG_LOG_LAN(args)
#endif

#ifdef DEBUG_PTP
#define DEBUG_LOG_PTP(args) std::cout << args << logger::endl;
#else
#define DEBUG_LOG_PTP(args)
#endif

#ifdef DEBUG_DEV
#define DEBUG_LOG_DEV(args) std::cout << args << logger::endl;
#else
#define DEBUG_LOG_DEV(args)
#endif



// Helper macros to simplify switch case statements. CASE_N duplicates the provided statement N times, and sets the INDEX integer to the current index.
#define CASE(base, stmt) \
      case (base): {stmt; break;}


#define CASE_2(base, stmt) \
      case (base(0)): {uint32_t INDEX = 0; stmt; break;} \
      case (base(1)): {uint32_t INDEX = 1; stmt; break;}


#define CASE_4(base, offset, stmt) \
      case (base(0 + offset)): {uint32_t INDEX = 0; stmt; break;} \
      case (base(1 + offset)): {uint32_t INDEX = 1; stmt; break;} \
      case (base(2 + offset)): {uint32_t INDEX = 2; stmt; break;} \
      case (base(3 + offset)): {uint32_t INDEX = 3; stmt; break;}



#define CASE_6(base, offset, stmt) \
      case (base(0 + offset)): {uint32_t INDEX = 0; stmt; break;} \
      case (base(1 + offset)): {uint32_t INDEX = 1; stmt; break;} \
      case (base(2 + offset)): {uint32_t INDEX = 2; stmt; break;} \
      case (base(3 + offset)): {uint32_t INDEX = 3; stmt; break;} \
      case (base(4 + offset)): {uint32_t INDEX = 4; stmt; break;} \
      case (base(5 + offset)): {uint32_t INDEX = 5; stmt; break;}

