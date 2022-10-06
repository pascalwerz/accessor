// Although quick and dirty, these tests allowed to fix some bugs and leaks in the "accessor" package

// For best results, this program should be run with diagnostics enabled such as:
// - address sanitizer
// - undefined behavior sanitizer such as UBSan
// - memory allocation debugging such as Guard Malloc
// - memory leak detection


#define ACCESSOR_TEST_ITERATIONS    ((uintmax_t) 100)

#include "accessor.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>		// for mktemp etc.
#include <libgen.h>     // for basename



#define CHECK_EQ(x, v)      do { if ((x) != (v)) debugBreakpoint(__LINE__); } while (0)
#define CHECK_NE(x, v)      do { if ((x) == (v)) debugBreakpoint(__LINE__); } while (0)


// global variables
static accessorEndianness endianness[ACCESSOR_ENDIANNESS_COUNT] = { accessorBig, accessorLittle, accessorNative, accessorReverse }; // used to loop over endianness

// prototypes
void debugBreakpoint(int line);

void testEndianness(void);          // must be called first
void testOpen(void);
void testNumbers(void);
void testArrays(void);
void testBlocks(void);
void testLookAhead(void);
void testStrings(void);
void testGetPointer(void);
void testSwap(void);
void testWriteProtection(void);
void testCoverage(void);
void testOffset(void);
void testLimits(void);



void debugBreakpoint(int line)
{
    // set a breakpoint here to break on errors
    fprintf(stderr, "test failure in %s at line %u.\n", basename(__FILE__), line);
}



int main(int argc, char *argv[])
{
#pragma unused(argc, argv)

    printf("testing accessor build %ju\n", (uintmax_t) accessorBuildNumber());

    testEndianness();
    for (uintmax_t i = 1; i <= ACCESSOR_TEST_ITERATIONS; i++)
    {
        printf("test iteration %ju/%ju\r", i, ACCESSOR_TEST_ITERATIONS);
        fflush(stdout);
        testOpen();
        testNumbers();
        testArrays();
        testBlocks();
        testLookAhead();
        testStrings();
        testGetPointer();
        testSwap();
        testWriteProtection();
        testCoverage();
        testOffset();
        testLimits();
    }
    printf("All tests were run.        \n");

    return 0;
}



