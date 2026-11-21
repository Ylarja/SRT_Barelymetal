#define TEST_ENABLED 0

#if TEST_ENABLED == 1

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../Include/Ring_Buffer.h"

/* -------------------------------------------------
 * Test configuration
 * ------------------------------------------------- */
#define STORAGE_SIZE   4   /* must be power of two */

/* -------------------------------------------------
 * Tests: Initialization
 * ------------------------------------------------- */
static void Test_Init(void)
{
    ring_buffer_t rb;
    uint16_t storage[STORAGE_SIZE];

    assert(Ring_Buffer_Init(NULL, storage, STORAGE_SIZE, sizeof(uint16_t)) == RC_INVALID_ARG);
    assert(Ring_Buffer_Init(&rb, NULL, STORAGE_SIZE, sizeof(uint16_t)) == RC_INVALID_ARG);
    assert(Ring_Buffer_Init(&rb, storage, 0, sizeof(uint16_t)) == RC_DATA_RANGE);
    assert(Ring_Buffer_Init(&rb, storage, STORAGE_SIZE, 0) == RC_DATA_RANGE);
    assert(Ring_Buffer_Init(&rb, storage, 3, sizeof(uint16_t)) == RC_DATA_RANGE); /* not power of two */
    assert(Ring_Buffer_Init(&rb, storage, STORAGE_SIZE, sizeof(uint16_t)) == RC_OK);
}

/* -------------------------------------------------
 * Tests: Empty / Full
 * ------------------------------------------------- */
static void Test_Empty_Full(void)
{
    ring_buffer_t rb;
    uint16_t storage[STORAGE_SIZE];
    uint16_t value = 0x1234;

    Ring_Buffer_Init(&rb, storage, STORAGE_SIZE, sizeof(uint16_t));

    assert(Ring_Buffer_Is_Empty(NULL) == RC_INVALID_ARG);
    assert(Ring_Buffer_Is_Full(NULL) == RC_INVALID_ARG);

    assert(Ring_Buffer_Is_Empty(&rb) == RC_OK_TRUE);
    assert(Ring_Buffer_Is_Full(&rb) == RC_OK_FALSE);

    /* capacity - 1 usable slots */
    for (uint8_t i = 0; i < STORAGE_SIZE - 1; i++)
    {
        assert(Ring_Buffer_Push(&rb, &value) == RC_OK);
    }

    assert(Ring_Buffer_Is_Empty(&rb) == RC_OK_FALSE);
    assert(Ring_Buffer_Is_Full(&rb) == RC_OK_TRUE);
    assert(Ring_Buffer_Push(&rb, &value) == RC_NO_RESOURCE);
}

/* -------------------------------------------------
 * Tests: Push / Pop single (FIFO)
 * ------------------------------------------------- */
static void Test_Push_Pop(void)
{
    ring_buffer_t rb;
    uint16_t storage[STORAGE_SIZE];
    uint16_t in[STORAGE_SIZE - 1] = {0xAAAA, 0xBBBB, 0xCCCC};
    uint16_t out = 0;

    Ring_Buffer_Init(&rb, storage, STORAGE_SIZE, sizeof(uint16_t));

    for (uint8_t i = 0; i < STORAGE_SIZE - 1; i++)
    {
        assert(Ring_Buffer_Push(&rb, &in[i]) == RC_OK);
    }

    for (uint8_t i = 0; i < STORAGE_SIZE - 1; i++)
    {
        assert(Ring_Buffer_Pop(&rb, &out) == RC_OK);
        assert(out == in[i]); /* FIFO order */
    }

    assert(Ring_Buffer_Pop(&rb, &out) == RC_OK_NO_DATA);
    assert(Ring_Buffer_Pop(NULL, &out) == RC_INVALID_ARG);
    assert(Ring_Buffer_Pop(&rb, NULL) == RC_INVALID_ARG);
}

/* -------------------------------------------------
 * Tests: Push / Pop multiple
 * ------------------------------------------------- */
static void Test_Push_Pop_N(void)
{
    ring_buffer_t rb;
    uint16_t storage[STORAGE_SIZE];
    uint16_t in[STORAGE_SIZE - 1];
    uint16_t out[STORAGE_SIZE - 1];

    Ring_Buffer_Init(&rb, storage, STORAGE_SIZE, sizeof(uint16_t));

    for (uint8_t i = 0; i < STORAGE_SIZE - 1; i++)
    {
        in[i] = (uint16_t)(0x1000 + i);
    }

    assert(Ring_Buffer_Push_N_Items(NULL, in, 1) == RC_INVALID_ARG);
    assert(Ring_Buffer_Push_N_Items(&rb, NULL, 1) == RC_INVALID_ARG);
    assert(Ring_Buffer_Push_N_Items(&rb, in, 0) == RC_DATA_RANGE);

    assert(Ring_Buffer_Push_N_Items(&rb, in, STORAGE_SIZE - 1) == RC_OK);
    assert(Ring_Buffer_Push_N_Items(&rb, in, 1) == RC_NO_RESOURCE);

    assert(Ring_Buffer_Pop_N_Items(NULL, out, 1) == RC_INVALID_ARG);
    assert(Ring_Buffer_Pop_N_Items(&rb, NULL, 1) == RC_INVALID_ARG);
    assert(Ring_Buffer_Pop_N_Items(&rb, out, 0) == RC_DATA_RANGE);

    assert(Ring_Buffer_Pop_N_Items(&rb, out, STORAGE_SIZE - 1) == RC_OK);
    assert(Ring_Buffer_Pop_N_Items(&rb, out, 1) == RC_OK_NO_DATA);

    for (uint8_t i = 0; i < STORAGE_SIZE - 1; i++)
    {
        assert(out[i] == in[i]); /* FIFO */
    }
}

