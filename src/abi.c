#include <nocturne/abi.h>

uint32_t nt_abi_version(void)
{
    return NT_ABI_VERSION;
}

const char *nt_result_name(int32_t result)
{
    switch (result) {
    case NT_RESULT_OK: return "NT_RESULT_OK";
    case NT_RESULT_INVALID_ARGUMENT: return "NT_RESULT_INVALID_ARGUMENT";
    case NT_RESULT_OUT_OF_RANGE: return "NT_RESULT_OUT_OF_RANGE";
    case NT_RESULT_ALLOCATION_FAILED: return "NT_RESULT_ALLOCATION_FAILED";
    case NT_RESULT_INSUFFICIENT_BUFFER: return "NT_RESULT_INSUFFICIENT_BUFFER";
    case NT_RESULT_STALE_REFERENCE: return "NT_RESULT_STALE_REFERENCE";
    case NT_RESULT_WRONG_PHASE: return "NT_RESULT_WRONG_PHASE";
    case NT_RESULT_UNSUPPORTED_CAPABILITY: return "NT_RESULT_UNSUPPORTED_CAPABILITY";
    case NT_RESULT_TRANSPORT_FAILED: return "NT_RESULT_TRANSPORT_FAILED";
    case NT_RESULT_CALLBACK_FAILED: return "NT_RESULT_CALLBACK_FAILED";
    default: return NULL;
    }
}
