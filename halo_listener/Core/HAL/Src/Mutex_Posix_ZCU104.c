#include "../../Core_Config.h"
#ifdef __TARGET_PLATFORM_POSIX__
#include <pthread.h>
pthread_mutex_t core_critical_section_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif