// ************
// * accessor *
// ************
//
// With accessor, you can get read and write access to files or memory without worrying about data size, endianness and details.
// accessor is developed with reverse engineering in mind, so, while care was taken to performance, it is not its main goal.
// accessor isn't thread-safe, if needed, you have to serialize accessor function calls.
//
// **********
// * how to *
// **********
// To use accessor, you only need accessor.h and link with accessor.c or a library containing it
//
// accessor implements accessor_t, an opaque variable type hiding internal details that are subject to change and shouldn't be trusted.
// accessor notions include:
// - read or write: read accessors only allow read operations, write accessors allow read and write operations. write operations on read accessors return accessorInvalidParameter
// - endianness: big or little are as usual. native and reverse are also defined and depend on the architecture of the running program.
// - cursor: read and write operations occur at the cursor which then advance from the transfer's byte count.
// - window: defined by an offset and a size, a window delimits the accessible part of some (possibly larger) data chunk.
// - coverage: read accessors may be instructed to record which part of the data was read. Coverage can then be consolidated, e.g. to find which part of data isn't explored yet.
// - sub-accessors: a readonly accessor (the sub-accessor) may be created to access part of another readonly accessor (the super-accessor). This is done without duplicating data. write accessors can't be sub-accessed
// - both a "basePath" and a "path" are used to specify a file path:
//   - if "path" is an absolute path, "basePath" is ignored
//   - if "path" is a relative path and "basePath" refers to a non existing object, then "basePath" is considered as a directory path and "path" is the relative to the "basePath" directory
//   - if "path" is a relative path and "basePath" refers to an existing directory, then "path" is the relative to the "basePath" directory
//   - if "path" is a relative path and "basePath" refers to an existing non-directory object, then "path" is the relative to the directory containing the "basePath" object
//   - "~" and "~user" constructs are not honored


#ifndef accessor_h
#define accessor_h