void testLimits(void)
{
#define TEST_LIMITS_SIZE 65536
    accessor_t * a = ACCESSOR_INIT;
    uint8_t wdata[TEST_LIMITS_SIZE + 1];
    uint8_t rdata[TEST_LIMITS_SIZE + 1];
    uintmax_t um;
    intmax_t im;
    uint8_t u8;
    uint16_t u16;
    uint32_t u24;
    uint32_t u32;
    uint64_t u64;
    int8_t i8;
    int16_t i16;
    int32_t i24;
    int32_t i32;
    int64_t i64;
    float f32;
    double f64;
    size_t count;
    void * array;


    for (size_t i = 0; i < sizeof(wdata) ; i++) wdata[i] = (uint8_t) random();

    CHECK_EQ(accessorOpenReadingMemory(&a, wdata, sizeof(wdata), accessorDontFreeOnClose, 1, ACCESSOR_UNTIL_END), accessorOk);

    for (int e = 0; e < ACCESSOR_ENDIANNESS_COUNT; e++)
    {
        CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);
        CHECK_EQ(accessorReadBytes(a, rdata, TEST_LIMITS_SIZE + 1), accessorBeyondEnd);
        CHECK_EQ(accessorReadBytes(a, rdata, TEST_LIMITS_SIZE), accessorOk);

        CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);
        CHECK_EQ(accessorReadEndianBytes(a, rdata, TEST_LIMITS_SIZE + 1, endianness[e]), accessorBeyondEnd);
        CHECK_EQ(accessorReadEndianBytes(a, rdata, TEST_LIMITS_SIZE, endianness[e]), accessorOk);

        CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);
        CHECK_EQ(accessorReadAllocatedBytes(a, &array, TEST_LIMITS_SIZE + 1), accessorBeyondEnd);
        CHECK_EQ(accessorReadAllocatedBytes(a, &array, TEST_LIMITS_SIZE), accessorOk);
        free(array);

        CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);
        CHECK_EQ(accessorReadAllocatedEndianBytes(a, &array, TEST_LIMITS_SIZE + 1, endianness[e]), accessorBeyondEnd);
        CHECK_EQ(accessorReadAllocatedEndianBytes(a, &array, TEST_LIMITS_SIZE, endianness[e]), accessorOk);
        free(array);

        CHECK_EQ(accessorSeek(a, TEST_LIMITS_SIZE - 1, SEEK_SET), accessorOk);
        CHECK_EQ(accessorReadEndianUInt(a, &um, endianness[e], 2), accessorBeyondEnd);
        CHECK_EQ(accessorReadEndianUInt(a, &um, endianness[e], 1), accessorOk);

        CHECK_EQ(accessorSeek(a, TEST_LIMITS_SIZE - 2 + 1, SEEK_SET), accessorOk);
        CHECK_EQ(accessorReadEndianUInt16(a, &u16, endianness[e]), accessorBeyondEnd);
        CHECK_EQ(accessorSeek(a, -1, SEEK_CUR), accessorOk);
        CHECK_EQ(accessorReadEndianUInt16(a, &u16, endianness[e]), accessorOk);

        CHECK_EQ(accessorSeek(a, TEST_LIMITS_SIZE - 3 + 1, SEEK_SET), accessorOk);
        CHECK_EQ(accessorReadEndianUInt24(a, &u24, endianness[e]), accessorBeyondEnd);
        CHECK_EQ(accessorSeek(a, -1, SEEK_CUR), accessorOk);
        CHECK_EQ(accessorReadEndianUInt24(a, &u24, endianness[e]), accessorOk);

        CHECK_EQ(accessorSeek(a, TEST_LIMITS_SIZE - 4 + 1, SEEK_SET), accessorOk);
        CHECK_EQ(accessorReadEndianUInt32(a, &u32, endianness[e]), accessorBeyondEnd);
        CHECK_EQ(accessorSeek(a, -1, SEEK_CUR), accessorOk);
        CHECK_EQ(accessorReadEndianUInt32(a, &u32, endianness[e]), accessorOk);

        CHECK_EQ(accessorSeek(a, TEST_LIMITS_SIZE - 8 + 1, SEEK_SET), accessorOk);
        CHECK_EQ(accessorReadEndianUInt64(a, &u64, endianness[e]), accessorBeyondEnd);
        CHECK_EQ(accessorSeek(a, -1, SEEK_CUR), accessorOk);
        CHECK_EQ(accessorReadEndianUInt64(a, &u64, endianness[e]), accessorOk);


        CHECK_EQ(accessorSeek(a, TEST_LIMITS_SIZE - 1, SEEK_SET), accessorOk);
        CHECK_EQ(accessorReadEndianInt(a, &im, endianness[e], 2), accessorBeyondEnd);
        CHECK_EQ(accessorReadEndianInt(a, &im, endianness[e], 1), accessorOk);

        CHECK_EQ(accessorSeek(a, TEST_LIMITS_SIZE - 2 + 1, SEEK_SET), accessorOk);
        CHECK_EQ(accessorReadEndianInt16(a, &i16, endianness[e]), accessorBeyondEnd);
        CHECK_EQ(accessorSeek(a, -1, SEEK_CUR), accessorOk);
        CHECK_EQ(accessorReadEndianInt16(a, &i16, endianness[e]), accessorOk);

        CHECK_EQ(accessorSeek(a, TEST_LIMITS_SIZE - 3 + 1, SEEK_SET), accessorOk);
        CHECK_EQ(accessorReadEndianInt24(a, &i24, endianness[e]), accessorBeyondEnd);
        CHECK_EQ(accessorSeek(a, -1, SEEK_CUR), accessorOk);
        CHECK_EQ(accessorReadEndianInt24(a, &i24, endianness[e]), accessorOk);

        CHECK_EQ(accessorSeek(a, TEST_LIMITS_SIZE - 4 + 1, SEEK_SET), accessorOk);
        CHECK_EQ(accessorReadEndianInt32(a, &i32, endianness[e]), accessorBeyondEnd);
        CHECK_EQ(accessorSeek(a, -1, SEEK_CUR), accessorOk);
        CHECK_EQ(accessorReadEndianInt32(a, &i32, endianness[e]), accessorOk);

        CHECK_EQ(accessorSeek(a, TEST_LIMITS_SIZE - 8 + 1, SEEK_SET), accessorOk);
        CHECK_EQ(accessorReadEndianInt64(a, &i64, endianness[e]), accessorBeyondEnd);
        CHECK_EQ(accessorSeek(a, -1, SEEK_CUR), accessorOk);
        CHECK_EQ(accessorReadEndianInt64(a, &i64, endianness[e]), accessorOk);

        CHECK_EQ(accessorSeek(a, TEST_LIMITS_SIZE - 4 + 1, SEEK_SET), accessorOk);
        CHECK_EQ(accessorReadEndianFloat32(a, &f32, endianness[e]), accessorBeyondEnd);
        CHECK_EQ(accessorSeek(a, -1, SEEK_CUR), accessorOk);
        CHECK_EQ(accessorReadEndianFloat32(a, &f32, endianness[e]), accessorOk);

        CHECK_EQ(accessorSeek(a, TEST_LIMITS_SIZE - 8 + 1, SEEK_SET), accessorOk);
        CHECK_EQ(accessorReadEndianFloat64(a, &f64, endianness[e]), accessorBeyondEnd);
        CHECK_EQ(accessorSeek(a, -1, SEEK_CUR), accessorOk);
        CHECK_EQ(accessorReadEndianFloat64(a, &f64, endianness[e]), accessorOk);


        CHECK_EQ(accessorSeek(a, TEST_LIMITS_SIZE - 1, SEEK_SET), accessorOk);
        CHECK_EQ(accessorReadUInt(a, &um, 2), accessorBeyondEnd);
        CHECK_EQ(accessorReadUInt(a, &um, 1), accessorOk);

        CHECK_EQ(accessorSeek(a, TEST_LIMITS_SIZE - 1 + 1, SEEK_SET), accessorOk);
        CHECK_EQ(accessorReadUInt8(a, &u8), accessorBeyondEnd);
        CHECK_EQ(accessorSeek(a, -1, SEEK_CUR), accessorOk);
        CHECK_EQ(accessorReadUInt8(a, &u8), accessorOk);

        CHECK_EQ(accessorSeek(a, TEST_LIMITS_SIZE - 2 + 1, SEEK_SET), accessorOk);
        CHECK_EQ(accessorReadUInt16(a, &u16), accessorBeyondEnd);
        CHECK_EQ(accessorSeek(a, -1, SEEK_CUR), accessorOk);
        CHECK_EQ(accessorReadUInt16(a, &u16), accessorOk);

        CHECK_EQ(accessorSeek(a, TEST_LIMITS_SIZE - 3 + 1, SEEK_SET), accessorOk);
        CHECK_EQ(accessorReadUInt24(a, &u24), accessorBeyondEnd);
        CHECK_EQ(accessorSeek(a, -1, SEEK_CUR), accessorOk);
        CHECK_EQ(accessorReadUInt24(a, &u24), accessorOk);

        CHECK_EQ(accessorSeek(a, TEST_LIMITS_SIZE - 4 + 1, SEEK_SET), accessorOk);
        CHECK_EQ(accessorReadUInt32(a, &u32), accessorBeyondEnd);
        CHECK_EQ(accessorSeek(a, -1, SEEK_CUR), accessorOk);
        CHECK_EQ(accessorReadUInt32(a, &u32), accessorOk);

        CHECK_EQ(accessorSeek(a, TEST_LIMITS_SIZE - 8 + 1, SEEK_SET), accessorOk);
        CHECK_EQ(accessorReadUInt64(a, &u64), accessorBeyondEnd);
        CHECK_EQ(accessorSeek(a, -1, SEEK_CUR), accessorOk);
        CHECK_EQ(accessorReadUInt64(a, &u64), accessorOk);


        CHECK_EQ(accessorSeek(a, TEST_LIMITS_SIZE - 1, SEEK_SET), accessorOk);
        CHECK_EQ(accessorReadInt(a, &im, 2), accessorBeyondEnd);
        CHECK_EQ(accessorReadInt(a, &im, 1), accessorOk);

        CHECK_EQ(accessorSeek(a, TEST_LIMITS_SIZE - 1 + 1, SEEK_SET), accessorOk);
        CHECK_EQ(accessorReadInt8(a, &i8), accessorBeyondEnd);
        CHECK_EQ(accessorSeek(a, -1, SEEK_CUR), accessorOk);
        CHECK_EQ(accessorReadInt8(a, &i8), accessorOk);

        CHECK_EQ(accessorSeek(a, TEST_LIMITS_SIZE - 2 + 1, SEEK_SET), accessorOk);
        CHECK_EQ(accessorReadInt16(a, &i16), accessorBeyondEnd);
        CHECK_EQ(accessorSeek(a, -1, SEEK_CUR), accessorOk);
        CHECK_EQ(accessorReadInt16(a, &i16), accessorOk);

        CHECK_EQ(accessorSeek(a, TEST_LIMITS_SIZE - 3 + 1, SEEK_SET), accessorOk);
        CHECK_EQ(accessorReadInt24(a, &i24), accessorBeyondEnd);
        CHECK_EQ(accessorSeek(a, -1, SEEK_CUR), accessorOk);
        CHECK_EQ(accessorReadInt24(a, &i24), accessorOk);

        CHECK_EQ(accessorSeek(a, TEST_LIMITS_SIZE - 4 + 1, SEEK_SET), accessorOk);
        CHECK_EQ(accessorReadInt32(a, &i32), accessorBeyondEnd);
        CHECK_EQ(accessorSeek(a, -1, SEEK_CUR), accessorOk);
        CHECK_EQ(accessorReadInt32(a, &i32), accessorOk);

        CHECK_EQ(accessorSeek(a, TEST_LIMITS_SIZE - 8 + 1, SEEK_SET), accessorOk);
        CHECK_EQ(accessorReadInt64(a, &i64), accessorBeyondEnd);
        CHECK_EQ(accessorSeek(a, -1, SEEK_CUR), accessorOk);
        CHECK_EQ(accessorReadInt64(a, &i64), accessorOk);

        CHECK_EQ(accessorSeek(a, TEST_LIMITS_SIZE - 4 + 1, SEEK_SET), accessorOk);
        CHECK_EQ(accessorReadFloat32(a, &f32), accessorBeyondEnd);
        CHECK_EQ(accessorSeek(a, -1, SEEK_CUR), accessorOk);
        CHECK_EQ(accessorReadFloat32(a, &f32), accessorOk);

        CHECK_EQ(accessorSeek(a, TEST_LIMITS_SIZE - 8 + 1, SEEK_SET), accessorOk);
        CHECK_EQ(accessorReadFloat64(a, &f64), accessorBeyondEnd);
        CHECK_EQ(accessorSeek(a, -1, SEEK_CUR), accessorOk);
        CHECK_EQ(accessorReadFloat64(a, &f64), accessorOk);


        CHECK_EQ(accessorSeek(a, TEST_LIMITS_SIZE - 2 * 2 + 1, SEEK_SET), accessorOk);
        CHECK_EQ(accessorReadEndianUInt16Array(a, (uint16_t **) &array, 2, endianness[e]), accessorBeyondEnd);
        CHECK_EQ(accessorSeek(a, -1, SEEK_CUR), accessorOk);
        CHECK_EQ(accessorReadEndianUInt16Array(a, (uint16_t **) &array, 2, endianness[e]), accessorOk);
        free(array);

        CHECK_EQ(accessorSeek(a, TEST_LIMITS_SIZE - 3 * 2 + 1, SEEK_SET), accessorOk);
        CHECK_EQ(accessorReadEndianUInt24Array(a, (uint32_t **) &array, 2, endianness[e]), accessorBeyondEnd);
        CHECK_EQ(accessorSeek(a, -1, SEEK_CUR), accessorOk);
        CHECK_EQ(accessorReadEndianUInt24Array(a, (uint32_t **) &array, 2, endianness[e]), accessorOk);
        free(array);

        CHECK_EQ(accessorSeek(a, TEST_LIMITS_SIZE - 4 * 2 + 1, SEEK_SET), accessorOk);
        CHECK_EQ(accessorReadEndianUInt32Array(a, (uint32_t **) &array, 2, endianness[e]), accessorBeyondEnd);
        CHECK_EQ(accessorSeek(a, -1, SEEK_CUR), accessorOk);
        CHECK_EQ(accessorReadEndianUInt32Array(a, (uint32_t **) &array, 2, endianness[e]), accessorOk);
        free(array);

        CHECK_EQ(accessorSeek(a, TEST_LIMITS_SIZE - 8 * 2 + 1, SEEK_SET), accessorOk);
        CHECK_EQ(accessorReadEndianUInt64Array(a, (uint64_t **) &array, 2, endianness[e]), accessorBeyondEnd);
        CHECK_EQ(accessorSeek(a, -1, SEEK_CUR), accessorOk);
        CHECK_EQ(accessorReadEndianUInt64Array(a, (uint64_t **) &array, 2, endianness[e]), accessorOk);
        free(array);

        CHECK_EQ(accessorSeek(a, TEST_LIMITS_SIZE - 2 * 2 + 1, SEEK_SET), accessorOk);
        CHECK_EQ(accessorReadEndianInt16Array(a, (int16_t **) &array, 2, endianness[e]), accessorBeyondEnd);
        CHECK_EQ(accessorSeek(a, -1, SEEK_CUR), accessorOk);
        CHECK_EQ(accessorReadEndianInt16Array(a, (int16_t **) &array, 2, endianness[e]), accessorOk);
        free(array);

        CHECK_EQ(accessorSeek(a, TEST_LIMITS_SIZE - 3 * 2 + 1, SEEK_SET), accessorOk);
        CHECK_EQ(accessorReadEndianInt24Array(a, (int32_t **) &array, 2, endianness[e]), accessorBeyondEnd);
        CHECK_EQ(accessorSeek(a, -1, SEEK_CUR), accessorOk);
        CHECK_EQ(accessorReadEndianInt24Array(a, (int32_t **) &array, 2, endianness[e]), accessorOk);
        free(array);

        CHECK_EQ(accessorSeek(a, TEST_LIMITS_SIZE - 4 * 2 + 1, SEEK_SET), accessorOk);
        CHECK_EQ(accessorReadEndianInt32Array(a, (int32_t **) &array, 2, endianness[e]), accessorBeyondEnd);
        CHECK_EQ(accessorSeek(a, -1, SEEK_CUR), accessorOk);
        CHECK_EQ(accessorReadEndianInt32Array(a, (int32_t **) &array, 2, endianness[e]), accessorOk);
        free(array);

        CHECK_EQ(accessorSeek(a, TEST_LIMITS_SIZE - 8 * 2 + 1, SEEK_SET), accessorOk);
        CHECK_EQ(accessorReadEndianInt64Array(a, (int64_t **) &array, 2, endianness[e]), accessorBeyondEnd);
        CHECK_EQ(accessorSeek(a, -1, SEEK_CUR), accessorOk);
        CHECK_EQ(accessorReadEndianInt64Array(a, (int64_t **) &array, 2, endianness[e]), accessorOk);
        free(array);

        CHECK_EQ(accessorSeek(a, TEST_LIMITS_SIZE - 4 * 2 + 1, SEEK_SET), accessorOk);
        CHECK_EQ(accessorReadEndianFloat32Array(a, (float **) &array, 2, endianness[e]), accessorBeyondEnd);
        CHECK_EQ(accessorSeek(a, -1, SEEK_CUR), accessorOk);
        CHECK_EQ(accessorReadEndianFloat32Array(a, (float **) &array, 2, endianness[e]), accessorOk);
        free(array);

        CHECK_EQ(accessorSeek(a, TEST_LIMITS_SIZE - 8 * 2 + 1, SEEK_SET), accessorOk);
        CHECK_EQ(accessorReadEndianFloat64Array(a, (double **) &array, 2, endianness[e]), accessorBeyondEnd);
        CHECK_EQ(accessorSeek(a, -1, SEEK_CUR), accessorOk);
        CHECK_EQ(accessorReadEndianFloat64Array(a, (double **) &array, 2, endianness[e]), accessorOk);
        free(array);


        CHECK_EQ(accessorSeek(a, TEST_LIMITS_SIZE - 2 * 2 + 1, SEEK_SET), accessorOk);
        CHECK_EQ(accessorReadUInt16Array(a, (uint16_t **) &array, 2), accessorBeyondEnd);
        CHECK_EQ(accessorSeek(a, -1, SEEK_CUR), accessorOk);
        CHECK_EQ(accessorReadUInt16Array(a, (uint16_t **) &array, 2), accessorOk);
        free(array);

        CHECK_EQ(accessorSeek(a, TEST_LIMITS_SIZE - 3 * 2 + 1, SEEK_SET), accessorOk);
        CHECK_EQ(accessorReadUInt24Array(a, (uint32_t **) &array, 2), accessorBeyondEnd);
        CHECK_EQ(accessorSeek(a, -1, SEEK_CUR), accessorOk);
        CHECK_EQ(accessorReadUInt24Array(a, (uint32_t **) &array, 2), accessorOk);
        free(array);

        CHECK_EQ(accessorSeek(a, TEST_LIMITS_SIZE - 4 * 2 + 1, SEEK_SET), accessorOk);
        CHECK_EQ(accessorReadUInt32Array(a, (uint32_t **) &array, 2), accessorBeyondEnd);
        CHECK_EQ(accessorSeek(a, -1, SEEK_CUR), accessorOk);
        CHECK_EQ(accessorReadUInt32Array(a, (uint32_t **) &array, 2), accessorOk);
        free(array);

        CHECK_EQ(accessorSeek(a, TEST_LIMITS_SIZE - 8 * 2 + 1, SEEK_SET), accessorOk);
        CHECK_EQ(accessorReadUInt64Array(a, (uint64_t **) &array, 2), accessorBeyondEnd);
        CHECK_EQ(accessorSeek(a, -1, SEEK_CUR), accessorOk);
        CHECK_EQ(accessorReadUInt64Array(a, (uint64_t **) &array, 2), accessorOk);
        free(array);

        CHECK_EQ(accessorSeek(a, TEST_LIMITS_SIZE - 2 * 2 + 1, SEEK_SET), accessorOk);
        CHECK_EQ(accessorReadInt16Array(a, (int16_t **) &array, 2), accessorBeyondEnd);
        CHECK_EQ(accessorSeek(a, -1, SEEK_CUR), accessorOk);
        CHECK_EQ(accessorReadInt16Array(a, (int16_t **) &array, 2), accessorOk);
        free(array);

        CHECK_EQ(accessorSeek(a, TEST_LIMITS_SIZE - 3 * 2 + 1, SEEK_SET), accessorOk);
        CHECK_EQ(accessorReadInt24Array(a, (int32_t **) &array, 2), accessorBeyondEnd);
        CHECK_EQ(accessorSeek(a, -1, SEEK_CUR), accessorOk);
        CHECK_EQ(accessorReadInt24Array(a, (int32_t **) &array, 2), accessorOk);
        free(array);

        CHECK_EQ(accessorSeek(a, TEST_LIMITS_SIZE - 4 * 2 + 1, SEEK_SET), accessorOk);
        CHECK_EQ(accessorReadInt32Array(a, (int32_t **) &array, 2), accessorBeyondEnd);
        CHECK_EQ(accessorSeek(a, -1, SEEK_CUR), accessorOk);
        CHECK_EQ(accessorReadInt32Array(a, (int32_t **) &array, 2), accessorOk);
        free(array);

        CHECK_EQ(accessorSeek(a, TEST_LIMITS_SIZE - 8 * 2 + 1, SEEK_SET), accessorOk);
        CHECK_EQ(accessorReadInt64Array(a, (int64_t **) &array, 2), accessorBeyondEnd);
        CHECK_EQ(accessorSeek(a, -1, SEEK_CUR), accessorOk);
        CHECK_EQ(accessorReadInt64Array(a, (int64_t **) &array, 2), accessorOk);
        free(array);

        CHECK_EQ(accessorSeek(a, TEST_LIMITS_SIZE - 4 * 2 + 1, SEEK_SET), accessorOk);
        CHECK_EQ(accessorReadFloat32Array(a, (float **) &array, 2), accessorBeyondEnd);
        CHECK_EQ(accessorSeek(a, -1, SEEK_CUR), accessorOk);
        CHECK_EQ(accessorReadFloat32Array(a, (float **) &array, 2), accessorOk);
        free(array);

        CHECK_EQ(accessorSeek(a, TEST_LIMITS_SIZE - 8 * 2 + 1, SEEK_SET), accessorOk);
        CHECK_EQ(accessorReadFloat64Array(a, (double **) &array, 2), accessorBeyondEnd);
        CHECK_EQ(accessorSeek(a, -1, SEEK_CUR), accessorOk);
        CHECK_EQ(accessorReadFloat64Array(a, (double **) &array, 2), accessorOk);
        free(array);

        CHECK_EQ(accessorSeek(a, TEST_LIMITS_SIZE, SEEK_SET), accessorOk);
        CHECK_EQ(accessorLookAheadEndianBytes(a, rdata, 10, endianness[e]), 0);
        CHECK_EQ(accessorSeek(a, -1, SEEK_CUR), accessorOk);
        CHECK_EQ(accessorLookAheadEndianBytes(a, rdata, 10, endianness[e]), 1);

        CHECK_EQ(accessorSeek(a, TEST_LIMITS_SIZE, SEEK_SET), accessorOk);
        CHECK_EQ(accessorLookAheadBytes(a, rdata, 10), 0);
        CHECK_EQ(accessorSeek(a, -1, SEEK_CUR), accessorOk);
        CHECK_EQ(accessorLookAheadBytes(a, rdata, 10), 1);

        CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);
        CHECK_EQ(accessorLookAheadAvailableBytes(a, (const void **) &array), TEST_LIMITS_SIZE);
    }

    CHECK_EQ(accessorClose(&a), accessorOk);

    CHECK_EQ(accessorOpenWritingMemory(&a, 0, 0), accessorOk);

    for (int e = 0; e < ACCESSOR_ENDIANNESS_COUNT; e++)
    {
        CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);
        CHECK_EQ(accessorTruncate(a), accessorOk);
        CHECK_EQ(accessorWriteRepeatedByte(a, ' ', TEST_LIMITS_SIZE), accessorOk);
        CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);
        CHECK_EQ(accessorReadCString(a, (char **) &array, &count), accessorBeyondEnd);

        CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);
        CHECK_EQ(accessorWriteRepeatedByte(a, ' ', TEST_LIMITS_SIZE - 1), accessorOk);
        CHECK_EQ(accessorWriteUInt8(a, 0x00), accessorOk);
        CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);
        CHECK_EQ(accessorReadCString(a, (char **) &array, &count), accessorOk);
        CHECK_EQ(accessorAvailableBytesCount(a), 0);
        free(array);

        CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);
        CHECK_EQ(accessorTruncate(a), accessorOk);
        CHECK_EQ(accessorWriteUInt8(a, 0x08), accessorOk);
        CHECK_EQ(accessorWriteRepeatedByte(a, ' ', 7), accessorOk);
        CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);
        CHECK_EQ(accessorReadPString(a, (char **) &array, &count), accessorBeyondEnd);
        CHECK_EQ(accessorAvailableBytesCount(a), 8);

        CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);
        CHECK_EQ(accessorTruncate(a), accessorOk);
        CHECK_EQ(accessorWriteUInt8(a, 0x08), accessorOk);
        CHECK_EQ(accessorWriteRepeatedByte(a, ' ', 8), accessorOk);
        CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);
        CHECK_EQ(accessorReadPString(a, (char **) &array, &count), accessorOk);
        CHECK_EQ(accessorAvailableBytesCount(a), 0);
        free(array);

        CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);
        CHECK_EQ(accessorTruncate(a), accessorOk);
        CHECK_EQ(accessorWriteRepeatedByte(a, ' ', 7), accessorOk);
        CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);
        CHECK_EQ(accessorReadFixedLengthString(a, (char **) &array, 8), accessorBeyondEnd);
        CHECK_EQ(accessorAvailableBytesCount(a), 7);

        CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);
        CHECK_EQ(accessorTruncate(a), accessorOk);
        CHECK_EQ(accessorWriteRepeatedByte(a, ' ', 8), accessorOk);
        CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);
        CHECK_EQ(accessorReadFixedLengthString(a, (char **) &array, 8), accessorOk);
        CHECK_EQ(accessorAvailableBytesCount(a), 0);
        free(array);

        CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);
        CHECK_EQ(accessorTruncate(a), accessorOk);
        CHECK_EQ(accessorWriteRepeatedByte(a, ' ', 7), accessorOk);
        CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);
        count = 8;
        CHECK_EQ(accessorReadPaddedString(a, (char **) &array, &count, ' '), accessorBeyondEnd);
        CHECK_EQ(accessorAvailableBytesCount(a), 7);

        CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);
        CHECK_EQ(accessorTruncate(a), accessorOk);
        CHECK_EQ(accessorWriteRepeatedByte(a, ' ', 8), accessorOk);
        CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);
        count = 8;
        CHECK_EQ(accessorReadPaddedString(a, (char **) &array, &count, ' '), accessorOk);
        CHECK_EQ(count, 0);
        CHECK_EQ(accessorAvailableBytesCount(a), 0);
        free(array);

        CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);
        CHECK_EQ(accessorTruncate(a), accessorOk);
        for (size_t i = 0; i < 8; i++)
            CHECK_EQ(accessorWriteUInt16(a, 0x0020), accessorOk);
        CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);
        CHECK_EQ(accessorReadEndianString16(a, (uint16_t **) &array, &count, endianness[e]), accessorBeyondEnd);

        CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);
        CHECK_EQ(accessorTruncate(a), accessorOk);
        for (size_t i = 0; i < 8; i++)
            CHECK_EQ(accessorWriteUInt16(a, 0x0020), accessorOk);
        CHECK_EQ(accessorWriteUInt16(a, 0x0000), accessorOk);
        CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);
        CHECK_EQ(accessorReadEndianString16(a, (uint16_t **) &array, &count, endianness[e]), accessorOk);
        CHECK_EQ(count, 8);
        CHECK_EQ(accessorAvailableBytesCount(a), 0);
        free(array);

        CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);
        CHECK_EQ(accessorTruncate(a), accessorOk);
        for (size_t i = 0; i < 8; i++)
            CHECK_EQ(accessorWriteUInt32(a, 0x00000020), accessorOk);
        CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);
        CHECK_EQ(accessorReadEndianString32(a, (uint32_t **) &array, &count, endianness[e]), accessorBeyondEnd);

        CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);
        CHECK_EQ(accessorTruncate(a), accessorOk);
        for (size_t i = 0; i < 8; i++)
            CHECK_EQ(accessorWriteUInt32(a, 0x00000020), accessorOk);
        CHECK_EQ(accessorWriteUInt32(a, 0x00000000), accessorOk);
        CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);
        CHECK_EQ(accessorReadEndianString32(a, (uint32_t **) &array, &count, endianness[e]), accessorOk);
        CHECK_EQ(count, 8);
        CHECK_EQ(accessorAvailableBytesCount(a), 0);
        free(array);

        CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);
        CHECK_EQ(accessorTruncate(a), accessorOk);
        for (size_t i = 0; i < 8; i++)
            CHECK_EQ(accessorWriteUInt16(a, 0x0020), accessorOk);
        CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);
        CHECK_EQ(accessorReadString16(a, (uint16_t **) &array, &count), accessorBeyondEnd);

        CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);
        CHECK_EQ(accessorTruncate(a), accessorOk);
        for (size_t i = 0; i < 8; i++)
            CHECK_EQ(accessorWriteUInt16(a, 0x0020), accessorOk);
        CHECK_EQ(accessorWriteUInt16(a, 0x0000), accessorOk);
        CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);
        CHECK_EQ(accessorReadString16(a, (uint16_t **) &array, &count), accessorOk);
        CHECK_EQ(count, 8);
        CHECK_EQ(accessorAvailableBytesCount(a), 0);
        free(array);

        CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);
        CHECK_EQ(accessorTruncate(a), accessorOk);
        for (size_t i = 0; i < 8; i++)
            CHECK_EQ(accessorWriteUInt32(a, 0x00000020), accessorOk);
        CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);
        CHECK_EQ(accessorReadString32(a, (uint32_t **) &array, &count), accessorBeyondEnd);

        CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);
        CHECK_EQ(accessorTruncate(a), accessorOk);
        for (size_t i = 0; i < 8; i++)
            CHECK_EQ(accessorWriteUInt32(a, 0x00000020), accessorOk);
        CHECK_EQ(accessorWriteUInt32(a, 0x00000000), accessorOk);
        CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);
        CHECK_EQ(accessorReadString32(a, (uint32_t **) &array, &count), accessorOk);
        CHECK_EQ(count, 8);
        CHECK_EQ(accessorAvailableBytesCount(a), 0);
        free(array);

        CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);
        CHECK_EQ(accessorTruncate(a), accessorOk);
        for (size_t i = 0; i < 8; i++)
            CHECK_EQ(accessorWriteUInt8(a, 0x00), accessorOk);
        CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);
        CHECK_EQ(accessorGetPointerForBytesToRead(a, (const void **) &array, 9), accessorBeyondEnd);
        CHECK_EQ(accessorGetPointerForBytesToRead(a, (const void **) &array, 8), accessorOk);
        array = NULL;

        CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);
        CHECK_EQ(accessorTruncate(a), accessorOk);
        for (size_t i = 0; i < 8; i++)
            CHECK_EQ(accessorWriteUInt8(a, ' '), accessorOk);
        CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);
        CHECK_EQ(accessorLookAheadCountBytesBeforeDelimiter(a, &count, 8, 1, "\n"), accessorBeyondEnd);

        CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);
        CHECK_EQ(accessorTruncate(a), accessorOk);
        for (size_t i = 0; i < 8; i++)
            CHECK_EQ(accessorWriteUInt8(a, ' '), accessorOk);
        CHECK_EQ(accessorWriteUInt8(a, '\r'), accessorOk);
        CHECK_EQ(accessorWriteUInt8(a, '\n'), accessorOk);
        CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);
        CHECK_EQ(accessorLookAheadCountBytesBeforeDelimiter(a, &count, 8, 2, "\r\n"), accessorOk);
        CHECK_EQ(count, 8);
    }

    CHECK_EQ(accessorClose(&a), accessorOk);

    CHECK_EQ(accessorOpenWritingMemory(&a, 0, 0), accessorOk);

    for (size_t i = 0; i < (sizeof(uintmax_t) * CHAR_BIT + 6) / 7 - 1; i++)
        CHECK_EQ(accessorWriteUInt8(a, 0x80), accessorOk);
    CHECK_EQ(accessorWriteUInt8(a, 0x00), accessorOk);

    CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);
    CHECK_EQ(accessorReadVarInt(a, &um), accessorOk);
    CHECK_EQ(accessorReadUInt8(a, &u8), accessorBeyondEnd);

    CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);
    CHECK_EQ(accessorReadZigZagInt(a, &im), accessorOk);
    CHECK_EQ(accessorReadUInt8(a, &u8), accessorBeyondEnd);

    CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);
    for (size_t i = 0; i < (sizeof(uintmax_t) * CHAR_BIT + 6) / 7 - 1; i++)
        CHECK_EQ(accessorWriteUInt8(a, 0xff), accessorOk);
    CHECK_EQ(accessorWriteUInt8(a, 0x7f), accessorOk);

    CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);
    CHECK_EQ(accessorReadVarInt(a, &um), accessorOk);
    CHECK_EQ(accessorReadUInt8(a, &u8), accessorBeyondEnd);

    CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);
    CHECK_EQ(accessorReadZigZagInt(a, &im), accessorOk);
    CHECK_EQ(accessorReadUInt8(a, &u8), accessorBeyondEnd);

    CHECK_EQ(accessorClose(&a), accessorOk);
}