/* -------------------------------------------------
 * Tests: Peek
 * ------------------------------------------------- */
static void Test_Peek_Nth(void)
{
    ring_buffer_t rb;
    uint16_t storage[STORAGE_SIZE];
    uint16_t in[STORAGE_SIZE - 1] = {1, 2, 3};
    uint16_t out = 0;

    Ring_Buffer_Init(&rb, storage, STORAGE_SIZE, sizeof(uint16_t));

    assert(Ring_Buffer_Peek_Nth(NULL, 1, &out) == RC_INVALID_ARG);
    assert(Ring_Buffer_Peek_Nth(&rb, 1, NULL) == RC_INVALID_ARG);
    assert(Ring_Buffer_Peek_Nth(&rb, 0, &out) == RC_DATA_RANGE);
    assert(Ring_Buffer_Peek_Nth(&rb, STORAGE_SIZE, &out) == RC_DATA_RANGE);
    assert(Ring_Buffer_Peek_Nth(&rb, 1, &out) == RC_OK_NO_DATA);

    assert(Ring_Buffer_Push_N_Items(&rb, in, STORAGE_SIZE - 1) == RC_OK);

    assert(Ring_Buffer_Peek_Nth(&rb, 1, &out) == RC_OK);
    assert(out == 1);

    assert(Ring_Buffer_Peek_Nth(&rb, 2, &out) == RC_OK);
    assert(out == 2);

    assert(Ring_Buffer_Peek_Nth(&rb, 3, &out) == RC_OK);
    assert(out == 3);

}

/* -------------------------------------------------
 * Tests: Discard / Clear
 * ------------------------------------------------- */
static void Test_Discard_Clear(void)
{
    ring_buffer_t rb;
    uint16_t storage[STORAGE_SIZE];
    uint16_t value = 0x55AA;
    uint8_t stored = 0;

    Ring_Buffer_Init(&rb, storage, STORAGE_SIZE, sizeof(uint16_t));

    assert(Ring_Buffer_Discard(NULL, 1) == RC_INVALID_ARG);
    assert(Ring_Buffer_Discard(&rb, 0) == RC_DATA_RANGE);
    assert(Ring_Buffer_Discard(&rb, 1) == RC_OK_NO_DATA);

    assert(Ring_Buffer_Push(&rb, &value) == RC_OK);
    assert(Ring_Buffer_Discard(&rb, 1) == RC_OK);

    Ring_Buffer_Stored_Count(&rb, &stored);
    assert(stored == 0);

    assert(Ring_Buffer_Push(&rb, &value) == RC_OK);
    assert(Ring_Buffer_Clear(NULL) == RC_INVALID_ARG);
    assert(Ring_Buffer_Clear(&rb) == RC_OK);

    assert(Ring_Buffer_Is_Empty(&rb) == RC_OK_TRUE);
}

/* -------------------------------------------------
 * Tests: Getters
 * ------------------------------------------------- */
static void Test_Getters(void)
{
    ring_buffer_t rb;
    uint8_t storage[STORAGE_SIZE];
    uint8_t cap = 0;
    uint8_t elem = 0;
    uint8_t stored = 0;

    Ring_Buffer_Init(&rb, storage, STORAGE_SIZE, sizeof(uint8_t));

    assert(Ring_Buffer_Get_Capacity(NULL, &cap) == RC_INVALID_ARG);
    assert(Ring_Buffer_Get_Capacity(&rb, NULL) == RC_INVALID_ARG);
    assert(Ring_Buffer_Get_Capacity(&rb, &cap) == RC_OK);
    assert(cap == STORAGE_SIZE);

    assert(Ring_Buffer_Get_Element_Size(&rb, &elem) == RC_OK);
    assert(elem == sizeof(uint8_t));

    assert(Ring_Buffer_Stored_Count(&rb, &stored) == RC_OK);
    assert(stored == 0);
}

/* -------------------------------------------------
 * Main
 * ------------------------------------------------- */
int main(void)
{
    Test_Init();
    Test_Empty_Full();
    Test_Push_Pop();
    Test_Push_Pop_N();
    Test_Peek_Nth();
    Test_Discard_Clear();
    Test_Getters();

    printf("All Ring Buffer tests passed.\n");
    return 0;
}

#endif // TEST_ENABLED