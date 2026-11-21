#define TEST_ENABLED 0

#if TEST_ENABLED == 1

#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "../Include/LIFO_Buffer.h"

/* -------------------------------------------------
 * Test configuration
 * ------------------------------------------------- */
#define STORAGE_SIZE   4

/* -------------------------------------------------
 * Tests: Initialization
 * ------------------------------------------------- */
static void Test_Init(void)
{
    lifo_buffer_t buf;
    uint8_t storage[STORAGE_SIZE];

    assert(LIFO_Buffer_Init(NULL, storage, STORAGE_SIZE, sizeof(uint8_t)) == RC_INVALID_ARG);
    assert(LIFO_Buffer_Init(&buf, NULL, STORAGE_SIZE, sizeof(uint8_t)) == RC_INVALID_ARG);
    assert(LIFO_Buffer_Init(&buf, storage, 0, sizeof(uint8_t)) == RC_DATA_RANGE);
    assert(LIFO_Buffer_Init(&buf, storage, STORAGE_SIZE, 0) == RC_DATA_RANGE);

    assert(LIFO_Buffer_Init(&buf, storage, STORAGE_SIZE, sizeof(uint8_t)) == RC_OK);
}

/* -------------------------------------------------
 * Tests: Empty / Full
 * ------------------------------------------------- */
static void Test_Empty_Full(void)
{
    lifo_buffer_t buf;
    uint16_t storage[STORAGE_SIZE];
    uint16_t value = 0x1234;

    LIFO_Buffer_Init(&buf, storage, STORAGE_SIZE, sizeof(uint16_t));

    assert(LIFO_Buffer_Is_Empty(NULL) == RC_INVALID_ARG);
    assert(LIFO_Buffer_Is_Full(NULL) == RC_INVALID_ARG);

    assert(LIFO_Buffer_Is_Empty(&buf) == RC_OK_TRUE);
    assert(LIFO_Buffer_Is_Full(&buf) == RC_OK_FALSE);

    for (uint8_t i = 0; i < STORAGE_SIZE; i++)
    {
        assert(LIFO_Buffer_Push(&buf, &value) == RC_OK);
    }

    assert(LIFO_Buffer_Is_Empty(&buf) == RC_OK_FALSE);
    assert(LIFO_Buffer_Is_Full(&buf) == RC_OK_TRUE);
}

/* -------------------------------------------------
 * Tests: Push / Pop single
 * ------------------------------------------------- */
static void Test_Push_Pop(void)
{
    lifo_buffer_t buf;
    uint16_t storage[STORAGE_SIZE];
    uint16_t in[STORAGE_SIZE] = {0xAAAA, 0xBBBB, 0xCCCC, 0xDDDD};
    uint16_t out = 0;

    LIFO_Buffer_Init(&buf, storage, STORAGE_SIZE, sizeof(uint16_t));


    for(int i = 0; i < STORAGE_SIZE; i++){
        assert(LIFO_Buffer_Push(&buf, &in[i]) == RC_OK);
    }
    assert(LIFO_Buffer_Push(&buf, &in[0]) == RC_NO_RESOURCE);
    assert(LIFO_Buffer_Push(NULL, &in[0]) == RC_INVALID_ARG);
    assert(LIFO_Buffer_Push(&buf, NULL) == RC_INVALID_ARG);

    for(int i = 0; i < STORAGE_SIZE; i++){
        assert(LIFO_Buffer_Pop(&buf, &out) == RC_OK);
        assert(out == in[STORAGE_SIZE - 1 - i]);
    }

    assert(LIFO_Buffer_Pop(&buf, &out) == RC_OK_NO_DATA);
    assert(LIFO_Buffer_Pop(NULL, &out) == RC_INVALID_ARG);
    assert(LIFO_Buffer_Pop(&buf, NULL) == RC_INVALID_ARG);
}

/* -------------------------------------------------
 * Tests: Push / Pop multiple
 * ------------------------------------------------- */
static void Test_Push_Pop_N(void)
{
    lifo_buffer_t buf;
    uint16_t storage[STORAGE_SIZE];
    uint16_t in[STORAGE_SIZE];
    uint16_t out[STORAGE_SIZE];

    LIFO_Buffer_Init(&buf, storage, STORAGE_SIZE, sizeof(uint16_t));

    for (uint8_t i = 0; i < STORAGE_SIZE; i++)
    {
        in[i] = (uint16_t)(0x1000 + i);
    }

    assert(LIFO_Buffer_Push_N(NULL, in, 1) == RC_INVALID_ARG);
    assert(LIFO_Buffer_Push_N(&buf, NULL, 1) == RC_INVALID_ARG);
    assert(LIFO_Buffer_Push_N(&buf, in, 0) == RC_DATA_RANGE);

    assert(LIFO_Buffer_Push_N(&buf, in, STORAGE_SIZE) == RC_OK);
    assert(LIFO_Buffer_Push_N(&buf, in, 1) == RC_NO_RESOURCE);

    assert(LIFO_Buffer_Pop_N(NULL, out, 1) == RC_INVALID_ARG);
    assert(LIFO_Buffer_Pop_N(&buf, NULL, 1) == RC_INVALID_ARG);
    assert(LIFO_Buffer_Pop_N(&buf, out, 0) == RC_DATA_RANGE);
    assert(LIFO_Buffer_Pop_N(&buf, out, STORAGE_SIZE) == RC_OK);
    assert(LIFO_Buffer_Pop_N(&buf, out, 1) == RC_OK_NO_DATA);

    /* LIFO order */
    for (uint8_t i = 0; i < STORAGE_SIZE; i++)
    {
        assert(out[i] == in[STORAGE_SIZE - 1 - i]);
    }
}