void testOffset(void)
{
#define TEST_OFFSET_SIZE 65536
    accessor_t * a = ACCESSOR_INIT;
    accessor_t * b = ACCESSOR_INIT;
    accessor_t * c = ACCESSOR_INIT;
    uint8_t data[TEST_OFFSET_SIZE];


    for (size_t i = 0; i < TEST_OFFSET_SIZE; i++) data[i] = (uint8_t) random();

    CHECK_EQ(accessorOpenReadingMemory(&a, data, sizeof(data), accessorDontFreeOnClose, 1, ACCESSOR_UNTIL_END), accessorOk);
    CHECK_EQ(accessorOpenReadingAccessorWindow(&b, a, 1, ACCESSOR_UNTIL_END), accessorOk);
    CHECK_EQ(accessorOpenReadingAccessorWindow(&c, b, 1, ACCESSOR_UNTIL_END), accessorOk);

    CHECK_EQ(accessorRootWindowOffset(c), 3);
    CHECK_EQ(accessorRootWindowOffset(b), 2);
    CHECK_EQ(accessorRootWindowOffset(a), 1);

    CHECK_EQ(accessorClose(&c), accessorOk);
    CHECK_EQ(accessorClose(&b), accessorOk);
    CHECK_EQ(accessorClose(&a), accessorOk);
}



