#ifndef CORE_RETURN_CODES_H
#define CORE_RETURN_CODES_H

#ifdef __cplusplus
extern "C" {
#endif


typedef enum {

    // Success codes
    RC_OK = 0,      // operation successful, no additional info
    RC_OK_NO_DATA,        // operation successful, but no data available
    RC_OK_TRUE,           // operation successful, boolean true
    RC_OK_FALSE,          // operation successful, boolean false

    // Failure codes
    RC_TIMEOUT,        // operation timed out
    RC_BUSY,           // transient lock/contention
    RC_RETRY_LIMIT,    // retries exhausted
    RC_INVALID_ARG,    // bad input/config
    RC_NO_RESOURCE,    // buffer/memory/resource exhausted
    RC_PERM_DENIED,    // policy/permission denied
    RC_HW_FAULT,       // peripheral/bus fault
    RC_INTEGRITY_FAIL, // CRC/auth/check failed
    RC_DATA_RANGE,     // value out of allowed range
    RC_ERROR,          // generic error, unspecified failure
    RC_FATAL           // unrecoverable; safe-state

}return_code_t;

#define RC_SUCCESS(rc) ((rc) <= RC_OK_FALSE)
#define RC_FAILURE(rc) ((rc) >= RC_TIMEOUT)

#ifdef __cplusplus
}
#endif

#endif // CORE_RETURN_CODES_H