/* -------------------------------------------------
 * Tests: Peek
 * ------------------------------------------------- */
static void Test_Peek_Nth(void)
{
    lifo_buffer_t buf;
    uint16_t storage[STORAGE_SIZE];
    uint16_t in[STORAGE_SIZE] = {1, 2, 3, 4};
    uint16_t out = 0;

    LIFO_Buffer_Init(&buf, storage, STORAGE_SIZE, sizeof(uint16_t));

    assert(LIFO_Buffer_Peek_Nth(NULL, 1, &out) == RC_INVALID_ARG);
    assert(LIFO_Buffer_Peek_Nth(&buf, 1, NULL) == RC_INVALID_ARG);
    assert(LIFO_Buffer_Peek_Nth(&buf, 0, &out) == RC_DATA_RANGE);
    assert(LIFO_Buffer_Peek_Nth(&buf, 4, &out) == RC_OK_NO_DATA);


    uint8_t capacity;
    assert(LIFO_Buffer_Get_Capacity(&buf, &capacity) == RC_OK);
    assert(capacity == STORAGE_SIZE);

    assert(LIFO_Buffer_Push_N(&buf, in, STORAGE_SIZE) == RC_OK);

    assert(LIFO_Buffer_Peek_Nth(&buf, 1, &out) == RC_OK);
    assert(out == 4);
    assert(LIFO_Buffer_Peek_Nth(&buf, 2, &out) == RC_OK);
    assert(out == 3);
    assert(LIFO_Buffer_Peek_Nth(&buf, 3, &out) == RC_OK);
    assert(out == 2);
    assert(LIFO_Buffer_Peek_Nth(&buf, 4, &out) == RC_OK);
    assert(out == 1);
}

/* -------------------------------------------------
 * Tests: Discard / Clear
 * ------------------------------------------------- */
static void Test_Discard_Clear(void)
{
    lifo_buffer_t buf;
    uint16_t storage[STORAGE_SIZE];
    uint16_t value = 0x55AA;
    uint8_t stored_count = 0;

    LIFO_Buffer_Init(&buf, storage, STORAGE_SIZE, sizeof(uint16_t));

    assert(LIFO_Buffer_Discard(NULL, 1) == RC_INVALID_ARG);
    assert(LIFO_Buffer_Discard(&buf, 0) == RC_DATA_RANGE);
    assert(LIFO_Buffer_Discard(&buf, 1) == RC_OK_NO_DATA);

    assert(LIFO_Buffer_Push(&buf, &value) == RC_OK);
    assert(LIFO_Buffer_Discard(&buf, 1) == RC_OK);

    assert(LIFO_Buffer_Is_Empty(&buf) == RC_OK_TRUE);

    assert(LIFO_Buffer_Push(&buf, &value) == RC_OK);
    assert(LIFO_Buffer_Clear(NULL) == RC_INVALID_ARG);
    assert(LIFO_Buffer_Clear(&buf) == RC_OK);

    assert(LIFO_Buffer_Is_Empty(&buf) == RC_OK_TRUE);
}

/* -------------------------------------------------
 * Tests: Getters
 * ------------------------------------------------- */
static void Test_Getters(void)
{
    lifo_buffer_t buf;
    uint8_t storage[STORAGE_SIZE];
    uint8_t cap = 0;
    uint8_t stored_count = 0;
    uint8_t elem = 0xA;
    uint8_t elem_size = 0;


    LIFO_Buffer_Init(&buf, storage, STORAGE_SIZE, sizeof(uint8_t));

    assert(LIFO_Buffer_Get_Capacity(NULL, &cap) == RC_INVALID_ARG);
    assert(LIFO_Buffer_Get_Capacity(&buf, NULL) == RC_INVALID_ARG);
    assert(LIFO_Buffer_Get_Capacity(&buf, &cap) == RC_OK);
    assert(cap == STORAGE_SIZE);

    assert(LIFO_Buffer_Get_Stored_Count(&buf, &stored_count) == RC_OK);
    assert(stored_count == 0);

    assert(LIFO_Buffer_Push(&buf, &elem) == RC_OK);
    assert(LIFO_Buffer_Push(&buf, &elem) == RC_OK);
    assert(LIFO_Buffer_Push(&buf, &elem) == RC_OK);
    assert(LIFO_Buffer_Push(&buf, &elem) == RC_OK);
    assert(LIFO_Buffer_Get_Stored_Count(&buf, &stored_count) == RC_OK);
    assert(stored_count == 4);

    assert(LIFO_Buffer_Pop(&buf, &elem) == RC_OK);
    assert(LIFO_Buffer_Get_Stored_Count(&buf, &stored_count) == RC_OK);
    assert(stored_count == 3);

    assert(LIFO_Buffer_Get_Element_Size(&buf, &elem_size) == RC_OK);
    assert(elem_size == sizeof(uint8_t));
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

    printf("All LIFO Buffer tests passed.\n");
    return 0;
}

#endif // TEST_ENABLED