void testCoverage(void)
{
#define TEST_COVERAGE_SIZE  65536
    accessor_t * a = ACCESSOR_INIT;
    uint8_t data[TEST_COVERAGE_SIZE];
    uint8_t u8;
    const accessorCoverageRecord * records;
    size_t count;


    for (size_t i = 0; i < TEST_COVERAGE_SIZE; i++) data[i] = (uint8_t) random();

    CHECK_EQ(accessorOpenReadingMemory(&a, data, sizeof(data), accessorDontFreeOnClose, 0, ACCESSOR_UNTIL_END), accessorOk);
    accessorAllowCoverage(a, accessorEnableCoverage);
    CHECK_EQ(accessorIsCoverageAllowed(a), accessorEnableCoverage);
    accessorAllowCoverage(a, accessorDisableCoverage);
    CHECK_EQ(accessorIsCoverageAllowed(a), accessorDisableCoverage);

    accessorSetCoverageUsage(a, 0, (const void *) 1);
    accessorAllowCoverage(a, accessorEnableCoverage);
    CHECK_EQ(accessorIsCoverageAllowed(a), accessorEnableCoverage);

    CHECK_EQ(accessorReadUInt8(a, &u8), accessorOk);
    records = accessorCoverageArray(a, &count);
    CHECK_EQ(count, 1);

    CHECK_EQ(accessorReadUInt8(a, &u8), accessorOk);
    records = accessorCoverageArray(a, &count);
    CHECK_EQ(count, 2);

    CHECK_EQ(accessorReadUInt8(a, &u8), accessorOk);
    records = accessorCoverageArray(a, &count);
    CHECK_EQ(count, 3);

    CHECK_EQ(accessorReadUInt8(a, &u8), accessorOk);
    records = accessorCoverageArray(a, &count);
    CHECK_EQ(count, 4);

    accessorSuspendCoverage(a);
    accessorSuspendCoverage(a);
    CHECK_EQ(accessorReadUInt8(a, &u8), accessorOk);
    records = accessorCoverageArray(a, &count);
    CHECK_EQ(count, 4);
    accessorResumeCoverage(a);
    CHECK_EQ(accessorReadUInt8(a, &u8), accessorOk);
    records = accessorCoverageArray(a, &count);
    CHECK_EQ(count, 4);
    accessorResumeCoverage(a);
    CHECK_EQ(accessorReadUInt8(a, &u8), accessorOk);
    records = accessorCoverageArray(a, &count);
    CHECK_EQ(count, 5);

    accessorAddCoverageRecord(a, accessorCursor(a), 1, 2, (const void *) 3, accessorCoverageOnlyIfEnabled);
    records = accessorCoverageArray(a, &count);
    CHECK_EQ(count, 6);

    accessorSuspendCoverage(a);
    accessorAddCoverageRecord(a, accessorCursor(a), 1, 2, (const void *) 3, accessorCoverageOnlyIfEnabled);
    records = accessorCoverageArray(a, &count);
    CHECK_EQ(count, 6);
    accessorResumeCoverage(a);

    accessorAllowCoverage(a, accessorDisableCoverage);
    accessorAddCoverageRecord(a, accessorCursor(a), 1, 2, (const void *) 3, accessorCoverageOnlyIfEnabled);
    records = accessorCoverageArray(a, &count);
    CHECK_EQ(count, 6);
    accessorAddCoverageRecord(a, accessorSize(a), 1, 2, (const void *) 3, accessorCoverageEvenIfDisabled);
    records = accessorCoverageArray(a, &count);
    CHECK_EQ(count, 6);
    accessorAddCoverageRecord(a, accessorSize(a) - 1, ACCESSOR_UNTIL_END, 2, (const void *) 3, accessorCoverageEvenIfDisabled);
    records = accessorCoverageArray(a, &count);
    CHECK_EQ(count, 7);
    accessorAllowCoverage(a, accessorEnableCoverage);

    accessorSummarizeCoverage(a, NULL, NULL);
    records = accessorCoverageArray(a, &count);
    CHECK_EQ(count, 4);

    CHECK_EQ(records[0].offset, 0);
    CHECK_EQ(records[0].size, 4);
    CHECK_EQ(records[0].usage1, 0);
    CHECK_EQ(records[0].usage2, (const void *) 1);

    CHECK_EQ(records[1].offset, 6);
    CHECK_EQ(records[1].size, 1);
    CHECK_EQ(records[1].usage1, 0);
    CHECK_EQ(records[1].usage2, (const void *) 1);

    CHECK_EQ(records[2].offset, 7);
    CHECK_EQ(records[2].size, 1);
    CHECK_EQ(records[2].usage1, 2);
    CHECK_EQ(records[2].usage2, (const void *) 3);

    CHECK_EQ(records[3].offset, TEST_COVERAGE_SIZE - 1);
    CHECK_EQ(records[3].size, 1);
    CHECK_EQ(records[3].usage1, 2);
    CHECK_EQ(records[3].usage2, (const void *) 3);

    CHECK_EQ(accessorClose(&a), accessorOk);
}



void testWriteProtection(void)
{
    accessor_t * a = ACCESSOR_INIT;
    accessor_t * b = ACCESSOR_INIT;
    uint8_t data[256];
    void * ptr;


    memset(data, 0, sizeof(data));

    CHECK_EQ(accessorOpenWritingMemory(&a, 0, 0), accessorOk);
    CHECK_EQ(accessorOpenReadingMemory(&b, data, sizeof(data), accessorDontFreeOnClose, 0, ACCESSOR_UNTIL_END), accessorOk);

    CHECK_EQ(accessorSwap(&a, &b), accessorOk);
    CHECK_EQ(accessorClose(&b), accessorOk);

    CHECK_EQ(accessorTruncate(a), accessorReadOnlyError);
    CHECK_EQ(accessorWriteEndianUInt(a, 0, accessorNative, 1), accessorReadOnlyError);
    CHECK_EQ(accessorWriteEndianUInt16(a, 0, accessorNative), accessorReadOnlyError);
    CHECK_EQ(accessorWriteEndianUInt24(a, 0, accessorNative), accessorReadOnlyError);
    CHECK_EQ(accessorWriteEndianUInt32(a, 0, accessorNative), accessorReadOnlyError);
    CHECK_EQ(accessorWriteEndianUInt64(a, 0, accessorNative), accessorReadOnlyError);
    CHECK_EQ(accessorWriteEndianInt(a, 0, accessorNative, 1), accessorReadOnlyError);
    CHECK_EQ(accessorWriteEndianInt16(a, 0, accessorNative), accessorReadOnlyError);
    CHECK_EQ(accessorWriteEndianInt24(a, 0, accessorNative), accessorReadOnlyError);
    CHECK_EQ(accessorWriteEndianInt32(a, 0, accessorNative), accessorReadOnlyError);
    CHECK_EQ(accessorWriteEndianInt64(a, 0, accessorNative), accessorReadOnlyError);
    CHECK_EQ(accessorWriteEndianFloat32(a, 0, accessorNative), accessorReadOnlyError);
    CHECK_EQ(accessorWriteEndianFloat64(a, 0, accessorNative), accessorReadOnlyError);
    CHECK_EQ(accessorWriteUInt(a, 0, 1), accessorReadOnlyError);
    CHECK_EQ(accessorWriteUInt8(a, 0), accessorReadOnlyError);
    CHECK_EQ(accessorWriteUInt16(a, 0), accessorReadOnlyError);
    CHECK_EQ(accessorWriteUInt24(a, 0), accessorReadOnlyError);
    CHECK_EQ(accessorWriteUInt32(a, 0), accessorReadOnlyError);
    CHECK_EQ(accessorWriteUInt64(a, 0), accessorReadOnlyError);
    CHECK_EQ(accessorWriteInt(a, 0, 1), accessorReadOnlyError);
    CHECK_EQ(accessorWriteInt8(a, 0), accessorReadOnlyError);
    CHECK_EQ(accessorWriteInt16(a, 0), accessorReadOnlyError);
    CHECK_EQ(accessorWriteInt24(a, 0), accessorReadOnlyError);
    CHECK_EQ(accessorWriteInt32(a, 0), accessorReadOnlyError);
    CHECK_EQ(accessorWriteInt64(a, 0), accessorReadOnlyError);
    CHECK_EQ(accessorWriteFloat32(a, 0), accessorReadOnlyError);
    CHECK_EQ(accessorWriteFloat64(a, 0), accessorReadOnlyError);
    CHECK_EQ(accessorWriteVarInt(a, 0), accessorReadOnlyError);
    CHECK_EQ(accessorWriteZigZagInt(a, 0), accessorReadOnlyError);
    CHECK_EQ(accessorWriteEndianUInt16Array(a, (uint16_t *) data, 1, accessorNative), accessorReadOnlyError);
    CHECK_EQ(accessorWriteEndianUInt24Array(a, (uint32_t *) data, 1, accessorNative), accessorReadOnlyError);
    CHECK_EQ(accessorWriteEndianUInt32Array(a, (uint32_t *) data, 1, accessorNative), accessorReadOnlyError);
    CHECK_EQ(accessorWriteEndianUInt64Array(a, (uint64_t *) data, 1, accessorNative), accessorReadOnlyError);
    CHECK_EQ(accessorWriteEndianInt16Array(a, (int16_t *) data, 1, accessorNative), accessorReadOnlyError);
    CHECK_EQ(accessorWriteEndianInt24Array(a, (int32_t *) data, 1, accessorNative), accessorReadOnlyError);
    CHECK_EQ(accessorWriteEndianInt32Array(a, (int32_t *) data, 1, accessorNative), accessorReadOnlyError);
    CHECK_EQ(accessorWriteEndianInt64Array(a, (int64_t *) data, 1, accessorNative), accessorReadOnlyError);
    CHECK_EQ(accessorWriteEndianFloat32Array(a, (float *) data, 1, accessorNative), accessorReadOnlyError);
    CHECK_EQ(accessorWriteEndianFloat64Array(a, (double *) data, 1, accessorNative), accessorReadOnlyError);
    CHECK_EQ(accessorWriteUInt16Array(a, (uint16_t *) data, 1), accessorReadOnlyError);
    CHECK_EQ(accessorWriteUInt24Array(a, (uint32_t *) data, 1), accessorReadOnlyError);
    CHECK_EQ(accessorWriteUInt32Array(a, (uint32_t *) data, 1), accessorReadOnlyError);
    CHECK_EQ(accessorWriteUInt64Array(a, (uint64_t *) data, 1), accessorReadOnlyError);
    CHECK_EQ(accessorWriteInt16Array(a, (int16_t *) data, 1), accessorReadOnlyError);
    CHECK_EQ(accessorWriteInt24Array(a, (int32_t *) data, 1), accessorReadOnlyError);
    CHECK_EQ(accessorWriteInt32Array(a, (int32_t *) data, 1), accessorReadOnlyError);
    CHECK_EQ(accessorWriteInt64Array(a, (int64_t *) data, 1), accessorReadOnlyError);
    CHECK_EQ(accessorWriteFloat32Array(a, (float *) data, 1), accessorReadOnlyError);
    CHECK_EQ(accessorWriteFloat64Array(a, (double *) data, 1), accessorReadOnlyError);
    CHECK_EQ(accessorWriteEndianBytes(a, data, 1, accessorNative), accessorReadOnlyError);
    CHECK_EQ(accessorWriteBytes(a, data, 1), accessorReadOnlyError);
    CHECK_EQ(accessorWriteRepeatedByte(a, 0, 1), accessorReadOnlyError);
    CHECK_EQ(accessorWriteCStringWithLength(a, (char *) data, 0), accessorReadOnlyError);
    CHECK_EQ(accessorWritePStringWithLength(a, (char *) data, 0), accessorReadOnlyError);
    CHECK_EQ(accessorWritePaddedStringWithLength(a, (char *) data, 0, 1, ' '), accessorReadOnlyError);
    CHECK_EQ(accessorWriteEndianString16WithLength(a, (uint16_t *) data, 0, accessorNative), accessorReadOnlyError);
    CHECK_EQ(accessorWriteEndianString32WithLength(a, (uint32_t *) data, 0, accessorNative), accessorReadOnlyError);
    CHECK_EQ(accessorWriteString16WithLength(a, (uint16_t *) data, 0), accessorReadOnlyError);
    CHECK_EQ(accessorWriteString32WithLength(a, (uint32_t *) data, 0), accessorReadOnlyError);
    CHECK_EQ(accessorWriteCString(a, (char *) data), accessorReadOnlyError);
    CHECK_EQ(accessorWritePString(a, (char *) data), accessorReadOnlyError);
    CHECK_EQ(accessorWritePaddedString(a, (char *) data, 1, ' '), accessorReadOnlyError);
    CHECK_EQ(accessorWriteEndianString16(a, (uint16_t *) data, accessorNative), accessorReadOnlyError);
    CHECK_EQ(accessorWriteEndianString32(a, (uint32_t *) data, accessorNative), accessorReadOnlyError);
    CHECK_EQ(accessorWriteString16(a, (uint16_t *) data), accessorReadOnlyError);
    CHECK_EQ(accessorWriteString32(a, (uint32_t *) data), accessorReadOnlyError);
    CHECK_EQ(accessorGetPointerForBytesToWrite(a, &ptr, 1), accessorReadOnlyError);

    CHECK_EQ(accessorClose(&a), accessorOk);
}



