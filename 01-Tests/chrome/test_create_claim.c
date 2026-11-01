#include "test_platform.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Use the public production declaration as well as the unchanged function body. */
/* PRODUCTION_DECLARATION */
/* PRODUCTION_FUNCTION */

typedef SECURITY_STATUS (WINAPI *CLAIM_API)(NCRYPT_KEY_HANDLE, NCRYPT_KEY_HANDLE,
    DWORD, NCryptBufferDesc *, PBYTE, DWORD, DWORD *, DWORD);

int main(void)
{
    struct { BYTE Before[8]; BYTE Value[16]; BYTE After[8]; } output, output_before;
    struct { DWORD Before; DWORD Value; DWORD After; } result, result_before;
    BYTE input[16], input_before[16];
    NCryptBuffer parameter, parameter_before;
    NCryptBufferDesc parameters, parameters_before;
    CLAIM_API claim;
    SECURITY_STATUS status;
    unsigned int iteration;

    assert(sizeof(DWORD) == 4 && sizeof(SECURITY_STATUS) == 4);
    assert(sizeof(NCRYPT_KEY_HANDLE) == sizeof(void *));
    claim = NCryptCreateClaim; /* Incompatible public ABI is a compile error (/WX). */
    memset(&output, 0xa5, sizeof(output));
    memset(&result, 0x5a, sizeof(result));
    memset(input, 0x3c, sizeof(input));
    memset(&parameter, 0, sizeof(parameter));
    memset(&parameters, 0, sizeof(parameters));
    parameter.cbBuffer = sizeof(input);
    parameter.BufferType = 0x11223344;
    parameter.pvBuffer = input;
    parameters.ulVersion = 0;
    parameters.cBuffers = 1;
    parameters.pBuffers = &parameter;
    output_before = output;
    result_before = result;
    memcpy(input_before, input, sizeof(input));
    parameter_before = parameter;
    parameters_before = parameters;

    for (iteration = 0; iteration < 1000; ++iteration) {
        status = claim((NCRYPT_KEY_HANDLE)0x1111, (NCRYPT_KEY_HANDLE)0x2222,
            3, &parameters, output.Value, sizeof(output.Value), &result.Value, 0x98);
        assert(status == NTE_NOT_SUPPORTED && status != 0);
        assert(memcmp(&output, &output_before, sizeof(output)) == 0);
        assert(memcmp(&result, &result_before, sizeof(result)) == 0);
        assert(memcmp(input, input_before, sizeof(input)) == 0);
        assert(memcmp(&parameter, &parameter_before, sizeof(parameter)) == 0);
        assert(memcmp(&parameters, &parameters_before, sizeof(parameters)) == 0);
    }
    /* An unsupported size query does not publish an invented size. */
    assert(claim(0, 0, 0, NULL, NULL, 0, &result.Value, 0) == NTE_NOT_SUPPORTED);
    assert(memcmp(&result, &result_before, sizeof(result)) == 0);
    assert(claim(0, 0, 0, NULL, output.Value, 0, &result.Value, 0) == NTE_NOT_SUPPORTED);
    assert(memcmp(&output, &output_before, sizeof(output)) == 0);
    assert(memcmp(&result, &result_before, sizeof(result)) == 0);
    assert(claim(0, 0, 0, NULL, NULL, 0, NULL, 0) == NTE_NOT_SUPPORTED);
    /* No caller pointer should be dereferenced on this unsupported path. */
    assert(claim((NCRYPT_KEY_HANDLE)(ULONG_PTR)-1, (NCRYPT_KEY_HANDLE)(ULONG_PTR)-2,
        0xffffffffU, (NCryptBufferDesc *)(ULONG_PTR)1, (PBYTE)(ULONG_PTR)1,
        0xffffffffU, (DWORD *)(ULONG_PTR)1, 0xffffffffU) == NTE_NOT_SUPPORTED);
    puts("NCryptCreateClaim eight-argument ABI and honest-failure/canary tests passed.");
    return 0;
}