#ifdef __cplusplus
extern "C" {
#endif



#define ACCESSOR_BUILD_NUMBER   103
// Version history:
//
//  Build   Date            Comment
//  103     05-NOV-2022     optimized accessorSwap[U]Int for common number width
//  102     03-NOV-2022     stop using mktemp. when reading or mapping a file, only the window (possibly rounded to page boundary) is read or mapped
//  101     14-OCT-2022     small files are always read in memory, not mapped
//  100     05-OCT-2022     a completely rewritten successor to original accessor toolkit build 25, faster and safer



#define KB  1024
#define MB  (KB*KB)
#define GB  (KB*KB*KB)
#define TB  (KB*KB*KB*KB)



// if ACCESSOR_USE_MMAP is true, accessor will try mapping data in memory instead of reading it.
#if TARGET_MSYS
#define ACCESSOR_USE_MMAP               0
#else
#define ACCESSOR_USE_MMAP               1
#endif

// read accessing a file's window smaller than ACCESSOR_MMAP_MIN_FILESIZE will avoid mapping it.
#ifndef ACCESSOR_MMAP_MIN_FILESIZE
#define ACCESSOR_MMAP_MIN_FILESIZE      (16 * KB)
#endif



#include <stdint.h>
#include <fcntl.h>          // for mode_t
#include <stdio.h>          // for SEEK_SET etc., perror
#include <limits.h>         // for INT32_MAX
#include <stddef.h>         // for nullptr


// accessor_t is an opaque structure
// accessor variables are of type "accessor_t *" and their initial value has to be set to ACCESSOR_INIT
typedef struct _accessor_t accessor_t;



// accessor_t variables MUST be initialized to ACCESSOR_INIT, else accessorOpen... functions will fail
#ifdef __cplusplus
#define ACCESSOR_INIT       nullptr
#else
#define ACCESSOR_INIT       NULL
#endif



// windowSize and countLimit special value meaning "up to end of data"
#define ACCESSOR_UNTIL_END  SIZE_MAX


// endianness passed as parameter must be one of accessorBig, Little, Native or Reverse
typedef enum
{
    accessorBig                         = 0,        // well known
    accessorLittle,                                 // well known
    accessorNative,                                 // the native byte order of running program, be it big or little
    accessorReverse,                                // the opposite of accessorNative
#define ACCESSOR_ENDIANNESS_COUNT       4
} accessorEndianness;



// various status used to indicate most probable cause of failure
typedef enum
{
    accessorOk                          = 0,
    accessorInvalidParameter,                       // invalid parameter or invalid request
    accessorBeyondEnd,                              // accessing data beyond end of allowed window or beyond some other size limit
    accessorOutOfMemory,                            // memory allocation failed
    accessorHostError,                              // operating system returned some failure status
    accessorOpenError,                              // can't open a file (read or write)
    accessorInvalidReadData,                        // attempt to read invalid data
    accessorWriteError,                             // error writing a file
    accessorReadOnlyError,                          // write operation attempted on readonly accessor
} accessorStatus;



// accessorPathOptions may be ORed
enum
{
    accessorPathOptionNone              = 0x00,
    accessorPathOptionCreateDirectory   = 0x01,     // directory containing object referred by path will be created if needed. created directory are all created using mode 0777 (ugo=rwx)
    accessorPathOptionCreatePath        = 0x02,     // directory and intermediate directories containing object referred by path will be created if needed, using mode 0777 (ugo=rwx). implies accessorPathOptionCreateDirectory
    accessorPathOptionConvertBackslash  = 0x04,     // backslash ("\") will be converted to slash ("/"). Useful for some ugly foreign path syntax.
    accessorPathOptionPathIsRelative    = 0x08,     // if path is an absolute path, it will be converted to a relative one by removing leading "\" or "/"
    accessorPathOptionIs32Bits          = INT32_MAX // don't use, this is to force enum to 32 bits integers
};
typedef uint32_t accessorPathOptions;



// non-ORable
typedef enum
{
    accessorDontFreeOnClose             = 0,        // don't free() memory on accessorClose
    accessorFreeOnClose                 = 1,        // free() memory on accessorClose
} accessorFreeOnCloseOption;



// non-ORable
typedef enum
{
    accessorDisableCoverage             = 0,        // self explanatory
    accessorEnableCoverage              = 1,        // self explanatory
} accessorCoverageOption;



// non-ORable
typedef enum
{
    accessorCoverageOnlyIfEnabled       = 0,        // accessorAddCoverageRecord will add coverage record only if accessor coverage is enabled and not supended
    accessorCoverageEvenIfDisabled      = 1,        // accessorAddCoverageRecord will add coverage record even if accessor coverage is disabled except if coverage is supended
} accessorCoverageForceOption;



// only read operations may generate coverage record, write operations don't
typedef struct
{
    size_t offset;                                  // default sort keys order is increasing offset, decreasing size, increasing usage1, increasing usage2
    size_t size;
    uintmax_t usage1;                               // free use
    const void * usage2;                            // free use: no accessor routine will access *usage2
} accessorCoverageRecord;



// coverage merge function return type
typedef enum
{
    accessorDidntMerge                   = 0,       // self explanatory
    accessorDidMerge,                               // self explanatory
} accessorMergeResult;


// accessor open and close

// read accessors

// all read accessors are created with coverage disabled, once open, accessor's coverage can be enabled with accessorAllowCoverage()
// windowOffset and windowSize define the window, i.e. the part of the underlying (memory or file) data accessible by the accessor
// e.g. cursor == 0 will access data at start of window, cursor == windowSize - 1 will point at last window byte
// sub accessor's windows are window on super accessor's window, not on super accessor's complete data

// ptr              memory chunk address
// dataSize         memory chunk size
// freeOption       memory chunk can be freed by accessorClose
// windowOffset     offset of accessible data window
// windowSize       size of accessible data window
// if the data as limited by the window offset and size is modified between calls to accessor functions, behavior is undefined
// initial endianness is accessorDefaultEndianness()
accessorStatus accessorOpenReadingMemory(accessor_t ** a, const void * ptr, size_t dataSize, accessorFreeOnCloseOption freeOption, size_t windowOffset, size_t windowSize);

// windowSize == ACCESSOR_UNTIL_END means "up to end of file", other windowSize values are taken literally
// options accessorPathOptionCreateDirectory and accessorPathOptionCreatePath are ignored
// if the file data as limited by the window offset and size is modified between calls to accessor functions, behavior is undefined
// initial endianness is accessorDefaultEndianness()
accessorStatus accessorOpenReadingFile(accessor_t ** a, const char * basePath, const char * path, accessorPathOptions pathOptions, size_t windowOffset, size_t windowSize);

// create a readonly sub accessor whose data is read from a readonly super accessor's own window.
// count == ACCESSOR_UNTIL_END means up to end of super accessor's data, other values are taken literally
// coverage for a sub accessor future operations is handled by sub accessor only, super accessor's coverage is not affected by operations on sub accessor.
// a single coverage record is added for super accessor, if enabled and not suspended
// sub accessor inherits super accessor's endianness
accessorStatus accessorOpenReadingAccessorBytes(accessor_t ** a, accessor_t  *supera, size_t count);

// create a readonly sub accessor accessing a sub-window of a readonly super accessor's own window.
// current cursor of supera is irrelevant
// windowSize == ACCESSOR_UNTIL_END means up to end of super accessor's data, other values are taken literally
// coverage for a sub accessor future operations is handled by sub accessor only, super accessor's coverage is not affected by operations on sub accessor.
// no coverage record is added for super accessor
// sub accessor inherits super accessor's endianness
// internal super accessor reference count is incremented but is otherwise unmodified
accessorStatus accessorOpenReadingAccessorWindow(accessor_t ** a, accessor_t * supera, size_t windowOffset, size_t windowSize);

// write accessors

// all write accessors are created with read coverage disabled, once open, accessor's coverage can be enabled with accessorAllowCoverage()
// only read operations will generate coverage record. write operations don't
// initialAllocation and granularity are hints (that may or may not be honored) for memory allocation chunk size and may be set to 0 for default (unspecified) values

// create an empty read/write accessor, writing data to some internal memory buffer
// accessor's data will be dismissed on accessorClose(), see accessorWriteToFile() to save data before accessorClose()
// initial endianness is accessorDefaultEndianness()
accessorStatus accessorOpenWritingMemory(accessor_t ** a, size_t initialAllocation, size_t granularity);

// create an empty read/write accessor, writing data to some internal memory buffer
// accessor's data is written to file on accessorClose()
// file is created immediately and truncated if needed
// mode             file creation mode
// initial endianness is accessorDefaultEndianness()
accessorStatus accessorOpenWritingFile(accessor_t ** a, const char * basePath, const char * path, accessorPathOptions pathOptions, mode_t mode, size_t initialAllocation, size_t granularity);

// write (part of) an accessor window's data to a file
// especially useful when output filename is known only after accessorOpenWritingMemory() has been called
// windowOffset and windowSize delimit a window on accessor's own window for the data to be written to file
// windowSize == ACCESSOR_UNTIL_END means up to end of accessor's own window, other windowSize values are taken literally
accessorStatus accessorWriteToFile(const accessor_t * a, const char * basePath, const char * path, accessorPathOptions pathOptions, mode_t mode, size_t windowOffset, size_t windowSize);

// accessor is closed. if a is super accessor to another accessor, it close actions may be delayed until all sub accessors are closed
// "a" will be set to ACCESSOR_INIT on success whether it is a super accessor or not
accessorStatus accessorClose(accessor_t ** a);




// cursor and size related

// get accessor window's offset in the root accessor's data
// the root accessor is the top of super accessors chain or itself if it has no superaccessor
// returned value include root accessor's own window offset into data, if applicable
size_t accessorRootWindowOffset(const accessor_t * a);

// get current accessor window size
// for write accessor, the window size may have been extended to contain the last written byte
size_t accessorSize(const accessor_t * a);

// for write accessors only: truncate at current position, data following cursor is dismissed
accessorStatus accessorTruncate(accessor_t * a);

// change cursor position
// write accessor cursor can be moved beyong its window, in which case the window size is extended and added bytes are set to 0x00
// whence                 is one of SEEK_SET, SEEK_CUR or SEEK_END, similar to lseek(2)
accessorStatus accessorSeek(accessor_t * a, ssize_t offset, int whence);

// get how much bytes are available from cursor until accessor's end of window
size_t accessorAvailableBytesCount(const accessor_t * a);

// get current cursor position, in the [0...windowSize] range, windowSize meaning it is just after the last byte, with no more available bytes
size_t accessorCursor(const accessor_t * a);

// save current cursor on the cursor stack
accessorStatus accessorPushCursor(accessor_t * a);

// restore last pushed cursor, removing it from the cursor stack
accessorStatus accessorPopCursor(accessor_t * a);

// similar to repeating accessorDropCursor() n-1 times followed by a single accessorPopCursor()
accessorStatus accessorPopCursors(accessor_t * a, size_t n);

// remove last pushed cursor from the cursor stack, cursor is not modified
accessorStatus accessorDropCursor(accessor_t * a);

// similar to repeating accessorDropCursor() n times
accessorStatus accessorDropCursors(accessor_t * a, size_t n);



// andianness related

// get native endianness as either endianness_big or accessorLittle
// for programs running in emulation mode (e.g. Rosetta) this is the native endianness of the emulated code
accessorEndianness accessorGetNativeEndianness(void);

// returns accessorBig or accessorLittle corresponding to given endianness
accessorEndianness accessorBigOrLittleEndianness(accessorEndianness e);
// returns accessorNative or accessorReverse corresponding to given endianness
accessorEndianness accessorNativeOrReverseEndianness(accessorEndianness e);

// big <-> little and native <-> reverse conversion
accessorEndianness accessorOppositeEndianness(accessorEndianness e);

// get or set default endianness for future accessor creations
// initially, default endianness is native
// default endianness is globally shared among all threads
accessorEndianness accessorDefaultEndianness(void);                     // for future accessors
accessorStatus accessorSetDefaultEndianness(accessorEndianness e);      // for future accessors

// get or set default endianness for an accessor
accessorEndianness accessorCurrentEndianness(const accessor_t * a);
accessorStatus accessorSetCurrentEndianness(accessor_t * a, accessorEndianness e);



// number read

// read operations move cursor and add a coverage record if coverage is enabled and not suspended
accessorStatus accessorReadEndianUInt(accessor_t * a, uintmax_t * x, accessorEndianness e, size_t nbytes);                          // read a nbytes wide unsigned integer at cursor
accessorStatus accessorReadEndianUInt16(accessor_t * a, uint16_t * x, accessorEndianness e);                                        // read a 2 bytes unsigned integer at cursor
accessorStatus accessorReadEndianUInt24(accessor_t * a, uint32_t * x, accessorEndianness e);                                        // read a 3 bytes unsigned integer at cursor
accessorStatus accessorReadEndianUInt32(accessor_t * a, uint32_t * x, accessorEndianness e);                                        // read a 4 bytes unsigned integer at cursor
accessorStatus accessorReadEndianUInt64(accessor_t * a, uint64_t * x, accessorEndianness e);                                        // read a 8 bytes unsigned integer at cursor

accessorStatus accessorReadEndianInt(accessor_t * a, intmax_t * x, accessorEndianness e, size_t nbytes);                            // read a nbytes wide integer at cursor
accessorStatus accessorReadEndianInt16(accessor_t * a, int16_t * x, accessorEndianness e);                                          // read a 2 bytes integer at cursor
accessorStatus accessorReadEndianInt24(accessor_t * a, int32_t * x, accessorEndianness e);                                          // read a 3 bytes integer at cursor
accessorStatus accessorReadEndianInt32(accessor_t * a, int32_t * x, accessorEndianness e);                                          // read a 4 bytes integer at cursor
accessorStatus accessorReadEndianInt64(accessor_t * a, int64_t * x, accessorEndianness e);                                          // read a 8 bytes integer at cursor

accessorStatus accessorReadEndianFloat32(accessor_t * a, float * x, accessorEndianness e);                                          // read a float at cursor
accessorStatus accessorReadEndianFloat64(accessor_t * a, double * x, accessorEndianness e);                                         // read a double at cursor

// using accessor's endianness
accessorStatus accessorReadUInt(accessor_t * a, uintmax_t * x, size_t nbytes);                                                      // read a nbytes wide unsigned integer at cursor using accessor's default endianness
accessorStatus accessorReadUInt8(accessor_t * a, uint8_t * x);                                                                      // read a 1 byte unsigned integer at cursor using accessor's default endianness
accessorStatus accessorReadUInt16(accessor_t * a, uint16_t * x);                                                                    // read a 2 bytes unsigned integer at cursor using accessor's default endianness
accessorStatus accessorReadUInt24(accessor_t * a, uint32_t * x);                                                                    // read a 3 bytes unsigned integer at cursor using accessor's default endianness
accessorStatus accessorReadUInt32(accessor_t * a, uint32_t * x);                                                                    // read a 4 bytes unsigned integer at cursor using accessor's default endianness
accessorStatus accessorReadUInt64(accessor_t * a, uint64_t * x);                                                                    // read a 8 bytes unsigned integer at cursor using accessor's default endianness

accessorStatus accessorReadInt(accessor_t * a, intmax_t * x, size_t nbytes);                                                        // read a nbytes wide integer at cursor using accessor's default endianness
accessorStatus accessorReadInt8(accessor_t * a, int8_t * x);                                                                        // read a 1 byte integer at cursor using accessor's default endianness
accessorStatus accessorReadInt16(accessor_t * a, int16_t * x);                                                                      // read a 2 bytes integer at cursor using accessor's default endianness
accessorStatus accessorReadInt24(accessor_t * a, int32_t * x);                                                                      // read a 3 bytes integer at cursor using accessor's default endianness
accessorStatus accessorReadInt32(accessor_t * a, int32_t * x);                                                                      // read a 4 bytes integer at cursor using accessor's default endianness
accessorStatus accessorReadInt64(accessor_t * a, int64_t * x);                                                                      // read a 8 bytes integer at cursor using accessor's default endianness

accessorStatus accessorReadFloat32(accessor_t * a, float * x);                                                                      // read a float at cursor using accessor's default endianness
accessorStatus accessorReadFloat64(accessor_t * a, double * x);                                                                     // read a double at cursor using accessor's default endianness

// Varint and zigzag numbers are as found in protobuf (protocol buffers)
accessorStatus accessorReadVarInt(accessor_t * a, uintmax_t * x);                                                                   // read an unsigned base 128 varint at cursor. as varints have no upper limit, an error is returned if x overflows uintmax_t
accessorStatus accessorReadZigZagInt(accessor_t * a, intmax_t * x);                                                                 // read a signed base 128 zigzag integer at cursor. as zigzag ints have no lower/upper limit, an error is returned if x overflows intmax_t



// number write

accessorStatus accessorWriteEndianUInt(accessor_t * a, uintmax_t x, accessorEndianness e, size_t nbytes);                           // write a nbytes wide unsigned integer at cursor
accessorStatus accessorWriteEndianUInt16(accessor_t * a, uint16_t x, accessorEndianness e);                                         // write a 2 bytes unsigned integer at cursor
accessorStatus accessorWriteEndianUInt24(accessor_t * a, uint32_t x, accessorEndianness e);                                         // write a 3 bytes unsigned integer at cursor
accessorStatus accessorWriteEndianUInt32(accessor_t * a, uint32_t x, accessorEndianness e);                                         // write a 4 bytes unsigned integer at cursor
accessorStatus accessorWriteEndianUInt64(accessor_t * a, uint64_t x, accessorEndianness e);                                         // write a 8 bytes unsigned integer at cursor

accessorStatus accessorWriteEndianInt(accessor_t * a, intmax_t x, accessorEndianness e, size_t nbytes);                             // write a nbytes wide integer at cursor
accessorStatus accessorWriteEndianInt16(accessor_t * a, int16_t x, accessorEndianness e);                                           // write a 2 bytes integer at cursor
accessorStatus accessorWriteEndianInt24(accessor_t * a, int32_t x, accessorEndianness e);                                           // write a 3 bytes integer at cursor
accessorStatus accessorWriteEndianInt32(accessor_t * a, int32_t x, accessorEndianness e);                                           // write a 4 bytes integer at cursor
accessorStatus accessorWriteEndianInt64(accessor_t * a, int64_t x, accessorEndianness e);                                           // write a 8 bytes integer at cursor

accessorStatus accessorWriteEndianFloat32(accessor_t * a, float x, accessorEndianness e);                                           // write a 4 bytes float at cursor
accessorStatus accessorWriteEndianFloat64(accessor_t * a, double x, accessorEndianness e);                                          // write a 8 bytes float at cursor

// using accessor's endianness
accessorStatus accessorWriteUInt(accessor_t * a, uintmax_t x, size_t nbytes);                                                       // write a nbytes wide unsigned integer at cursor using accessor's default endianness
accessorStatus accessorWriteUInt8(accessor_t * a, uint8_t x);                                                                       // write a 1 byte unsigned integer at cursor using accessor's default endianness
accessorStatus accessorWriteUInt16(accessor_t * a, uint16_t x);                                                                     // write a 2 bytes unsigned integer at cursor using accessor's default endianness
accessorStatus accessorWriteUInt24(accessor_t * a, uint32_t x);                                                                     // write a 3 bytes unsigned integer at cursor using accessor's default endianness
accessorStatus accessorWriteUInt32(accessor_t * a, uint32_t x);                                                                     // write a 4 bytes unsigned integer at cursor using accessor's default endianness
accessorStatus accessorWriteUInt64(accessor_t * a, uint64_t x);                                                                     // write a 8 bytes unsigned integer at cursor using accessor's default endianness

accessorStatus accessorWriteInt(accessor_t * a, intmax_t x, size_t nbytes);                                                         // write a nbytes wide integer at cursor using accessor's default endianness
accessorStatus accessorWriteInt8(accessor_t * a, int8_t x);                                                                         // write a 1 byte integer at cursor using accessor's default endianness
accessorStatus accessorWriteInt16(accessor_t * a, int16_t x);                                                                       // write a 2 bytes integer at cursor using accessor's default endianness
accessorStatus accessorWriteInt24(accessor_t * a, int32_t x);                                                                       // write a 3 bytes integer at cursor using accessor's default endianness
accessorStatus accessorWriteInt32(accessor_t * a, int32_t x);                                                                       // write a 4 bytes integer at cursor using accessor's default endianness
accessorStatus accessorWriteInt64(accessor_t * a, int64_t x);                                                                       // write a 8 bytes integer at cursor using accessor's default endianness

accessorStatus accessorWriteFloat32(accessor_t * a, float x);                                                                       // write a 4 bytes float at cursor using accessor's default endianness
accessorStatus accessorWriteFloat64(accessor_t * a, double x);                                                                      // write a 8 bytes float at cursor using accessor's default endianness

accessorStatus accessorWriteVarInt(accessor_t * a, uintmax_t x);                                                                    // write an unsigned base 128 varint at cursor
accessorStatus accessorWriteZigZagInt(accessor_t * a, intmax_t x);                                                                  // write a signed zigzag base 128 varint integer at cursor



// integer arrays read

// read array is allocated, it is up to caller to free() it
// accessorRead[U]Int8Array functionality is available using accessorReadAllocatedBytes
accessorStatus accessorReadEndianUInt16Array(accessor_t * a, uint16_t ** array, size_t count, accessorEndianness e);                // read an array of 2 bytes unsigned integers at cursor
accessorStatus accessorReadEndianUInt24Array(accessor_t * a, uint32_t ** array, size_t count, accessorEndianness e);                // read an array of 3 bytes unsigned integers at cursor
accessorStatus accessorReadEndianUInt32Array(accessor_t * a, uint32_t ** array, size_t count, accessorEndianness e);                // read an array of 4 bytes unsigned integers at cursor
accessorStatus accessorReadEndianUInt64Array(accessor_t * a, uint64_t ** array, size_t count, accessorEndianness e);                // read an array of 8 bytes unsigned integers at cursor

accessorStatus accessorReadEndianInt16Array(accessor_t * a, int16_t ** array, size_t count, accessorEndianness e);                  // read an array of 2 bytes unsigned integers at cursor
accessorStatus accessorReadEndianInt24Array(accessor_t * a, int32_t ** array, size_t count, accessorEndianness e);                  // read an array of 3 bytes unsigned integers at cursor
accessorStatus accessorReadEndianInt32Array(accessor_t * a, int32_t ** array, size_t count, accessorEndianness e);                  // read an array of 4 bytes unsigned integers at cursor
accessorStatus accessorReadEndianInt64Array(accessor_t * a, int64_t ** array, size_t count, accessorEndianness e);                  // read an array of 8 bytes unsigned integers at cursor

accessorStatus accessorReadEndianFloat32Array(accessor_t * a, float ** array, size_t count, accessorEndianness e);                  // read an array of 4 bytes floats at cursor
accessorStatus accessorReadEndianFloat64Array(accessor_t * a, double ** array, size_t count, accessorEndianness e);                 // read an array of 8 bytes floats at cursor

accessorStatus accessorReadUInt16Array(accessor_t * a, uint16_t ** array, size_t count);                                            // read an array of 2 bytes integers at cursor
accessorStatus accessorReadUInt24Array(accessor_t * a, uint32_t ** array, size_t count);                                            // read an array of 3 bytes integers at cursor
accessorStatus accessorReadUInt32Array(accessor_t * a, uint32_t ** array, size_t count);                                            // read an array of 4 bytes integers at cursor
accessorStatus accessorReadUInt64Array(accessor_t * a, uint64_t ** array, size_t count);                                            // read an array of 8 bytes integers at cursor

accessorStatus accessorReadInt16Array(accessor_t * a, int16_t ** array, size_t count);                                              // read an array of 2 bytes integers at cursor
accessorStatus accessorReadInt24Array(accessor_t * a, int32_t ** array, size_t count);                                              // read an array of 3 bytes integers at cursor
accessorStatus accessorReadInt32Array(accessor_t * a, int32_t ** array, size_t count);                                              // read an array of 4 bytes integers at cursor
accessorStatus accessorReadInt64Array(accessor_t * a, int64_t ** array, size_t count);                                              // read an array of 8 bytes integers at cursor

accessorStatus accessorReadFloat32Array(accessor_t * a, float ** array, size_t count);                                              // read an array of 4 bytes floats at cursor
accessorStatus accessorReadFloat64Array(accessor_t * a, double ** array, size_t count);                                             // read an array of 8 bytes floats at cursor



// integer arrays write

accessorStatus accessorWriteEndianUInt16Array(accessor_t * a, const uint16_t * array, size_t count, accessorEndianness e);          // write an array of 2 bytes unsigneds integer at cursor
accessorStatus accessorWriteEndianUInt24Array(accessor_t * a, const uint32_t * array, size_t count, accessorEndianness e);          // write an array of 3 bytes unsigneds integer at cursor
accessorStatus accessorWriteEndianUInt32Array(accessor_t * a, const uint32_t * array, size_t count, accessorEndianness e);          // write an array of 4 bytes unsigneds integer at cursor
accessorStatus accessorWriteEndianUInt64Array(accessor_t * a, const uint64_t * array, size_t count, accessorEndianness e);          // write an array of 8 bytes unsigneds integer at cursor

accessorStatus accessorWriteEndianInt16Array(accessor_t * a, const int16_t * array, size_t count, accessorEndianness e);            // write an array of 2 bytes integers at cursor
accessorStatus accessorWriteEndianInt24Array(accessor_t * a, const int32_t * array, size_t count, accessorEndianness e);            // write an array of 3 bytes integers at cursor
accessorStatus accessorWriteEndianInt32Array(accessor_t * a, const int32_t * array, size_t count, accessorEndianness e);            // write an array of 4 bytes integers at cursor
accessorStatus accessorWriteEndianInt64Array(accessor_t * a, const int64_t * array, size_t count, accessorEndianness e);            // write an array of 8 bytes integers at cursor

accessorStatus accessorWriteEndianFloat32Array(accessor_t * a, float * array, size_t count, accessorEndianness e);                  // write an array of 4 bytes floats at cursor
accessorStatus accessorWriteEndianFloat64Array(accessor_t * a, double * array, size_t count, accessorEndianness e);                 // write an array of 8 bytes floats at cursor

accessorStatus accessorWriteUInt16Array(accessor_t * a, const uint16_t * array, size_t count);                                      // write an array of 2 bytes unsigned integer at cursor
accessorStatus accessorWriteUInt24Array(accessor_t * a, const uint32_t * array, size_t count);                                      // write an array of 3 bytes unsigned integer at cursor
accessorStatus accessorWriteUInt32Array(accessor_t * a, const uint32_t * array, size_t count);                                      // write an array of 4 bytes unsigned integer at cursor
accessorStatus accessorWriteUInt64Array(accessor_t * a, const uint64_t * array, size_t count);                                      // write an array of 8 bytes unsigned integer at cursor

accessorStatus accessorWriteInt16Array(accessor_t * a, const int16_t * array, size_t count);                                        // write an array of 2 bytes integer at cursor
accessorStatus accessorWriteInt24Array(accessor_t * a, const int32_t * array, size_t count);                                        // write an array of 3 bytes integer at cursor
accessorStatus accessorWriteInt32Array(accessor_t * a, const int32_t * array, size_t count);                                        // write an array of 4 bytes integer at cursor
accessorStatus accessorWriteInt64Array(accessor_t * a, const int64_t * array, size_t count);                                        // write an array of 8 bytes integer at cursor

accessorStatus accessorWriteFloat32Array(accessor_t * a, float * array, size_t count);                                              // write an array of 4 bytes floats at cursor
accessorStatus accessorWriteFloat64Array(accessor_t * a, double * array, size_t count);                                             // write an array of 8 bytes floats at cursor



// block read

// reading from an accessor involves a memory transfer from accessor's data to destination
// see accessorGetPointerForBytesToRead() if you prefer reading accessor's internal data
accessorStatus accessorReadEndianBytes(accessor_t * a, void * ptr, size_t count, accessorEndianness e);                             // read a chunk of bytes, maybe in reverse order. ptr must be able to hold count bytes
accessorStatus accessorReadBytes(accessor_t * a, void * ptr, size_t count);                                                         // read a chunk of bytes, ptr must be able to hold size bytes
accessorStatus accessorReadAllocatedEndianBytes(accessor_t * a, void ** ptr, size_t count, accessorEndianness e);                   // read a chunk of bytes, maybe in reverse order, *ptr is allocated, it is up to caller to free() it
accessorStatus accessorReadAllocatedBytes(accessor_t * a, void ** ptr, size_t count);                                               // read a chunk of bytes, *ptr is allocated, it is up to caller to free() it



// block write

// writing to an accessor involves a memory transfer from source to accessor's data
// see accessorGetPointerForBytesToWrite() if you prefer writing accessor's internal data
accessorStatus accessorWriteEndianBytes(accessor_t * a, const void * ptr, size_t count, accessorEndianness e);                      // write a chunk of bytes, maybe in reverse order
accessorStatus accessorWriteBytes(accessor_t * a, const void * ptr, size_t count);                                                  // write a chunk of bytes
accessorStatus accessorWriteRepeatedByte(accessor_t * a, uint8_t byte, size_t count);                                               // write a serie of identical bytes



// look-ahead

// similar to the read counterparts except that:
// - no coverage is recorded
// - cursor doesn't move
// - memory isn't allocated
// - byte count actually transferred is returned, in the [0...count] range
// - can't fail, although a returned byte count that is smaller than requested may be interpreted as a failure by the caller

// look ahead some bytes
// ptr is the buffer were up to count look-ahead bytes are copied
size_t accessorLookAheadEndianBytes(const accessor_t * a, void * ptr, size_t count, accessorEndianness e);                          // read up to count bytes (maybe in reverse order). ptr must be able to hold size bytes
size_t accessorLookAheadBytes(const accessor_t * a, void * ptr, size_t count);                                                      // read up to count bytes. ptr must be able to hold size bytes

// count bytes occuring before given delimiter, up to a maximum of countLimit
// no data is transferred transfer
// countLimit and returned count don't include delimiter
// returns accessorBeyondEnd if delimiter not found within limits
// countLimit == ACCESSOR_UNTIL_END means up to end of data, other countLimit values are taken literally
// delimiter is an array of delLength bytes
accessorStatus accessorLookAheadCountBytesBeforeDelimiter(const accessor_t * a, size_t * count, size_t countLimit, size_t delLength, const void * delimiter);

// this function is useful when minimizing memory transfers, such as e.g. (de)compressing or (de)crypting data from a read accessor to a write enabled accessor
// ptr returned from these function is only valid until next accessor cursor move
// this functions MUST BE USED WITH CAUTION, taking care to ACCESS ONLY BYTES IN THE [ptr...ptr+count-1] RANGE
// it is a good practice to set ptr to NULL as soon as data has been transferred, avoiding unintentional future access
size_t accessorLookAheadAvailableBytes(const accessor_t * a, const void ** ptr);                                                    // returns count and an internal pointer to all available data bytes (from cursor to end of window). cursor does not move, no coverage recorded



// string read

// the read string is converted to a zero-terminated string and is allocated, it's up to caller to free() it
// if length is not NULL, *length is set to returned string's (char|uint16_t|uin32_t) transfer count
// where applicable, returned length is not adjusted for NUL (char|uint16_t|uin32_t) in the middle of the string
accessorStatus accessorReadCString(accessor_t * a, char ** str, size_t * length);                                                   // read a C string up to trailing 0 byte end of string marker
accessorStatus accessorReadPString(accessor_t * a, char ** str, size_t * length);                                                   // read a pstring (one byte for string length followed by string)
accessorStatus accessorReadFixedLengthString(accessor_t * a, char ** str, size_t length);                                           // read an unterminated fixed length string, converted to C string
accessorStatus accessorReadPaddedString(accessor_t * a, char ** str, size_t * length, char pad);                                    // read a padded string, converted to C string, trailing padding removed, on input *length is the padded length, on return it is the length of result, stripped from trailing pad characters
accessorStatus accessorReadEndianString16(accessor_t * a, uint16_t ** str, size_t * length, accessorEndianness e);                  // read a 16-bits chars string up to NUL using specified endianness
accessorStatus accessorReadEndianString32(accessor_t * a, uint32_t ** str, size_t * length, accessorEndianness e);                  // read a 32-bits chars string up to NUL using specified endianness
accessorStatus accessorReadString16(accessor_t * a, uint16_t ** str, size_t * length);                                              // read a 16-bits chars string up to NUL
accessorStatus accessorReadString32(accessor_t * a, uint32_t ** str, size_t * length);                                              // read a 32-bits chars string up to NUL



// string write

// The accessorWrite...WithLength variants are intended to optimize speed when the string's length is known. Given length must match string's length else behavior is undefined.
// as its length is given, str doesn't have to be terminated by a NUL (8, 16 or 32 bits wide) character but a trailing NUL is nonetheless appended to written bytes, if needed
accessorStatus accessorWriteCStringWithLength(accessor_t * a, const char * str, size_t length);                                     // write a C string including trailing NUL
accessorStatus accessorWritePStringWithLength(accessor_t * a, const char * str, size_t length);                                     // write a C string as a pstring. if larger than 255 chars, accessorInvalidParameter is returned
accessorStatus accessorWritePaddedStringWithLength(accessor_t * a, const char * str, size_t length, size_t paddedLength, char pad); // write a C string as a padded string, padded up to paddedLength chars. if string is too long, accessorInvalidParameter is returned
accessorStatus accessorWriteEndianString16WithLength(accessor_t * a, const uint16_t * str, size_t length, accessorEndianness e);    // write a 16-bits chars string including trailing NUL using specified endianness
accessorStatus accessorWriteEndianString32WithLength(accessor_t * a, const uint32_t * str, size_t length, accessorEndianness e);    // write a 32-bits chars string including trailing NUL using specified endianness
accessorStatus accessorWriteString16WithLength(accessor_t * a, const uint16_t * str, size_t length);                                // write a 16-bits chars string including trailing NUL
accessorStatus accessorWriteString32WithLength(accessor_t * a, const uint32_t * str, size_t length);                                // write a 32-bits chars string including trailing NUL

// the same, when string length is unknown
accessorStatus accessorWriteCString(accessor_t * a, const char * str);                                                              // write a C string including trailing NUL
accessorStatus accessorWritePString(accessor_t * a, const char * str);                                                              // write a C string as a pstring. if larger than 255 chars, accessorInvalidParameter is returned
accessorStatus accessorWritePaddedString(accessor_t * a, const char * str, size_t paddedLength, char pad);                          // write a C string as a padded string, padded up to size chars. if string is too long, accessorInvalidParameter is returned
accessorStatus accessorWriteEndianString16(accessor_t * a, const uint16_t * str, accessorEndianness e);                             // write a 16-bits chars string including trailing NUL using specified endianness
accessorStatus accessorWriteEndianString32(accessor_t * a, const uint32_t * str, accessorEndianness e);                             // write a 32-bits chars string including trailing NUL using specified endianness
accessorStatus accessorWriteString16(accessor_t * a, const uint16_t * str);                                                         // write a 16-bits chars string including trailing NUL
accessorStatus accessorWriteString32(accessor_t * a, const uint32_t * str);                                                         // write a 32-bits chars string including trailing NUL



// block read/write/look-ahead without actual data transfer

// these functions are useful when minimizing memory transfers, such as e.g. (de)compressing or (de)crypting data from a read accessor to a write enabled accessor
// ptr returned from these function is only valid until next accessor cursor move
// these functions MUST BE USED WITH CAUTION, taking care to ACCESS ONLY BYTES IN THE [ptr...ptr+count-1] RANGE
// it is a good practice to set ptr to NULL as soon as data has been transferred, avoiding unintentional future access
accessorStatus accessorGetPointerForBytesToRead(accessor_t * a, const void ** ptr, size_t count);                                   // returns an internal pointer to requested data bytes. cursor does move, coverage recorded if enabled and not suspended.
accessorStatus accessorGetPointerForBytesToWrite(accessor_t * a, void ** ptr, size_t count);                                        // returns an internal pointer to uninitialized bytes that MUST be written by caller. cursor does move.




// coverage related

// set usage1 and usage2 for accessor's future coverage records
void accessorSetCoverageUsage(accessor_t * a, uintmax_t usage1, const void * usage2);

// add a single coverage record to accessor of count bytes at given offset of accessor's window
// forceOption may be used to override disabled coverage (but not suspended)
// count may be set to ACCESSOR_UNTIL_END
void accessorAddCoverageRecord(accessor_t * a, size_t offset, size_t count, uintmax_t usage1, const void * usage2, accessorCoverageForceOption forceOption);

// get or set coverage enabled status
accessorCoverageOption accessorIsCoverageAllowed(const accessor_t * a);                                                             // returns either accessorEnableCoverage or accessorDisableCoverage
void accessorAllowCoverage(accessor_t * a, accessorCoverageOption option);

// increment or decrement coverage suspend count. if suspend count is not 0, no coverage is recorded even if coverage is enabled
void accessorSuspendCoverage(accessor_t * a);
void accessorResumeCoverage(accessor_t * a);

// get the coverage record array
// size pointer may not be NULL.
// returned array may be NULL if *size is 0.
// this array isn't sorted/merged unless accessorEndOfCoverage() was called
const accessorCoverageRecord * accessorCoverageArray(const accessor_t * a, size_t * size);

// this will sort/merge the coverage records if required, coverage is NOT disabled
// compare function compares two coverage records and returns an integer < 0, == 0, or > 0 if "a" is considered to be respectively <, ==, or > to "b"
// compare may be NULL and defaults to an internal compare function that sorts by increasing offset, then by decreasing size, then by increasing usage1, then by increasing usage2
// merge checks if two records must be merged and, if so, merge them into "a".
// merge may be NULL and defaults to and internal function that merges overlapping record with matching user1 and user2
// merge function parameters a and b are such that a <= b in compare function semantic
void accessorSummarizeCoverage(accessor_t * a, int (* compare)(const void * a, const void * b), accessorMergeResult (* merge)(void * a, const void * b));



// various helpers

uint32_t accessorBuildNumber(void);                                                                                                 // get accessor toolkit build version

// swap two accessors
// if only one of a1 or a2 is readonly, the other becomes readonly, keeping its accessorClose write-specific action such as writing its content to file.
// if both a1 and a2 are read accessors, both keep their readonly status
// if both a1 and a2 are write accessors, both keep their write enabled status
accessorStatus accessorSwap(accessor_t ** a1, accessor_t ** a2);

// swap bytes
void accessorSwapBytes(void * ptr, size_t nbytes);                                                                                  // bytes are reordered from last to first

// swap integers, reversing their endianness
// on input, unused high order bits are ignored
// on output, unused high order bits are exact
uintmax_t accessorSwapUInt(uintmax_t x, size_t nbytes);                                                                             // valid for unsigned ints only
intmax_t accessorSwapInt(intmax_t x, size_t nbytes);                                                                                // valid for signed ints only
uint16_t accessorSwapUInt16(uint16_t x);                                                                                            // valid for signed and unsigned ints
uint32_t accessorSwapUInt24(uint32_t x);                                                                                            // valid for unsigned ints only
int32_t accessorSwapInt24(int32_t x);                                                                                               // valid for signed ints only
uint32_t accessorSwapUInt32(uint32_t x);                                                                                            // valid for signed and unsigned ints
uint64_t accessorSwapUInt64(uint64_t x);                                                                                            // valid for signed and unsigned ints


// file operations helpers

// returned string buffer is allocated, it's up to caller to free() it.
// to ease path constructions, returned string buffer has at least "additionalAllocationLength" extra bytes available at its end.
// basePath can be NULL or "", NULL is a synonym for ""
// path can't be NULL nor empty
// no canonicalization is done and path components existence is not checked, except that:
// - if basePath does not end with '/' (or '\' if applicable) and basePath exists AND is not a directory, path is relative to basePath's *parent* directory
// file will not be created but accessorPathOptionCreateDirectory and accessorPathOptionCreatePath are honored
accessorStatus accessorBuildPath(char ** result, const char * basePath, const char * path, accessorPathOptions pathOptions, size_t additionalAllocationLength);

// create directory at specified path (and possibly parent directories)
// accessorPathOptionCreateDirectory is implied and doesn't need to be set, accessorPathOptionCreatePath is optional
accessorStatus accessorCreateDirectory(const char * basePath, const char * path, accessorPathOptions pathOptions);



#ifdef __cplusplus
}
#endif



#endif /* accessor_h */