void testSwap(void)
{
#define SWAP_SIZE   65521
    uint8_t wdata[SWAP_SIZE];
    uint8_t sdata[SWAP_SIZE];
    uintmax_t um;
    intmax_t im;
    uint16_t u16;
    uint32_t u24;
    int32_t i24;
    uint32_t u32;
    uint64_t u64;


    for (size_t i = 0; i < SWAP_SIZE; i++) wdata[i] = sdata[i] = (uint8_t) random();
    accessorSwapBytes(sdata, sizeof(sdata));
    for (size_t i = 0; i < sizeof(sdata); i++)
        CHECK_EQ(sdata[i], wdata[sizeof(sdata) - i - 1]);
    accessorSwapBytes(sdata, sizeof(sdata));
    for (size_t i = 0; i < sizeof(sdata); i++)
        CHECK_EQ(sdata[i], wdata[i]);

    for (size_t i = 0; i < sizeof(sdata); i++)
    {
        um = (uintmax_t) random() * (uintmax_t) random();
        im = (int64_t) random() * (int32_t) random();
        u16 = (uint16_t) random();
        u24 = random() & 0x00ffffff;
        i24 = random() & 0x00ffffff; if (i24 & 0x00800000) i24 |= 0xff000000;
        u32 = (uint32_t) random();
        u64 = (uint64_t) random() * (uint64_t) random();
        CHECK_EQ(accessorSwapUInt(accessorSwapUInt(um, sizeof(uintmax_t)), sizeof(uintmax_t)), um);
        CHECK_EQ(accessorSwapUInt(accessorSwapUInt(um, 8), 8), um & 0xffffffffffffffffull);
        CHECK_EQ(accessorSwapUInt(accessorSwapUInt(um, 7), 7), um & 0x00ffffffffffffffull);
        CHECK_EQ(accessorSwapUInt(accessorSwapUInt(um, 6), 6), um & 0x0000ffffffffffffull);
        CHECK_EQ(accessorSwapUInt(accessorSwapUInt(um, 5), 5), um & 0x000000ffffffffffull);
        CHECK_EQ(accessorSwapUInt(accessorSwapUInt(um, 4), 4), um & 0x00000000ffffffffull);
        CHECK_EQ(accessorSwapUInt(accessorSwapUInt(um, 3), 3), um & 0x0000000000ffffffull);
        CHECK_EQ(accessorSwapUInt(accessorSwapUInt(um, 2), 2), um & 0x000000000000ffffull);
        CHECK_EQ(accessorSwapUInt(accessorSwapUInt(um, 1), 1), um & 0x00000000000000ffull);
        CHECK_EQ(accessorSwapInt(accessorSwapInt(im, sizeof(intmax_t)), sizeof(intmax_t)), im);
        im &= 0xffffffffffffffffll;
        CHECK_EQ(accessorSwapInt(accessorSwapInt(im, 8), 8), im);
        im &= 0x00ffffffffffffffll; if (im & 0x0080000000000000ll) im |= 0xff00000000000000ll;
        CHECK_EQ(accessorSwapInt(accessorSwapInt(im, 7), 7), im);
        im &= 0x0000ffffffffffffll; if (im & 0x0000800000000000ll) im |= 0xffff000000000000ll;
        CHECK_EQ(accessorSwapInt(accessorSwapInt(im, 6), 6), im);
        im &= 0x000000ffffffffffll; if (im & 0x0000008000000000ll) im |= 0xffffff0000000000ll;
        CHECK_EQ(accessorSwapInt(accessorSwapInt(im, 5), 5), im);
        im &= 0x00000000ffffffffll; if (im & 0x0000000080000000ll) im |= 0xffffffff00000000ll;
        CHECK_EQ(accessorSwapInt(accessorSwapInt(im, 4), 4), im);
        im &= 0x0000000000ffffffll; if (im & 0x0000000000800000ll) im |= 0xffffffffff000000ll;
        CHECK_EQ(accessorSwapInt(accessorSwapInt(im, 3), 3), im);
        im &= 0x000000000000ffffll; if (im & 0x0000000000008000ll) im |= 0xffffffffffff0000ll;
        CHECK_EQ(accessorSwapInt(accessorSwapInt(im, 2), 2), im);
        im &= 0x00000000000000ffll; if (im & 0x0000000000000080ll) im |= 0xffffffffffffff00l;
        CHECK_EQ(accessorSwapInt(accessorSwapInt(im, 1), 1), im);
        CHECK_EQ(accessorSwapUInt16(accessorSwapUInt16(u16)), u16);
        CHECK_EQ(accessorSwapUInt24(accessorSwapUInt24(u24)), u24);
        CHECK_EQ(accessorSwapInt24(accessorSwapInt24(i24)), i24);
        CHECK_EQ(accessorSwapInt24(accessorSwapInt24(i24 ^ (int32_t) 0xff800000)), i24 ^ (int32_t) 0xff800000);
        CHECK_EQ(accessorSwapUInt32(accessorSwapUInt32(u32)), u32);
        CHECK_EQ(accessorSwapUInt64(accessorSwapUInt64(u64)), u64);
    }
}



void testGetPointer(void)
{
#define GETPOINTER_SIZE 65521
    accessor_t * a = ACCESSOR_INIT;
    uint8_t wdata[GETPOINTER_SIZE];
    void * ptr;
    const void * rptr;


    for (size_t i = 0; i < GETPOINTER_SIZE; i++) wdata[i] = (uint8_t) random();

    CHECK_EQ(accessorOpenWritingMemory(&a, 0, 0), accessorOk);

    CHECK_EQ(accessorGetPointerForBytesToWrite(a, &ptr, sizeof(wdata)), accessorOk);
    memcpy(ptr, wdata, sizeof(wdata));
    ptr = NULL;

    CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);

    CHECK_EQ(accessorGetPointerForBytesToRead(a, &rptr, sizeof(wdata)), accessorOk);
    CHECK_EQ(memcmp(rptr, wdata, sizeof((wdata))), 0);

    CHECK_EQ(accessorClose(&a), accessorOk);
}



void testStrings(void)
{
#define STRING_SIZE 65521   // must be > 256
    accessor_t * a = ACCESSOR_INIT;
    char str8[STRING_SIZE + 1];
    uint16_t str16[STRING_SIZE + 1];
    uint32_t str32[STRING_SIZE + 1];
    char * rstr8;
    uint16_t * rstr16;
    uint32_t * rstr32;
    size_t count;
    size_t rcount;


    for (size_t i = 0; i < STRING_SIZE; i++)
    {
        do { str8[i] = (char) random(); } while (str8[i]  == 0 || str8[i] == '*');  // also exclude '*' as it is used for padding
        do { str16[i] = (uint16_t) random(); } while (str16[i] == 0);
        do { str32[i] = (uint32_t) random(); } while (str32[i] == 0);
    }
    str8[STRING_SIZE] = 0;
    str16[STRING_SIZE] = 0;
    str32[STRING_SIZE] = 0;

    CHECK_EQ(accessorOpenWritingMemory(&a, 0, 0), accessorOk);

    for (int e = 0; e < ACCESSOR_ENDIANNESS_COUNT; e++)
    {
        CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);
        CHECK_EQ(accessorTruncate(a), accessorOk);

        count = strlen(str8);
        CHECK_EQ(accessorWriteCString(a, str8), accessorOk);
        CHECK_EQ(accessorWritePString(a, str8 + STRING_SIZE - 256), accessorInvalidParameter);
        CHECK_EQ(accessorWritePString(a, str8 + STRING_SIZE - 255), accessorOk);
        CHECK_EQ(accessorWritePaddedString(a, str8, count * 2 + 7, '*'), accessorOk);
        CHECK_EQ(accessorWriteEndianString16(a, str16, endianness[e]), accessorOk);
        CHECK_EQ(accessorWriteEndianString32(a, str32, endianness[e]), accessorOk);
        CHECK_EQ(accessorWriteString16(a, str16), accessorOk);
        CHECK_EQ(accessorWriteString32(a, str32), accessorOk);

        CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);

        CHECK_EQ(accessorReadFixedLengthString(a, &rstr8, count / 2), accessorOk);
        CHECK_EQ(memcmp(rstr8, str8, count / 2), 0);
        CHECK_EQ(rstr8[count / 2], 0);
        free(rstr8);

        CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);

        CHECK_EQ(accessorReadCString(a, &rstr8, NULL), accessorOk);
        CHECK_EQ(strcmp(rstr8, str8), 0);
        free(rstr8);
        CHECK_EQ(accessorReadPString(a, &rstr8, &rcount), accessorOk);
        CHECK_EQ(rcount, 255);
        CHECK_EQ(strcmp(rstr8, str8 + STRING_SIZE - 255), 0);
        free(rstr8);
        rcount = count * 2 + 7;
        CHECK_EQ(accessorReadPaddedString(a, &rstr8, &rcount, '*'), accessorOk);
        CHECK_EQ(rcount, count);
        CHECK_EQ(strcmp(rstr8, str8), 0);
        free(rstr8);
        CHECK_EQ(accessorReadEndianString16(a, &rstr16, &rcount, endianness[e]), accessorOk);
        CHECK_EQ(rcount, STRING_SIZE);
        CHECK_EQ(memcmp(rstr16, str16, rcount), 0);
        CHECK_EQ(rstr16[rcount], 0);
        free(rstr16);
        CHECK_EQ(accessorReadEndianString32(a, &rstr32, &rcount, endianness[e]), accessorOk);
        CHECK_EQ(rcount, STRING_SIZE);
        CHECK_EQ(memcmp(rstr32, str32, rcount), 0);
        CHECK_EQ(rstr32[rcount], 0);
        free(rstr32);
        CHECK_EQ(accessorReadString16(a, &rstr16, &rcount), accessorOk);
        CHECK_EQ(rcount, STRING_SIZE);
        CHECK_EQ(memcmp(rstr16, str16, rcount), 0);
        CHECK_EQ(rstr16[rcount], 0);
        free(rstr16);
        CHECK_EQ(accessorReadString32(a, &rstr32, &rcount), accessorOk);
        CHECK_EQ(rcount, STRING_SIZE);
        CHECK_EQ(memcmp(rstr32, str32, rcount), 0);
        CHECK_EQ(rstr32[rcount], 0);
        free(rstr32);
    }

    CHECK_EQ(accessorClose(&a), accessorOk);
}



void testLookAhead(void)
{
#define LOOK_AHEAD_SIZE 65521
    accessor_t * a = ACCESSOR_INIT;
    uint8_t wdata[LOOK_AHEAD_SIZE];
    uint8_t rdata[LOOK_AHEAD_SIZE];
    const void * ptr;
    char * delimiter = "delimiter"; // must start with 'd', must not contain 'f' and be longer than 3 characters
    size_t count;


    for (size_t i = 0; i < LOOK_AHEAD_SIZE; i++) do wdata[i] = (uint8_t) random(); while (wdata[i] == 'd' || wdata[i] == 'f');

    CHECK_EQ(accessorOpenWritingMemory(&a, 0, 0), accessorOk);

    for (int e = 0; e < ACCESSOR_ENDIANNESS_COUNT; e++)
    {
        CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);
        CHECK_EQ(accessorTruncate(a), accessorOk);

        CHECK_EQ(accessorWriteEndianBytes(a, wdata, LOOK_AHEAD_SIZE, endianness[e]), accessorOk);
        CHECK_EQ(accessorWriteBytes(a, wdata, LOOK_AHEAD_SIZE), accessorOk);

        CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);

        CHECK_EQ(accessorLookAheadEndianBytes(a, rdata, LOOK_AHEAD_SIZE, endianness[e]), LOOK_AHEAD_SIZE);
        CHECK_EQ(memcmp(rdata, wdata, LOOK_AHEAD_SIZE), 0);
        CHECK_EQ(accessorSeek(a, LOOK_AHEAD_SIZE, SEEK_CUR), accessorOk);
        CHECK_EQ(accessorLookAheadBytes(a, rdata, LOOK_AHEAD_SIZE), LOOK_AHEAD_SIZE);
        CHECK_EQ(memcmp(rdata, wdata, LOOK_AHEAD_SIZE), 0);
        CHECK_EQ(accessorLookAheadAvailableBytes(a, &ptr), LOOK_AHEAD_SIZE);
        CHECK_EQ(memcmp(ptr, wdata, LOOK_AHEAD_SIZE), 0);
    }
    CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);
    CHECK_EQ(accessorTruncate(a), accessorOk);

    CHECK_EQ(accessorWriteBytes(a, wdata, LOOK_AHEAD_SIZE), accessorOk);
    CHECK_EQ(accessorWritePaddedStringWithLength(a, delimiter, strlen(delimiter), strlen(delimiter), 0x00), accessorOk);

    CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);
    CHECK_EQ(accessorLookAheadCountBytesBeforeDelimiter(a, &count, ACCESSOR_UNTIL_END, strlen(delimiter), delimiter), accessorOk);
    CHECK_EQ(count, LOOK_AHEAD_SIZE);

    CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);
    CHECK_EQ(accessorLookAheadCountBytesBeforeDelimiter(a, &count, ACCESSOR_UNTIL_END, 1, delimiter), accessorOk);
    CHECK_EQ(count, LOOK_AHEAD_SIZE);

    CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);
    CHECK_EQ(accessorLookAheadCountBytesBeforeDelimiter(a, &count, ACCESSOR_UNTIL_END, 2, delimiter), accessorOk);
    CHECK_EQ(count, LOOK_AHEAD_SIZE);

    CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);
    CHECK_EQ(accessorLookAheadCountBytesBeforeDelimiter(a, &count, ACCESSOR_UNTIL_END, 1, "f"), accessorBeyondEnd);

    CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);
    CHECK_EQ(accessorLookAheadCountBytesBeforeDelimiter(a, &count, ACCESSOR_UNTIL_END, 2, "fg"), accessorBeyondEnd);

    CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);
    CHECK_EQ(accessorLookAheadCountBytesBeforeDelimiter(a, &count, ACCESSOR_UNTIL_END, 3, "fgh"), accessorBeyondEnd);

    CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);
    CHECK_EQ(accessorTruncate(a), accessorOk);

    CHECK_EQ(accessorWriteRepeatedByte(a, 0x87, LOOK_AHEAD_SIZE), accessorOk);
    CHECK_EQ(accessorWritePaddedStringWithLength(a, delimiter, strlen(delimiter), strlen(delimiter), 0x00), accessorOk);

    CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);

    CHECK_EQ(accessorLookAheadCountBytesBeforeDelimiter(a, &count, ACCESSOR_UNTIL_END, strlen(delimiter), delimiter), accessorOk);
    CHECK_EQ(count, LOOK_AHEAD_SIZE);

    CHECK_EQ(accessorClose(&a), accessorOk);
}



void testBlocks(void)
{
#define BLOCK_SIZE  65521
    accessor_t * a = ACCESSOR_INIT;
    uint8_t wdata[BLOCK_SIZE];
    void * rdata;
    uint8_t wu8;
    uint8_t u8;


    for (size_t i = 0; i < BLOCK_SIZE; i++) wdata[i] = (uint8_t) random();
    wu8 = (uint8_t) random();

    CHECK_EQ(accessorOpenWritingMemory(&a, 0, 0), accessorOk);

    for (int e = 0; e < ACCESSOR_ENDIANNESS_COUNT; e++)
    {
        CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);
        CHECK_EQ(accessorTruncate(a), accessorOk);

        CHECK_EQ(accessorWriteUInt8(a, wu8), accessorOk);
        CHECK_EQ(accessorWriteEndianBytes(a, wdata, BLOCK_SIZE, endianness[e]), accessorOk);
        CHECK_EQ(accessorWriteBytes(a, wdata, BLOCK_SIZE), accessorOk);
        CHECK_EQ(accessorWriteRepeatedByte(a, wu8, BLOCK_SIZE), accessorOk);

        CHECK_EQ(accessorSize(a), 1 + 3 * BLOCK_SIZE);

        CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);

        CHECK_EQ(accessorReadUInt8(a, &u8), accessorOk);
        CHECK_EQ(u8, wu8);
        CHECK_EQ(accessorReadAllocatedEndianBytes(a, &rdata, BLOCK_SIZE, endianness[e]), accessorOk);
        CHECK_EQ(memcmp(rdata, wdata, BLOCK_SIZE), 0);
        free(rdata);
        CHECK_EQ(accessorReadAllocatedBytes(a, &rdata, BLOCK_SIZE), accessorOk);
        CHECK_EQ(memcmp(rdata, wdata, BLOCK_SIZE), 0);
        // don't free(rdata) yet: it will be re-used as it is allocated and has the right size
        CHECK_EQ(accessorReadBytes(a, rdata, BLOCK_SIZE), accessorOk);
        for (size_t i = 0; i < BLOCK_SIZE; i++) CHECK_EQ(((uint8_t *) rdata)[i], wu8);

        CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);

        CHECK_EQ(accessorReadUInt8(a, &u8), accessorOk);
        CHECK_EQ(u8, wu8);
        CHECK_EQ(accessorReadEndianBytes(a, rdata, BLOCK_SIZE, endianness[e]), accessorOk);
        CHECK_EQ(memcmp(rdata, wdata, BLOCK_SIZE), 0);
        CHECK_EQ(accessorReadBytes(a, rdata, BLOCK_SIZE), accessorOk);
        CHECK_EQ(memcmp(rdata, wdata, BLOCK_SIZE), 0);
        // don't free(rdata) yet: it will be re-used as it is allocated and has the right size
        CHECK_EQ(accessorReadBytes(a, rdata, BLOCK_SIZE), accessorOk);
        for (size_t i = 0; i < BLOCK_SIZE; i++) CHECK_EQ(((uint8_t *) rdata)[i], wu8);

        // free(rdata) now, it won't be used anymore
        free(rdata);
    }

    CHECK_EQ(accessorClose(&a), accessorOk);
}



void testArrays(void)
{
#define ARRAY_SIZE  65521
    accessor_t * a = ACCESSOR_INIT;
    float    wf32[ARRAY_SIZE];
    double   wf64[ARRAY_SIZE];
    float    * rf32;
    double   * rf64;
    uint16_t wu16[ARRAY_SIZE];
    uint32_t wu24[ARRAY_SIZE];
    uint32_t wu32[ARRAY_SIZE];
    uint64_t wu64[ARRAY_SIZE];
    uint8_t  u8;
    uint16_t * ru16;
    uint32_t * ru24;
    uint32_t * ru32;
    uint64_t * ru64;
    int16_t wi16[ARRAY_SIZE];
    int32_t wi24[ARRAY_SIZE];
    int32_t wi32[ARRAY_SIZE];
    int64_t wi64[ARRAY_SIZE];
    int8_t  i8;
    int16_t * ri16;
    int32_t * ri24;
    int32_t * ri32;
    int64_t * ri64;


    for (size_t i = 0; i < ARRAY_SIZE; i++) wu16[i] = (uint16_t) random();
    for (size_t i = 0; i < ARRAY_SIZE; i++) wu24[i] = random() & 0x00ffffff;
    for (size_t i = 0; i < ARRAY_SIZE; i++) wu32[i] = (uint32_t) random();
    for (size_t i = 0; i < ARRAY_SIZE; i++) wu64[i] = (uint64_t) random() * (uint64_t) random();
    for (size_t i = 0; i < ARRAY_SIZE; i++) wi16[i] = (int16_t) random();
    for (size_t i = 0; i < ARRAY_SIZE; i++) { wi24[i] = random() & 0x00ffffff; if (wi24[i] & 0x00800000) wi24[i] |= 0xff000000; }
    for (size_t i = 0; i < ARRAY_SIZE; i++) wi32[i] = (int32_t) random();
    for (size_t i = 0; i < ARRAY_SIZE; i++) wi64[i] = (int64_t) random() * (int32_t) random();
    for (size_t i = 0; i < ARRAY_SIZE; i++) wf32[i] = (float) (int32_t) random() / INT32_MAX;
    for (size_t i = 0; i < ARRAY_SIZE; i++) wf64[i] = (double) (int32_t) random() * random() / INT64_MAX;

    CHECK_EQ(accessorOpenWritingMemory(&a, 0, 0), accessorOk);

    for (int e = 0; e < ACCESSOR_ENDIANNESS_COUNT; e++)
    {
        CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);
        CHECK_EQ(accessorTruncate(a), accessorOk);

        CHECK_EQ(accessorWriteUInt8(a, 0x87), accessorOk);
        CHECK_EQ(accessorWriteEndianUInt16Array(a, wu16, ARRAY_SIZE, endianness[e]), accessorOk);
        CHECK_EQ(accessorWriteEndianUInt24Array(a, wu24, ARRAY_SIZE, endianness[e]), accessorOk);
        CHECK_EQ(accessorWriteEndianUInt32Array(a, wu32, ARRAY_SIZE, endianness[e]), accessorOk);
        CHECK_EQ(accessorWriteEndianUInt64Array(a, wu64, ARRAY_SIZE, endianness[e]), accessorOk);
        CHECK_EQ(accessorWriteEndianFloat32Array(a, wf32, ARRAY_SIZE, endianness[e]), accessorOk);
        CHECK_EQ(accessorWriteEndianFloat64Array(a, wf64, ARRAY_SIZE, endianness[e]), accessorOk);

        CHECK_EQ(accessorSize(a), 1 + 2 * ARRAY_SIZE + 3 * ARRAY_SIZE + 4 * ARRAY_SIZE + 8 * ARRAY_SIZE + 4 * ARRAY_SIZE + 8 * ARRAY_SIZE);

        CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);

        CHECK_EQ(accessorReadUInt8(a, &u8), accessorOk);
        CHECK_EQ(u8, 0x87);
        CHECK_EQ(accessorReadEndianUInt16Array(a, &ru16, ARRAY_SIZE, endianness[e]), accessorOk);
        for (size_t i = 0; i < ARRAY_SIZE; i++) CHECK_EQ(ru16[i], wu16[i]);
        free(ru16);
        CHECK_EQ(accessorReadEndianUInt24Array(a, &ru24, ARRAY_SIZE, endianness[e]), accessorOk);
        for (size_t i = 0; i < ARRAY_SIZE; i++) CHECK_EQ(ru24[i], wu24[i]);
        free(ru24);
        CHECK_EQ(accessorReadEndianUInt32Array(a, &ru32, ARRAY_SIZE, endianness[e]), accessorOk);
        for (size_t i = 0; i < ARRAY_SIZE; i++) CHECK_EQ(ru32[i], wu32[i]);
        free(ru32);
        CHECK_EQ(accessorReadEndianUInt64Array(a, &ru64, ARRAY_SIZE, endianness[e]), accessorOk);
        for (size_t i = 0; i < ARRAY_SIZE; i++) CHECK_EQ(ru64[i], wu64[i]);
        free(ru64);
        CHECK_EQ(accessorReadEndianFloat32Array(a, &rf32, ARRAY_SIZE, endianness[e]), accessorOk);
        for (size_t i = 0; i < ARRAY_SIZE; i++) CHECK_EQ(rf32[i], wf32[i]);
        free(rf32);
        CHECK_EQ(accessorReadEndianFloat64Array(a, &rf64, ARRAY_SIZE, endianness[e]), accessorOk);
        for (size_t i = 0; i < ARRAY_SIZE; i++) CHECK_EQ(rf64[i], wf64[i]);
        free(rf64);

        CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);
        CHECK_EQ(accessorTruncate(a), accessorOk);

        CHECK_EQ(accessorWriteInt8(a, -0x79), accessorOk);
        CHECK_EQ(accessorWriteEndianInt16Array(a, wi16, ARRAY_SIZE, endianness[e]), accessorOk);
        CHECK_EQ(accessorWriteEndianInt24Array(a, wi24, ARRAY_SIZE, endianness[e]), accessorOk);
        CHECK_EQ(accessorWriteEndianInt32Array(a, wi32, ARRAY_SIZE, endianness[e]), accessorOk);
        CHECK_EQ(accessorWriteEndianInt64Array(a, wi64, ARRAY_SIZE, endianness[e]), accessorOk);

        CHECK_EQ(accessorSize(a), 1 + 2 * ARRAY_SIZE + 3 * ARRAY_SIZE + 4 * ARRAY_SIZE + 8 * ARRAY_SIZE);

        CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);

        CHECK_EQ(accessorReadInt8(a, &i8), accessorOk);
        CHECK_EQ(i8, -0x79);
        CHECK_EQ(accessorReadEndianInt16Array(a, &ri16, ARRAY_SIZE, endianness[e]), accessorOk);
        for (size_t i = 0; i < ARRAY_SIZE; i++) CHECK_EQ(ri16[i], wi16[i]);
        free(ri16);
        CHECK_EQ(accessorReadEndianInt24Array(a, &ri24, ARRAY_SIZE, endianness[e]), accessorOk);
        for (size_t i = 0; i < ARRAY_SIZE; i++) CHECK_EQ(ri24[i], wi24[i]);
        free(ri24);
        CHECK_EQ(accessorReadEndianInt32Array(a, &ri32, ARRAY_SIZE, endianness[e]), accessorOk);
        for (size_t i = 0; i < ARRAY_SIZE; i++) CHECK_EQ(ri32[i], wi32[i]);
        free(ri32);
        CHECK_EQ(accessorReadEndianInt64Array(a, &ri64, ARRAY_SIZE, endianness[e]), accessorOk);
        for (size_t i = 0; i < ARRAY_SIZE; i++) CHECK_EQ(ri64[i], wi64[i]);
        free(ri64);
    }

    CHECK_EQ(accessorClose(&a), accessorOk);
}



void testNumbers(void)
{
    accessor_t * a = ACCESSOR_INIT;
    uint8_t u8;
    uint16_t u16;
    uint32_t u24;
    uint32_t u32;
    uint64_t u64;
    uintmax_t um;
    int8_t i8;
    int16_t i16;
    int32_t i24;
    int32_t i32;
    int64_t i64;
    intmax_t im;
    float f32;
    double f64;
    size_t count;


    CHECK_EQ(accessorOpenWritingMemory(&a, 0, 0), accessorOk);

    CHECK_EQ(accessorWriteUInt8(a, 0x87), accessorOk);
    CHECK_EQ(accessorWriteUInt16(a, 0x8765), accessorOk);
    CHECK_EQ(accessorWriteUInt24(a, 0x876543), accessorOk);
    CHECK_EQ(accessorWriteUInt24(a, 0x776543), accessorOk);
    CHECK_EQ(accessorWriteUInt32(a, 0x87654321), accessorOk);
    CHECK_EQ(accessorWriteUInt64(a, 0x876543210fedcba9), accessorOk);
    CHECK_EQ(accessorWriteUInt(a, 0x876543210fedcb, 7), accessorOk);
    CHECK_EQ(accessorWriteUInt(a, 0x776543210fedcb, 7), accessorOk);
    CHECK_EQ(accessorWriteFloat32(a, (float) -0.1234567890123456789), accessorOk);
    CHECK_EQ(accessorWriteFloat64(a, -0.1234567890123456789), accessorOk);
    CHECK_EQ(accessorWriteVarInt(a, 0x876543210fedcba9), accessorOk);
    CHECK_EQ(accessorWriteZigZagInt(a, 0x776543210fedcba9), accessorOk);
    CHECK_EQ(accessorWriteZigZagInt(a, -0x776543210fedcba9), accessorOk);

    CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);

    CHECK_EQ(accessorReadUInt8(a, &u8), accessorOk);
    CHECK_EQ(u8, 0x87);
    CHECK_EQ(accessorReadUInt16(a, &u16), accessorOk);
    CHECK_EQ(u16, 0x8765);
    CHECK_EQ(accessorReadUInt24(a, &u24), accessorOk);
    CHECK_EQ(u24, 0x876543);
    CHECK_EQ(accessorReadUInt24(a, &u24), accessorOk);
    CHECK_EQ(u24, 0x776543);
    CHECK_EQ(accessorReadUInt32(a, &u32), accessorOk);
    CHECK_EQ(u32, 0x87654321);
    CHECK_EQ(accessorReadUInt64(a, &u64), accessorOk);
    CHECK_EQ(u64, 0x876543210fedcba9);
    CHECK_EQ(accessorReadUInt(a, &um, 7), accessorOk);
    CHECK_EQ(um, 0x876543210fedcb);
    CHECK_EQ(accessorReadUInt(a, &um, 7), accessorOk);
    CHECK_EQ(um, 0x776543210fedcb);
    CHECK_EQ(accessorReadFloat32(a, &f32), accessorOk);
    CHECK_EQ(f32, (float) -0.1234567890123456789);
    CHECK_EQ(accessorReadFloat64(a, &f64), accessorOk);
    CHECK_EQ(f64, -0.1234567890123456789);
    CHECK_EQ(accessorReadVarInt(a, &um), accessorOk);
    CHECK_EQ(um, 0x876543210fedcba9);
    CHECK_EQ(accessorReadZigZagInt(a, &im), accessorOk);
    CHECK_EQ(im, 0x776543210fedcba9);
    CHECK_EQ(accessorReadZigZagInt(a, &im), accessorOk);
    CHECK_EQ(im, -0x776543210fedcba9);

    CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);

    CHECK_EQ(accessorReadInt8(a, &i8), accessorOk);
    CHECK_EQ(i8, -0x79);
    CHECK_EQ(accessorReadInt16(a, &i16), accessorOk);
    CHECK_EQ(i16, -0x789b);
    CHECK_EQ(accessorReadInt24(a, &i24), accessorOk);
    CHECK_EQ(i24, -0x789abd);
    CHECK_EQ(accessorReadInt24(a, &i24), accessorOk);
    CHECK_EQ(i24, 0x776543);
    CHECK_EQ(accessorReadInt32(a, &i32), accessorOk);
    CHECK_EQ(i32, -0x789abcdf);
    CHECK_EQ(accessorReadInt64(a, &i64), accessorOk);
    CHECK_EQ(i64, -0x789abcdef0123457);
    CHECK_EQ(accessorReadInt(a, &im, 7), accessorOk);
    CHECK_EQ(im, -0x789abcdef01235);
    CHECK_EQ(accessorReadInt(a, &im, 7), accessorOk);
    CHECK_EQ(im, 0x776543210fedcb);
    CHECK_EQ(accessorReadFloat32(a, &f32), accessorOk);
    CHECK_EQ(f32, (float) -0.1234567890123456789);
    CHECK_EQ(accessorReadFloat64(a, &f64), accessorOk);
    CHECK_EQ(f64, -0.1234567890123456789);
    CHECK_EQ(accessorReadVarInt(a, &um), accessorOk);
    CHECK_EQ(um, 0x876543210fedcba9);
    CHECK_EQ(accessorReadZigZagInt(a, &im), accessorOk);
    CHECK_EQ(im, 0x776543210fedcba9);
    CHECK_EQ(accessorReadZigZagInt(a, &im), accessorOk);
    CHECK_EQ(im, -0x776543210fedcba9);

    count = accessorAvailableBytesCount(a);
    CHECK_EQ(count, 0);

    for (int e = 0; e < ACCESSOR_ENDIANNESS_COUNT; e++)
    {
        CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);
        CHECK_EQ(accessorTruncate(a), accessorOk);

        CHECK_EQ(accessorWriteUInt8(a, 0x87), accessorOk);
        CHECK_EQ(accessorWriteEndianUInt16(a, 0x8765, endianness[e]), accessorOk);
        CHECK_EQ(accessorWriteEndianUInt24(a, 0x876543, endianness[e]), accessorOk);
        CHECK_EQ(accessorWriteEndianUInt24(a, 0x776543, endianness[e]), accessorOk);
        CHECK_EQ(accessorWriteEndianUInt32(a, 0x87654321, endianness[e]), accessorOk);
        CHECK_EQ(accessorWriteEndianUInt64(a, 0x876543210fedcba9, endianness[e]), accessorOk);
        CHECK_EQ(accessorWriteEndianUInt(a, 0x876543210fedcb, endianness[e], 7), accessorOk);
        CHECK_EQ(accessorWriteEndianUInt(a, 0x776543210fedcb, endianness[e], 7), accessorOk);
        CHECK_EQ(accessorWriteEndianFloat32(a, (float) -0.1234567890123456789, endianness[e]), accessorOk);
        CHECK_EQ(accessorWriteEndianFloat64(a, -0.1234567890123456789, endianness[e]), accessorOk);

        CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);

        CHECK_EQ(accessorReadUInt8(a, &u8), accessorOk);
        CHECK_EQ(u8, 0x87);
        CHECK_EQ(accessorReadEndianUInt16(a, &u16, endianness[e]), accessorOk);
        CHECK_EQ(u16, 0x8765);
        CHECK_EQ(accessorReadEndianUInt24(a, &u24, endianness[e]), accessorOk);
        CHECK_EQ(u24, 0x876543);
        CHECK_EQ(accessorReadEndianUInt24(a, &u24, endianness[e]), accessorOk);
        CHECK_EQ(u24, 0x776543);
        CHECK_EQ(accessorReadEndianUInt32(a, &u32, endianness[e]), accessorOk);
        CHECK_EQ(u32, 0x87654321);
        CHECK_EQ(accessorReadEndianUInt64(a, &u64, endianness[e]), accessorOk);
        CHECK_EQ(u64, 0x876543210fedcba9);
        CHECK_EQ(accessorReadEndianUInt(a, &um, endianness[e], 7), accessorOk);
        CHECK_EQ(um, 0x876543210fedcb);
        CHECK_EQ(accessorReadEndianUInt(a, &um, endianness[e], 7), accessorOk);
        CHECK_EQ(um, 0x776543210fedcb);
        CHECK_EQ(accessorReadEndianFloat32(a, &f32, endianness[e]), accessorOk);
        CHECK_EQ(f32, (float) -0.1234567890123456789);
        CHECK_EQ(accessorReadEndianFloat64(a, &f64, endianness[e]), accessorOk);
        CHECK_EQ(f64, -0.1234567890123456789);

        CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);

        CHECK_EQ(accessorReadUInt8(a, &u8), accessorOk);
        CHECK_EQ(u8, 0x87);
        CHECK_EQ(accessorReadEndianUInt16(a, &u16, accessorOppositeEndianness(endianness[e])), accessorOk);
        CHECK_EQ(u16, 0x6587);
        CHECK_EQ(accessorReadEndianUInt24(a, &u24, accessorOppositeEndianness(endianness[e])), accessorOk);
        CHECK_EQ(u24, 0x436587);
        CHECK_EQ(accessorReadEndianUInt24(a, &u24, accessorOppositeEndianness(endianness[e])), accessorOk);
        CHECK_EQ(u24, 0x436577);
        CHECK_EQ(accessorReadEndianUInt32(a, &u32, accessorOppositeEndianness(endianness[e])), accessorOk);
        CHECK_EQ(u32, 0x21436587);
        CHECK_EQ(accessorReadEndianUInt64(a, &u64, accessorOppositeEndianness(endianness[e])), accessorOk);
        CHECK_EQ(u64, 0xa9cbed0f21436587);
        CHECK_EQ(accessorReadEndianUInt(a, &um, accessorOppositeEndianness(endianness[e]), 7), accessorOk);
        CHECK_EQ(um, 0xcbed0f21436587);
        CHECK_EQ(accessorReadEndianUInt(a, &um, accessorOppositeEndianness(endianness[e]), 7), accessorOk);
        CHECK_EQ(um, 0xcbed0f21436577);
        count = accessorAvailableBytesCount(a);
        CHECK_EQ(count, 4 + 8);
    }

    for (int e = 0; e < ACCESSOR_ENDIANNESS_COUNT; e++)
    {
        CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);
        CHECK_EQ(accessorTruncate(a), accessorOk);

        CHECK_EQ(accessorWriteInt8(a, -0x79), accessorOk);
        CHECK_EQ(accessorWriteEndianInt16(a, -0x789b, endianness[e]), accessorOk);
        CHECK_EQ(accessorWriteEndianInt24(a, -0x789abd, endianness[e]), accessorOk);
        CHECK_EQ(accessorWriteEndianInt24(a, 0x776543, endianness[e]), accessorOk);
        CHECK_EQ(accessorWriteEndianInt32(a, -0x789abcdf, endianness[e]), accessorOk);
        CHECK_EQ(accessorWriteEndianInt64(a, -0x789abcdef0123457, endianness[e]), accessorOk);
        CHECK_EQ(accessorWriteEndianInt(a, -0x789abcdef01235, endianness[e], 7), accessorOk);
        CHECK_EQ(accessorWriteEndianInt(a, 0x776543210fedcb, endianness[e], 7), accessorOk);

        CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);

        CHECK_EQ(accessorReadInt8(a, &i8), accessorOk);
        CHECK_EQ(i8, -0x79);
        CHECK_EQ(accessorReadEndianInt16(a, &i16, endianness[e]), accessorOk);
        CHECK_EQ(i16, -0x789b);
        CHECK_EQ(accessorReadEndianInt24(a, &i24, endianness[e]), accessorOk);
        CHECK_EQ(i24, -0x789abd);
        CHECK_EQ(accessorReadEndianInt24(a, &i24, endianness[e]), accessorOk);
        CHECK_EQ(i24, 0x776543);
        CHECK_EQ(accessorReadEndianInt32(a, &i32, endianness[e]), accessorOk);
        CHECK_EQ(i32, -0x789abcdf);
        CHECK_EQ(accessorReadEndianInt64(a, &i64, endianness[e]), accessorOk);
        CHECK_EQ(i64, -0x789abcdef0123457);
        CHECK_EQ(accessorReadEndianInt(a, &im, endianness[e], 7), accessorOk);
        CHECK_EQ(im, -0x789abcdef01235);
        CHECK_EQ(accessorReadEndianInt(a, &im, endianness[e], 7), accessorOk);
        CHECK_EQ(im, 0x776543210fedcb);

        CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);

        CHECK_EQ(accessorReadInt8(a, &i8), accessorOk);
        CHECK_EQ(i8, -0x79);
        CHECK_EQ(accessorReadEndianInt16(a, &i16, accessorOppositeEndianness(endianness[e])), accessorOk);
        CHECK_EQ(i16, 0x6587);
        CHECK_EQ(accessorReadEndianInt24(a, &i24, accessorOppositeEndianness(endianness[e])), accessorOk);
        CHECK_EQ(i24, 0x436587);
        CHECK_EQ(accessorReadEndianInt24(a, &i24, accessorOppositeEndianness(endianness[e])), accessorOk);
        CHECK_EQ(i24, 0x436577);
        CHECK_EQ(accessorReadEndianInt32(a, &i32, accessorOppositeEndianness(endianness[e])), accessorOk);
        CHECK_EQ(i32, 0x21436587);
        CHECK_EQ(accessorReadEndianInt64(a, &i64, accessorOppositeEndianness(endianness[e])), accessorOk);
        CHECK_EQ(i64, -0x563412f0debc9a79);
        CHECK_EQ(accessorReadEndianInt(a, &im, accessorOppositeEndianness(endianness[e]), 7), accessorOk);
        CHECK_EQ(im, -0x3412f0debc9a79);
        CHECK_EQ(accessorReadEndianInt(a, &im, accessorOppositeEndianness(endianness[e]), 7), accessorOk);
        CHECK_EQ(im, -0x3412f0debc9a89);
    }

    count = accessorAvailableBytesCount(a);
    CHECK_EQ(count, 0);

    CHECK_EQ(accessorClose(&a), accessorOk);
}



void testOpen(void)
{
    accessor_t * a = ACCESSOR_INIT;
    accessor_t * b = ACCESSOR_INIT;
    char dirPath[256] = "\\\\tmp\\accessorTest.XXXXXXXX";
    char subDirPath[256] = "XXXXXXXX";
    char * filename = "test.bin";
    char * fullDirPath;
    char * fullFilePath;
    uint32_t writtenData[16];
    uint32_t readData[16];
    const void * ptr;
    size_t count;


    for (size_t i = 0; i < sizeof(writtenData) / sizeof(*writtenData); i++)
        writtenData[i] = (uint32_t) random();

    mktemp(dirPath);
    mktemp(subDirPath);
    CHECK_EQ(accessorBuildPath(&fullDirPath, dirPath, subDirPath, accessorPathOptionNone | accessorPathOptionConvertBackslash | accessorPathOptionCreatePath, 0), accessorOk);
    CHECK_EQ(accessorOpenWritingFile(&a, fullDirPath, filename, accessorPathOptionNone | accessorPathOptionCreatePath, 0666, 0, 0), accessorOk);
    CHECK_EQ(accessorWriteBytes(a, writtenData, sizeof(writtenData)), accessorOk);
    CHECK_EQ(accessorClose(&a), accessorOk);
    CHECK_EQ(a, ACCESSOR_INIT);

    CHECK_EQ(accessorOpenReadingFile(&a, fullDirPath, filename, accessorPathOptionNone, sizeof(uint32_t), ACCESSOR_UNTIL_END), accessorOk);
    count = accessorLookAheadAvailableBytes(a, &ptr);
    CHECK_EQ(count, sizeof(writtenData) - sizeof(uint32_t));
    CHECK_EQ(memcmp(ptr, writtenData + 1, count), 0);
    CHECK_EQ(accessorClose(&a), accessorOk);

    CHECK_EQ(accessorBuildPath(&fullFilePath, fullDirPath, filename, accessorPathOptionNone | accessorPathOptionConvertBackslash, 0), accessorOk);

    CHECK_EQ(accessorOpenWritingMemory(&a, 0, 0), accessorOk);
    CHECK_EQ(accessorWriteBytes(a, writtenData, sizeof(writtenData)), accessorOk);
    CHECK_EQ(accessorWriteToFile(a, fullDirPath, filename, accessorPathOptionNone | accessorPathOptionConvertBackslash, 0666, 0, ACCESSOR_UNTIL_END), accessorOk);

    count = accessorSize(a);
    CHECK_EQ(count, sizeof(writtenData));
    CHECK_EQ(accessorSeek(a, sizeof(writtenData) / 2, SEEK_SET), accessorOk);
    count = accessorCursor(a);
    CHECK_EQ(count, sizeof(writtenData) / 2);
    CHECK_EQ(accessorTruncate(a), accessorOk);
    count = accessorSize(a);
    CHECK_EQ(count, sizeof(writtenData) / 2);
    count = accessorAvailableBytesCount(a);
    CHECK_EQ(count, 0);

    CHECK_EQ(accessorPushCursor(a), accessorOk);
    CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);
    count = accessorCursor(a);
    CHECK_EQ(count, 0);
    CHECK_EQ(accessorSeek(a, 1, SEEK_CUR), accessorOk);
    count = accessorCursor(a);
    CHECK_EQ(count, 1);
    CHECK_EQ(accessorPushCursor(a), accessorOk);
    count = accessorCursor(a);
    CHECK_EQ(accessorSeek(a, 1, SEEK_CUR), accessorOk);
    count = accessorCursor(a);
    CHECK_EQ(count, 2);
    CHECK_EQ(accessorPopCursor(a), accessorOk);
    count = accessorCursor(a);
    CHECK_EQ(count, 1);
    CHECK_EQ(accessorPopCursor(a), accessorOk);
    count = accessorCursor(a);
    CHECK_EQ(count, sizeof(writtenData) / 2);
    CHECK_EQ(accessorPushCursor(a), accessorOk);
    CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);
    CHECK_EQ(accessorPushCursor(a), accessorOk);
    CHECK_EQ(accessorPushCursor(a), accessorOk);
    CHECK_EQ(accessorPopCursors(a, 3), accessorOk);
    count = accessorCursor(a);
    CHECK_EQ(count, sizeof(writtenData) / 2);
    CHECK_EQ(accessorPopCursor(a), accessorInvalidParameter);

    CHECK_EQ(accessorClose(&a), accessorOk);

    CHECK_EQ(accessorOpenReadingFile(&a, fullDirPath, filename, accessorPathOptionNone, 0, ACCESSOR_UNTIL_END), accessorOk);
    count = accessorLookAheadAvailableBytes(a, &ptr);
    CHECK_EQ(count, sizeof(writtenData));
    CHECK_EQ(memcmp(ptr, writtenData, count), 0);
    CHECK_EQ(accessorClose(&a), accessorOk);

    CHECK_EQ(unlink(fullFilePath), 0);
    free(fullFilePath);

    CHECK_EQ(rmdir(fullDirPath), 0);
    free(fullDirPath);

    CHECK_EQ(accessorBuildPath(&fullDirPath, NULL, dirPath, accessorPathOptionNone | accessorPathOptionConvertBackslash, 0), accessorOk);
    CHECK_EQ(rmdir(fullDirPath), 0);

    CHECK_EQ(accessorCreateDirectory(NULL, fullDirPath, accessorPathOptionNone), accessorOk);
    CHECK_EQ(rmdir(fullDirPath), 0);
    free(fullDirPath);

    CHECK_EQ(accessorOpenReadingMemory(&a, writtenData, sizeof(writtenData), accessorDontFreeOnClose, sizeof(uint32_t), sizeof(writtenData) - 2 * sizeof(uint32_t)), accessorOk);
    CHECK_EQ(accessorReadBytes(a, readData, sizeof(writtenData) - 2 * sizeof(uint32_t)), accessorOk);
    CHECK_EQ(memcmp(readData, writtenData + 1, sizeof(writtenData) - 2 * sizeof(uint32_t)), 0);

    CHECK_EQ(accessorReadBytes(a, readData, 1), accessorBeyondEnd);

    CHECK_EQ(accessorSeek(a, 0, SEEK_END), accessorOk);
    CHECK_EQ(accessorReadBytes(a, readData, 1), accessorBeyondEnd);

    CHECK_EQ(accessorSeek(a, -1, SEEK_END), accessorOk);
    CHECK_EQ(accessorSeek(a, 0, SEEK_CUR), accessorOk);
    CHECK_EQ(accessorReadBytes(a, readData, 1), accessorOk);
    CHECK_EQ(accessorReadBytes(a, readData, 1), accessorBeyondEnd);

    CHECK_EQ(accessorSeek(a, 0, SEEK_SET), accessorOk);

    CHECK_EQ(accessorOpenReadingAccessorBytes(&b, a, sizeof(writtenData) / 2), accessorOk);
    CHECK_EQ(accessorGetPointerForBytesToRead(b, &ptr, sizeof(writtenData) / 2), accessorOk);
    CHECK_EQ(memcmp(ptr, writtenData + 1, sizeof(writtenData) / 2), 0);
    CHECK_EQ(accessorAvailableBytesCount(a), sizeof(writtenData) / 2 - 2 * sizeof(uint32_t));
    CHECK_EQ(accessorAvailableBytesCount(b), 0);

    CHECK_EQ(accessorClose(&b), accessorOk);

    CHECK_EQ(accessorOpenReadingAccessorWindow(&b, a, sizeof(uint32_t), sizeof(writtenData) / 2), accessorOk);
    CHECK_EQ(accessorGetPointerForBytesToRead(b, &ptr, sizeof(writtenData) / 2), accessorOk);
    CHECK_EQ(memcmp(ptr, writtenData + 2, sizeof(writtenData) / 2), 0);
    CHECK_EQ(accessorAvailableBytesCount(a), sizeof(writtenData) / 2 - 2 * sizeof(uint32_t));
    CHECK_EQ(accessorAvailableBytesCount(b), 0);

    CHECK_EQ(accessorClose(&b), accessorOk);

    CHECK_EQ(accessorClose(&a), accessorOk);
}



void testEndianness(void)
{
    switch(accessorGetNativeEndianness())
    {
    case accessorBig:
        printf("native endianness is: accessorBig\n");
        break;
    case accessorLittle:
        printf("native endianness is: accessorLittle\n");
        break;
    case accessorNative:
        printf("** OUCH **: native endianness is: accessorNative, should be accessorBig or accessorLittle\n");
        break;
    case accessorReverse:
        printf("** OUCH **: native endianness is: accessorReverse, should be accessorBig or accessorLittle\n");
        break;
    default:
        printf("** OUCH **: native endianness is unknown\n");
        break;
    }

    CHECK_EQ(accessorOppositeEndianness(accessorBig), accessorLittle);
    CHECK_EQ(accessorOppositeEndianness(accessorLittle), accessorBig);
    CHECK_EQ(accessorOppositeEndianness(accessorNative), accessorReverse);
    CHECK_EQ(accessorOppositeEndianness(accessorReverse), accessorNative);

    for (int e = 0; e < ACCESSOR_ENDIANNESS_COUNT; e++)
    {
        CHECK_EQ(accessorSetDefaultEndianness(endianness[e]), accessorOk);
        CHECK_EQ(accessorDefaultEndianness(), endianness[e]);
    }

    CHECK_EQ(accessorBigOrLittleEndianness(accessorLittle), accessorLittle);
    CHECK_EQ(accessorBigOrLittleEndianness(accessorBig), accessorBig);
    CHECK_EQ(accessorBigOrLittleEndianness(accessorNative), accessorGetNativeEndianness());
    CHECK_EQ(accessorBigOrLittleEndianness(accessorReverse), accessorOppositeEndianness(accessorGetNativeEndianness()));

    CHECK_EQ(accessorNativeOrReverseEndianness(accessorLittle), accessorGetNativeEndianness() == accessorBig ? accessorReverse : accessorNative);
    CHECK_EQ(accessorNativeOrReverseEndianness(accessorBig), accessorGetNativeEndianness() == accessorBig ? accessorNative : accessorReverse);
    CHECK_EQ(accessorNativeOrReverseEndianness(accessorNative), accessorNative);
    CHECK_EQ(accessorNativeOrReverseEndianness(accessorReverse), accessorReverse);

    CHECK_EQ(accessorSetDefaultEndianness(accessorNative), accessorOk);
}


