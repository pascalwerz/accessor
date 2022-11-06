#include "accessor.h"

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>       // fstat
#include <unistd.h>         // write, sysconf
#include <fcntl.h>          // open
#include <errno.h>          // errno_t
#include <limits.h>         // CHAR_BIT

// if ACCESSOR_USE_MMAP is true, accessor will try mapping data in memory instead of reading it.
#if defined(TARGET_MSYS) && TARGET_MSYS
#define ACCESSOR_USE_MMAP                   0
#else
#define ACCESSOR_USE_MMAP                   1
#endif

#if ACCESSOR_USE_MMAP
#include <sys/mman.h>       // mmap, munmap
#endif

#if CHAR_BIT != 8
#error Unsupported system, 'char' is not 8-bit wide.
#endif



#define KB  1024
#define MB  (1024 * KB)
#define GB  (1024 * MB)
#define TB  (1024 * GB)



// maximum read() transfer size. 1 GB seems safe as 2 GB leads to EINVAL errors, Linux limit is just under 2 GB
#define ACCESSOR_FILE_READ_SIZE_LIMIT       (1 * GB)

// file read accessors with a window smaller than ACCESSOR_MMAP_MIN_FILESIZE will not be mapped but read in memory.
#ifndef ACCESSOR_MMAP_MIN_FILESIZE
#define ACCESSOR_MMAP_MIN_FILESIZE          (16 * 1024)
#endif

// helper to select a value in 32/64 bit code
#define ACCESSOR_SELECT_32_64(X32, X64)     ((sizeof (void *) * CHAR_BIT < 64) ? (X32) : (X64))



// private typedefs
typedef struct _accessor_t
{
    // for all accessor_t types
    uintmax_t referenceCount;
    size_t windowOffset;
    size_t baseAccessorWindowOffset;    // window offset in baseAccessor's data
    size_t windowSize;                  // for writeEnabled accessors, this is the highwater mark
    size_t cursor;                      // in the [0, windowSize] range (cursor == windowSize means it is after end of data, hence availableBytes == 0)
    size_t availableBytes;              // in the [0, windowSize] range
    char isBaseAccessor;
    char writeEnabled;
    struct _accessor_t * baseAccessor;  // "weak" reference, base accessor are their own base accessor

    // for base accessor_t only
    uint8_t * data;                     // for readonly accessors, can't be moved/reallocated
    size_t dataMaxSize;                 // allocated or mapped memory segment size
    size_t dataFileOffset;              // offset of allocated or mapped memory in readonly file
    size_t granularity;
    char isMapped;
    char mayBeReallocated;
    char freeOnClose;
    int inputFileDescriptor;
    int outputFileDescriptor;
    char writeOnClose;

    // for sub accessor_t only
    struct _accessor_t * superAccessor; // "strong" reference incrementing super's referenceCount

    // common data for all accessor types
    accessorEndianness endianness;
    size_t * cursorStack;               // cursor push/pop stack. allocation grows but never shrinks
    size_t cursorStackAllocation;
    size_t cursorStackSize;
    char coverageEnabled;
    uintmax_t coverageSuspendCount;
    size_t coverageStartOffset;
    accessorCoverageRecord * coverageArray;
    size_t coverageArraySize;
    size_t coverageArrayAllocation;
    uintmax_t coverageUsage1;
    const void * coverageUsage2;
} _accessor_t;



// private prototypes
static inline uintmax_t accessorPrivateReadUIntAtPointer(const void * ptr, accessorEndianness e, size_t nbytes);
static inline uint16_t accessorPrivateReadUInt16AtPointer(const uint8_t * ptr, accessorEndianness e);
static inline uint32_t accessorPrivateReadUInt24AtPointer(const uint8_t * ptr, accessorEndianness e);
static inline uint32_t accessorPrivateReadUInt32AtPointer(const uint8_t * ptr, accessorEndianness e);
static inline uint64_t accessorPrivateReadUInt64AtPointer(const uint8_t * ptr, accessorEndianness e);

static inline intmax_t accessorPrivateReadIntAtPointer(const void * ptr, accessorEndianness e, size_t nbytes);
static inline int16_t accessorPrivateReadInt16AtPointer(const uint8_t * ptr, accessorEndianness e);
static inline int32_t accessorPrivateReadInt24AtPointer(const uint8_t * ptr, accessorEndianness e);
static inline int32_t accessorPrivateReadInt32AtPointer(const uint8_t * ptr, accessorEndianness e);
static inline int64_t accessorPrivateReadInt64AtPointer(const uint8_t * ptr, accessorEndianness e);

static inline void accessorPrivateWriteUIntAtPointer(void * ptr, uintmax_t x, accessorEndianness e, size_t nbytes);
static inline void accessorPrivateWriteUInt16AtPointer(uint8_t * ptr, uint16_t x, accessorEndianness e);
static inline void accessorPrivateWriteUInt24AtPointer(uint8_t * ptr, uint32_t x, accessorEndianness e);
static inline void accessorPrivateWriteUInt32AtPointer(uint8_t * ptr, uint32_t x, accessorEndianness e);
static inline void accessorPrivateWriteUInt64AtPointer(uint8_t * ptr, uint64_t x, accessorEndianness e);

static void accessorPrivateInitializeEndianness(void);

static accessorStatus accessorPrivateCreateEmpty(accessor_t ** a);

static accessorStatus accessorPrivateGetPointerForWrite(uint8_t ** r, accessor_t * a, size_t nbytes);  // accessor will grow if needed
static accessorStatus accessorPrivateGrow(accessor_t * a, size_t newSize);

static inline void accessorPrivateOpenCoverage(accessor_t * a);
static void accessorPrivateCloseCoverage(accessor_t * a);
static int accessorPrivateCoverageCompare(const void * p1, const void * p2);
static accessorMergeResult accessorPrivateCoverageMerge(void * p1, const void * p2);

static inline int accessorPrivateExtendPointerSizeAllocation(void ** ptr, size_t * size, size_t * alloc, size_t newsize, size_t allocChunk, size_t sizeofdata);

static inline char accessorPrivateIsPathSeparator(char c, accessorPathOptions pathOptions);                     // reply true for '/' (and for '\\' if accessorPathOptionConvertBackslash)
static accessorStatus accessorPrivateCreateEnclosingDirectory(char * path, accessorPathOptions pathOptions);    // private specialized code. path MUST NOT be const and MUST have been cleaned up by accessorBuildPath. only accessorPathOptionCreatePath option is honored

static inline uintmax_t accessorPrivateRoundUpwardsToNonNullMultiple(uintmax_t x, uintmax_t m);     // return value is a non-null multiple of m and strictly greater than x



// private global variables
static char accessorPrivateAccessorInited = 0;                                  // endianness initialization flag
static char accessorPrivateIsBigEndianness[ACCESSOR_ENDIANNESS_COUNT];          // resolve all 4 endianness to accessorBig or accessorLittle
static char accessorPrivateIsReverseEndianness[ACCESSOR_ENDIANNESS_COUNT];      // resolve all 4 endianness to accessorNative or accessorReverse
static accessorEndianness accessorPrivateNativeEndianness = accessorNative;     // will be set to either accessorBig or accessorLittle by accessorPrivateInitializeEndianness()
static accessorEndianness accessorPrivateDefaultEndianness = accessorNative;    // can be any endianness



uint32_t accessorBuildNumber(void)
{
    return ACCESSOR_BUILD_NUMBER;
}



static void accessorPrivateInitializeEndianness(void)
{
    uint64_t wu64;
    uint64_t ru64;
    uint8_t * tmp;

    if (accessorPrivateAccessorInited == 0)
    {
        wu64 = 0x123456789abcdef0;
        tmp = (uint8_t *) &wu64;
        ru64 = tmp[0];
        ru64 = (ru64 << 8) | tmp[1];
        ru64 = (ru64 << 8) | tmp[2];
        ru64 = (ru64 << 8) | tmp[3];
        ru64 = (ru64 << 8) | tmp[4];
        ru64 = (ru64 << 8) | tmp[5];
        ru64 = (ru64 << 8) | tmp[6];
        ru64 = (ru64 << 8) | tmp[7];

        if (ru64 == 0x123456789abcdef0)
            accessorPrivateNativeEndianness = accessorBig;
        else if (ru64 == 0xf0debc9a78563412)
            accessorPrivateNativeEndianness = accessorLittle;
        else
        {
            // unsupported mixed endianness
            fprintf(stderr, "fatal: unsupported mixed endianness\n");
            exit(127);
        }
        accessorPrivateIsBigEndianness[accessorBig]         = 1;
        accessorPrivateIsBigEndianness[accessorLittle]      = 0;
        accessorPrivateIsBigEndianness[accessorNative]      = accessorPrivateNativeEndianness == accessorBig;
        accessorPrivateIsBigEndianness[accessorReverse]     = accessorOppositeEndianness(accessorPrivateNativeEndianness) == accessorBig;

        accessorPrivateIsReverseEndianness[accessorBig]     = accessorBig == accessorOppositeEndianness(accessorPrivateNativeEndianness);
        accessorPrivateIsReverseEndianness[accessorLittle]  = accessorLittle == accessorOppositeEndianness(accessorPrivateNativeEndianness);
        accessorPrivateIsReverseEndianness[accessorNative]  = 0;
        accessorPrivateIsReverseEndianness[accessorReverse] = 1;

        accessorPrivateAccessorInited = 1;
    }

}



static inline int accessorPrivateExtendPointerSizeAllocation(void ** ptr, size_t * size, size_t * alloc, size_t newsize, size_t allocChunk, size_t sizeofdata)
{
    void * newptr;
    size_t newalloc;


    if (newsize > *alloc)
    {
        newalloc = (newsize - 1 + allocChunk) - (newsize - 1) % allocChunk;
        newptr = realloc(*ptr, newalloc * sizeofdata);
        if (newptr == NULL)
            return 1;
        else
        {
            *ptr = newptr;
            *alloc = newalloc;
        }
    }
    *size = newsize;

    return 0;
}



static accessorStatus accessorPrivateCreateEmpty(accessor_t ** a)
{
    accessor_t * result;


    accessorPrivateInitializeEndianness();

    if (*a != ACCESSOR_INIT)
        return accessorInvalidParameter;

    result = malloc(sizeof(*result));
    if (result == NULL)
        return accessorOutOfMemory;

    result->referenceCount = 0;
    result->windowOffset = 0;
    result->baseAccessorWindowOffset = 0;
    result->windowSize = 0;
    result->cursor = 0;
    result->availableBytes = 0;
    result->isBaseAccessor = 1;
    result->writeEnabled = 0;
    result->baseAccessor = result;

    result->data = NULL;
    result->dataMaxSize = 0;
    result->dataFileOffset = 0;
    result->granularity = ACCESSOR_SELECT_32_64(4 * KB, 64 * KB);
    result->isMapped = 0;
    result->mayBeReallocated = 0;
    result->freeOnClose = 0;
    result->inputFileDescriptor = -1;
    result->outputFileDescriptor = -1;
    result->writeOnClose = 0;

    result->superAccessor = ACCESSOR_INIT;

    result->endianness = accessorPrivateDefaultEndianness;

    result->cursorStack = NULL;
    result->cursorStackAllocation = 0;
    result->cursorStackSize = 0;

    result->coverageEnabled = 0;
    result->coverageSuspendCount = 0;
    result->coverageStartOffset = 0;
    result->coverageArray = NULL;
    result->coverageArraySize = 0;
    result->coverageArrayAllocation = 0;
    result->coverageUsage1 = 0;
    result->coverageUsage2 = NULL;


    *a = result;

    return accessorOk;
}



accessorStatus accessorOpenReadingMemory(accessor_t ** a, const void * ptr, size_t dataSize, accessorFreeOnCloseOption freeOption, size_t windowOffset, size_t windowSize)
{
    accessorStatus status;

    status = accessorPrivateCreateEmpty(a);
    if (status != accessorOk)
        return status;

    if (windowSize == ACCESSOR_UNTIL_END)
        windowSize = dataSize - windowOffset;

    if (windowOffset + windowSize > dataSize)
        return accessorBeyondEnd;

    (*a)->data = (uint8_t *) ptr;           // intentionally discarding const qualifier: accessor is read only
    (*a)->windowOffset = windowOffset;
    (*a)->baseAccessorWindowOffset = windowOffset;
    (*a)->windowSize = windowSize;
    (*a)->cursor = 0;
    (*a)->availableBytes = windowSize;
    (*a)->dataMaxSize = dataSize;
    (*a)->mayBeReallocated = 0;             // other code may access data even if freeOption == accessorFreeOnClose
    (*a)->freeOnClose = freeOption == accessorFreeOnClose ? 1 : 0;

    return accessorOk;
}



accessorStatus accessorOpenReadingFile(accessor_t ** a, const char * basePath, const char * path, accessorPathOptions pathOptions, size_t windowOffset, size_t windowSize)
{
    accessorStatus status;
    struct stat st;
    char * name;
    int file;
    size_t fileSize;


    status = accessorPrivateCreateEmpty(a);
    if (status != accessorOk)
        return status;

    pathOptions &= (accessorPathOptions) ~(accessorPathOptionCreateDirectory | accessorPathOptionCreatePath);    // no directory should be created
    status = accessorBuildPath(&name, basePath, path, pathOptions, 0);
    if (status != accessorOk)
        return status;

    if (stat(name, &st) != 0)
    {
        free(name);
        return accessorOpenError;
    }

    fileSize = (size_t) st.st_size;

    if ((file = open(name, O_RDONLY)) == -1)
    {
        free(name);
        accessorClose(a);
        return accessorOpenError;
    }

    if (windowOffset > fileSize)
    {
        close(file);
        free(name);
        accessorClose(a);

        return accessorBeyondEnd;
    }

    if (windowSize == ACCESSOR_UNTIL_END)
        windowSize = fileSize - windowOffset;

    if (windowOffset + windowSize > fileSize)
    {
        close(file);
        free(name);
        accessorClose(a);

        return accessorBeyondEnd;
    }

#if ACCESSOR_USE_MMAP
    long pageSize = -1;
    if (pageSize == -1)
        pageSize = sysconf(_SC_PAGESIZE);

    if (windowSize && windowSize >= ACCESSOR_MMAP_MIN_FILESIZE && pageSize != -1)
    {
        size_t fileMapOffset = windowOffset - (windowOffset % (size_t) pageSize);
        size_t fileMapSize = windowSize + (windowOffset % (size_t) pageSize);

        (*a)->data = mmap(NULL, fileMapSize, PROT_READ, MAP_FILE | MAP_PRIVATE, file, (off_t) fileMapOffset);
        if ((*a)->data != MAP_FAILED)
        {
            (*a)->isMapped = 1;
            (*a)->freeOnClose = 0;
            (*a)->dataFileOffset = fileMapOffset;
            (*a)->windowOffset = windowOffset % (size_t) pageSize;
            (*a)->dataMaxSize = fileMapSize;
            (*a)->baseAccessorWindowOffset = (*a)->windowOffset;
        }
        else
        {
            (*a)->data = NULL;  // MAP_FAILED is (or may be) different from NULL as mmap can be instructed to map a segment at address 0
        }
    }
#endif
    // if the contitional ACCESSOR_USE_MMAP block was compiled: if mmap was ruled out or failed, read the file data in memory
    // if the contitional ACCESSOR_USE_MMAP block wasn't compiled: simply read the file data in memory
    if ((*a)->data == NULL)
    {
        (*a)->data = malloc(windowSize > 0 ? windowSize : 1);   // ensure at least 1 byte is allocated
        if ((*a)->data == NULL || lseek(file, (off_t) windowOffset, SEEK_SET) == -1)
        {
            close(file);
            free(name);
            accessorClose(a);

            return accessorOutOfMemory;
        }

        for (size_t offset = 0; offset < windowSize ;)
        {
            size_t transferSize;
            ssize_t bytesTransferred;


            transferSize = windowSize - offset;
            if (transferSize > ACCESSOR_FILE_READ_SIZE_LIMIT)
                 transferSize = ACCESSOR_FILE_READ_SIZE_LIMIT;  // limit transfer size to a reasonable value

            bytesTransferred = read(file, (*a)->data + offset, transferSize);
            if (bytesTransferred == -1 || bytesTransferred == 0)
            {
                close(file);
                free(name);
                accessorClose(a);

                return accessorHostError;
            }
            offset += (size_t) bytesTransferred;
        }
        (*a)->dataFileOffset = windowOffset;
        (*a)->windowOffset = 0;
        (*a)->dataMaxSize = windowSize;
        (*a)->baseAccessorWindowOffset = 0;
        (*a)->freeOnClose = 1;
    }

    (*a)->windowSize = windowSize;
    (*a)->cursor = 0;
    (*a)->availableBytes = windowSize;
    (*a)->mayBeReallocated = 0;
    (*a)->inputFileDescriptor = file;

    free(name);

    return accessorOk;
}



accessorStatus accessorOpenWritingMemory(accessor_t ** a, size_t initialAllocation, size_t granularity)
{
    accessorStatus status;

    status = accessorPrivateCreateEmpty(a);
    if (status != accessorOk)
        return status;

    if (granularity == 0)
        granularity = ACCESSOR_SELECT_32_64(4 * KB, 64 * KB);

    if (initialAllocation > ACCESSOR_SELECT_32_64(1 * MB, 16 * MB))
    {
        initialAllocation = ACCESSOR_SELECT_32_64(1 * MB, 16 * MB);
        granularity = ACCESSOR_SELECT_32_64(1 * MB, 16 * MB);
    }

    initialAllocation = accessorPrivateRoundUpwardsToNonNullMultiple(initialAllocation, granularity);
    if (((*a)->data = malloc(initialAllocation)) == NULL)
    {
        accessorClose(a);
        return accessorOutOfMemory;
    }
    memset((*a)->data, 0, initialAllocation);

    (*a)->dataMaxSize = initialAllocation;
    (*a)->windowOffset = 0;
    (*a)->baseAccessorWindowOffset = 0;
    (*a)->windowSize = 0;
    (*a)->cursor = 0;
    (*a)->availableBytes = 0;
    (*a)->writeEnabled = 1;
    (*a)->granularity = granularity;
    (*a)->mayBeReallocated = 1;
    (*a)->freeOnClose = 1;

    return accessorOk;
}



accessorStatus accessorOpenWritingFile(accessor_t ** a, const char * basePath, const char * path, accessorPathOptions pathOptions, mode_t mode, size_t initialAllocation, size_t granularity)
{
    accessorStatus status;
    char * name;


    status = accessorPrivateCreateEmpty(a);
    if (status != accessorOk)
        return status;

    if (granularity == 0)
        granularity = ACCESSOR_SELECT_32_64(4 * KB, 64 * KB);

    if (initialAllocation > ACCESSOR_SELECT_32_64(1 * MB, 16 * MB))
    {
        initialAllocation = ACCESSOR_SELECT_32_64(1 * MB, 16 * MB);
        granularity = ACCESSOR_SELECT_32_64(1 * MB, 16 * MB);
    }

    initialAllocation = accessorPrivateRoundUpwardsToNonNullMultiple(initialAllocation, granularity);
    if (((*a)->data = malloc(initialAllocation)) == NULL)
    {
        accessorClose(a);
        return accessorOutOfMemory;
    }
    memset((*a)->data, 0, initialAllocation);

    status = accessorBuildPath(&name, basePath, path, pathOptions, 0);
    if (status != accessorOk)
        return status;

    if (((*a)->outputFileDescriptor = open(name, O_WRONLY | O_CREAT | O_TRUNC, mode)) == -1)
    {
        free(name);
        accessorClose(a);
        return accessorOpenError;
    }

    (*a)->windowOffset = 0;
    (*a)->baseAccessorWindowOffset = 0;
    (*a)->windowSize = 0;
    (*a)->cursor = 0;
    (*a)->availableBytes = 0;
    (*a)->writeEnabled = 1;
    (*a)->dataMaxSize = initialAllocation;
    (*a)->granularity = granularity;
    (*a)->mayBeReallocated = 1;
    (*a)->freeOnClose = 1;
    (*a)->writeOnClose = 1;

    free(name);

    return accessorOk;
}



accessorStatus accessorWriteToFile(const accessor_t * a, const char * basePath, const char * path, accessorPathOptions pathOptions, mode_t mode, size_t windowOffset, size_t windowSize)
{
    accessorStatus status;
    int fileDescriptor;
    char * name;
    ssize_t writtenBytes;


    if (windowOffset > a->windowSize)
        return accessorBeyondEnd;

    if (windowSize == ACCESSOR_UNTIL_END)
        windowSize = a->windowSize - windowOffset;

    if (windowOffset + windowSize > a->windowSize)
        return accessorBeyondEnd;

    status = accessorBuildPath(&name, basePath, path, pathOptions, 0);
    if (status != accessorOk)
        return status;

    if ((fileDescriptor = open(name, O_WRONLY | O_CREAT | O_TRUNC, mode)) == -1)
    {
        free(name);
        return accessorOpenError;
    }
    free(name);

    writtenBytes = write(fileDescriptor, a->baseAccessor->data + a->baseAccessorWindowOffset + windowOffset, windowSize);

    close(fileDescriptor);

    if (writtenBytes < 0 || (size_t) writtenBytes != windowSize)
        return accessorWriteError;

    return accessorOk;
}



accessorStatus accessorOpenReadingAccessorBytes(accessor_t ** a, accessor_t * supera, size_t count)
{
    accessorStatus status;


    if (supera->writeEnabled)
        return accessorInvalidParameter;

    if (count == ACCESSOR_UNTIL_END)
        count = supera->availableBytes;
    else if (count > supera->availableBytes)
        return accessorBeyondEnd;

    status = accessorPrivateCreateEmpty(a);
    if (status != accessorOk)
        return status;

    supera->referenceCount++;

    (*a)->windowOffset = supera->cursor;
    (*a)->baseAccessorWindowOffset = supera->baseAccessorWindowOffset + supera->cursor;
    (*a)->windowSize = count;
    (*a)->cursor = 0;
    (*a)->availableBytes = count;
    (*a)->isBaseAccessor = 0;
    (*a)->writeEnabled = 0;
    (*a)->baseAccessor = supera->baseAccessor;
    (*a)->superAccessor = supera;
    (*a)->endianness = supera->endianness;      // inherit from supera

    accessorPrivateOpenCoverage(supera);

    supera->cursor += count;
    supera->availableBytes -= count;

    accessorPrivateCloseCoverage(supera);

    return accessorOk;
}



accessorStatus accessorOpenReadingAccessorWindow(accessor_t ** a, accessor_t * supera, size_t windowOffset, size_t windowSize)
{
    accessorStatus status;


    if (supera->writeEnabled)
        return accessorInvalidParameter;

    status = accessorPrivateCreateEmpty(a);
    if (status != accessorOk)
        return status;

    if (windowOffset > supera->windowSize)
    {
        accessorClose(a);
        return accessorBeyondEnd;
    }

    if (windowSize == ACCESSOR_UNTIL_END)
        windowSize = supera->windowSize - windowOffset;

    if (windowOffset + windowSize > supera->windowSize)
    {
        accessorClose(a);
        return accessorBeyondEnd;
    }

    supera->referenceCount++;

    (*a)->windowOffset = windowOffset;
    (*a)->baseAccessorWindowOffset = windowOffset + supera->baseAccessorWindowOffset;
    (*a)->windowSize = windowSize;
    (*a)->cursor = 0;
    (*a)->availableBytes = windowSize;
    (*a)->isBaseAccessor = 0;
    (*a)->writeEnabled = 0;
    (*a)->baseAccessor = supera->baseAccessor;
    (*a)->superAccessor = supera;
    (*a)->endianness = supera->endianness;      // inherit endianness from supera

    return accessorOk;
}



accessorStatus accessorClose(accessor_t ** a)
{
    accessorStatus status;


    if (*a == ACCESSOR_INIT)
        return accessorInvalidParameter;

    if ((*a)->referenceCount > 0)
    {
        (*a)->referenceCount--;
        *a = ACCESSOR_INIT;     // caller must forget about this accessor anyway

        return accessorOk;
    }

    if ((*a)->writeOnClose && (*a)->outputFileDescriptor != -1 && (*a)->data != NULL)
    {
        ssize_t writtenBytes = write((*a)->outputFileDescriptor, (*a)->data, (*a)->windowSize);
        if (writtenBytes < 0 || (size_t) writtenBytes != (*a)->windowSize)
            return accessorWriteError;
    }

    if ((*a)->inputFileDescriptor != -1)
        close((*a)->inputFileDescriptor);

    if ((*a)->outputFileDescriptor != -1)
        close((*a)->outputFileDescriptor);

    if ((*a)->isBaseAccessor)
    {
#if ACCESSOR_USE_MMAP
        if ((*a)->isMapped)
        {
            (void) munmap((*a)->data, (*a)->dataMaxSize);    // errors intentionally ignored
        }
#endif

        if ((*a)->freeOnClose && (*a)->data != NULL)
        {
            free((*a)->data);
        }
    }
    else
    {
        status = accessorClose(&(*a)->superAccessor);
        if (status != accessorOk)
            return status;
    }

    if ((*a)->cursorStackAllocation)
        free((*a)->cursorStack);

    if ((*a)->coverageArrayAllocation)
        free((*a)->coverageArray);

    free(*a);
    *a = ACCESSOR_INIT;

    return accessorOk;
}



static accessorStatus accessorPrivateGetPointerForWrite(uint8_t ** r, accessor_t * a, size_t nbytes)
{
    accessorStatus status;
    size_t offset;
    size_t endOffset;


    offset = a->baseAccessorWindowOffset + a->cursor;
    endOffset = offset + nbytes;

    if (endOffset > a->windowSize)
    {
        status = accessorPrivateGrow(a->baseAccessor, endOffset);
        if (status != accessorOk)
            return  status;
    }

    a->cursor += nbytes;
    a->availableBytes -= nbytes;

    *r = a->baseAccessor->data + offset;

    return accessorOk;
}



size_t accessorCursor(const accessor_t * a)
{
    return a->cursor;
}



size_t accessorRootWindowOffset(const accessor_t * a)
{
    return a->baseAccessorWindowOffset + a->baseAccessor->dataFileOffset;
}



size_t accessorSize(const accessor_t * a)
{
    return a->windowSize;
}



accessorStatus accessorTruncate(accessor_t * a)
{
    if (!a->writeEnabled)
        return accessorReadOnlyError;

    a->windowSize = a->cursor;
    a->availableBytes = 0;

    return accessorOk;
}



accessorStatus accessorSeek(accessor_t * a, ssize_t offset, int whence)
{
    accessorStatus status;
    size_t newCursor;


    switch(whence)
    {
    default:
        return accessorInvalidParameter;                    // catch SEEK_HOLE, SEEK_DATA etc.
    case SEEK_SET:
        newCursor = (size_t) offset;
        break;
    case SEEK_CUR:
        newCursor = a->cursor + (size_t) offset;
        break;
    case SEEK_END:
        newCursor = a->windowSize + (size_t) offset;
        break;
    }

    if (a->writeEnabled && newCursor > a->windowSize)
    {
        size_t windowSizeBeforeGrow;


        windowSizeBeforeGrow = a->windowSize;
        if (newCursor > a->baseAccessor->dataMaxSize)
        {
            status = accessorPrivateGrow(a->baseAccessor, newCursor);
            if (status != accessorOk)
                return  status;
        }
        memset(a->baseAccessor->data + windowSizeBeforeGrow, 0, newCursor - windowSizeBeforeGrow);
        a->windowSize = newCursor;
    }

    if (newCursor > a->windowSize)
        return accessorBeyondEnd;

    a->cursor = newCursor;
    a->availableBytes = a->windowSize - newCursor;

    return accessorOk;
}



size_t accessorAvailableBytesCount(const accessor_t * a)
{
    return a->availableBytes;
}



accessorStatus accessorPushCursor(accessor_t * a)
{
    if (accessorPrivateExtendPointerSizeAllocation((void *) &a->cursorStack, &a->cursorStackSize, &a->cursorStackAllocation, a->cursorStackSize + 1, 64, sizeof(*a->cursorStack)))
        return accessorOutOfMemory;
    a->cursorStack[a->cursorStackSize - 1] = a->cursor;

    return accessorOk;
}



accessorStatus accessorPopCursor(accessor_t * a)
{
    if (a->cursorStackSize < 1)
        return accessorInvalidParameter;

    return accessorSeek(a, (ssize_t) a->cursorStack[--a->cursorStackSize], SEEK_SET);
}



accessorStatus accessorPopCursors(accessor_t * a, size_t n)
{
    if (a->cursorStackSize < n)
        return accessorInvalidParameter;

    return accessorSeek(a, (ssize_t) a->cursorStack[a->cursorStackSize -= n], SEEK_SET);
}



accessorStatus accessorDropCursor(accessor_t * a)
{
    if (a->cursorStackSize < 1)
        return accessorInvalidParameter;

    a->cursorStackSize--;

    return accessorOk;
}



accessorStatus accessorDropCursors(accessor_t * a, size_t n)
{
    if (a->cursorStackSize< n)
        return accessorInvalidParameter;

    a->cursorStackSize -= n;

    return accessorOk;
}



static accessorStatus accessorPrivateGrow(accessor_t * a, size_t newSize)
{
    uint8_t * newData;
    size_t newDataSize;


    if (newSize <= a->windowOffset + a->windowSize)
        return accessorOk;

    if (a->dataMaxSize < newSize)
    {
        if (!a->mayBeReallocated)
            return accessorInvalidParameter;

        newDataSize = accessorPrivateRoundUpwardsToNonNullMultiple(newSize, a->granularity);
        newData = realloc(a->data, newDataSize);

        if (newData == NULL)
            return accessorOutOfMemory;

        a->data = newData;
        a->dataMaxSize = newDataSize;
    }

    a->windowSize = newSize;
    a->availableBytes = newSize - a->cursor;

    return accessorOk;
}



static inline char accessorPrivateIsPathSeparator(char c, accessorPathOptions pathOptions)
{
    if (c == '/')
        return 1;

    if (c != '\\' || !(pathOptions & accessorPathOptionConvertBackslash))
        return 0;

    return 1;
}



static inline uintmax_t accessorPrivateRoundUpwardsToNonNullMultiple(uintmax_t x, uintmax_t m)
{
    // given x = q.m + r, where q and r are positive or null integers
    // result =     x    +  m - (x % m)
    //        = q.m + r  +  m -    r
    //        = q.m + m
    //        = (q + 1).m
    // result is indeed multiple of m and > 0 and > x
    return x + m - (x % m);
}



accessorStatus accessorBuildPath(char ** result, const char * basePath, const char * path, accessorPathOptions pathOptions, size_t additionalAllocationLength)
{
    size_t basePathLength;
    size_t pathLength;
    size_t resultLength;
    char basePathIsDirectoryPath;
    char * dst;


    if (basePath == NULL)
    {
        basePath = "";
        basePathLength = 0;
    }
    else
    {
        basePathLength = strlen(basePath);
    }

    if (path == NULL)
    {
        path = "";
        pathLength = 0;
    }
    else
    {
        pathLength = strlen(path);
    }

    // starting here, basePath and path will be handled as non-terminated strings to allow for non-destructive head and tail removal

    // deduplicate leading separators
    while (basePathLength >= 2 && accessorPrivateIsPathSeparator(basePath[0], pathOptions) && accessorPrivateIsPathSeparator(basePath[1], pathOptions))
    {
        basePath++;
        basePathLength--;
    }
    while (pathLength >= 2 && accessorPrivateIsPathSeparator(path[0], pathOptions) && accessorPrivateIsPathSeparator(path[1], pathOptions))
    {
        path++;
        pathLength--;
    }

    // remove trailing separators
    basePathIsDirectoryPath = 0;
    while (basePathLength >= 2 && accessorPrivateIsPathSeparator(basePath[basePathLength - 1], pathOptions))
    {
        basePathIsDirectoryPath = 1;   // remember basePath is a directory path
        basePathLength--;
    }
    while (pathLength >= 2 && accessorPrivateIsPathSeparator(path[pathLength - 1], pathOptions))
    {
        pathLength--;
    }

    // ensure path is relative
    if (pathOptions & accessorPathOptionPathIsRelative)
    {
        if (pathLength > 0 && accessorPrivateIsPathSeparator(path[0], pathOptions))
        {
            // remove leading separator to make path relative
            path++;
            pathLength--;
        }
    }

    // path can't be empty
    if (pathLength == 0)
        return accessorInvalidParameter;

    // ensure destination is large enough
    dst = malloc(basePathLength + 1 + pathLength + 1 + additionalAllocationLength);
    if (dst == NULL)
        return accessorOutOfMemory;

    if (accessorPrivateIsPathSeparator(path[0], pathOptions))
    {
        // result is simply path
        memcpy(dst, path, pathLength);
        resultLength = pathLength;
    }
    else
    {
        if (pathLength == 0)
        {
            // result is simply basePath
            memcpy(dst, basePath, basePathLength);
            resultLength = basePathLength;
        }
        else
        {
            // if basePath exists AND is not a directory, result is parentPath(basePath) + '/' + path, or parentPath(basePath) + path if basePath ends with '/')
            // if basePath doesn't exist OR is a directory, result is basePath + '/' + path, or basePath + path if basePath ends with '/')
            struct stat st;


            memcpy(dst, basePath, basePathLength);
            dst[basePathLength] = 0;

            if (pathOptions & accessorPathOptionConvertBackslash)  // convert \ to / for the first part of the path
                for (size_t i = 0; i < basePathLength; i++)
                    if (dst[i] == '\\')
                        dst[i] = '/';

            if (!basePathIsDirectoryPath && stat(dst, &st) == 0 && !(st.st_mode & S_IFDIR))
            {
                // basePath exists AND is not a directory, convert it to its parent path
                char foundSeparator;


                foundSeparator = 0;
                for (size_t i = basePathLength; i > 1; i--)
                {
                    if (dst[i - 1] == '/')
                    {
                        basePathLength = i - 1;
                        foundSeparator = 1;
                        break;
                    }
                }
                if (!foundSeparator)
                {
                    basePathLength = 0;
                }
            }

            resultLength = basePathLength;

            if (basePathLength >= 1 && !accessorPrivateIsPathSeparator(basePath[basePathLength - 1], pathOptions))
                dst[resultLength++] = '/';

            memcpy(dst + resultLength, path, pathLength);
            resultLength += pathLength;
        }
    }

    if (pathOptions & accessorPathOptionConvertBackslash)
        for (size_t i = basePathLength; i < resultLength; i++)  // convert \ to / for the second part of the path
            if (dst[i] == '\\')
                dst[i] = '/';


    dst[resultLength] = 0;

    if (pathOptions & accessorPathOptionCreateDirectory || pathOptions & accessorPathOptionCreatePath)
        accessorPrivateCreateEnclosingDirectory(dst, pathOptions);

    *result = dst;

    return accessorOk;
}



accessorStatus accessorCreateDirectory(const char * basePath, const char * path, accessorPathOptions pathOptions)
{
    accessorStatus status;
    int mkdirStatus;
    char * directoryPath;


    status = accessorBuildPath(&directoryPath, basePath, path, pathOptions & (accessorPathOptions) ~(accessorPathOptionCreateDirectory | accessorPathOptionCreatePath), 0);  // don't request directory creation
    if (status != accessorOk)
        return status;

    mkdirStatus = mkdir(directoryPath, 0777);
    if (mkdirStatus == 0)
    {
        free(directoryPath);
        return accessorOk;
    }

    if (mkdirStatus == -1 && (errno == EEXIST || errno == EISDIR))
    {
        free(directoryPath);
        return accessorOk;
    }

    // parent directory doesn't exist
    if (mkdirStatus == -1 && errno == ENOENT && pathOptions & accessorPathOptionCreatePath)
    {
        // create parent and retry
        status = accessorPrivateCreateEnclosingDirectory(directoryPath, pathOptions);
        if (status != 0)
        {
            free(directoryPath);
            return status;
        }

        mkdirStatus = mkdir(directoryPath, 0777);
        if (mkdirStatus == 0)
        {
            free(directoryPath);
            return accessorOk;
        }

        if (mkdirStatus == -1 && (errno == EEXIST || errno == EISDIR))
        {
            free(directoryPath);
            return accessorOk;
        }
    }

    free(directoryPath);
    return accessorHostError;
}



static accessorStatus accessorPrivateCreateEnclosingDirectory(char * path, accessorPathOptions pathOptions)
{
    accessorStatus status;
    int mkdirStatus;
    char * slashPos;


    slashPos = strrchr(path, '/');
    if (slashPos == NULL)
    {
        // parent of a relative path is ".", which already exists and so doesn't need to be created
        return accessorOk;
    }

    *slashPos = 0;      // temporary shorten path to its enclosing directory
    mkdirStatus = mkdir(path, 0777);
    *slashPos = '/';    // restore complete path
    if (mkdirStatus == 0)
        return accessorOk;

    if (mkdirStatus == -1 && (errno == EEXIST || errno == EISDIR))
        return accessorOk;

    // parent directory seems inexistant
    if (mkdirStatus == -1 && errno == ENOENT && pathOptions & accessorPathOptionCreatePath)
    {
        // so, create parent directory...
        *slashPos = 0;      // temporary shorten path to its enclosing directory
        status = accessorPrivateCreateEnclosingDirectory(path, pathOptions);
        *slashPos = '/';    // restore complete path
        if (status != accessorOk)
            return status;

        // ...and retry
        *slashPos = 0;      // temporary shorten path to its enclosing directory
        mkdirStatus = mkdir(path, 0777);
        *slashPos = '/';    // restore complete path
        if (mkdirStatus == 0)
            return accessorOk;

        if (mkdirStatus == -1 && (errno == EEXIST || errno == EISDIR))
            return accessorOk;
    }

    return accessorHostError;
}



accessorEndianness accessorGetNativeEndianness(void)
{
    accessorPrivateInitializeEndianness();                      // required to set up accessorPrivateNativeEndianness

    return accessorPrivateNativeEndianness;
}



accessorEndianness accessorBigOrLittleEndianness(accessorEndianness e)
{
    accessorPrivateInitializeEndianness();                      // required to set up accessorPrivateNativeEndianness

    return accessorPrivateIsBigEndianness[e] ? accessorBig : accessorLittle;
}



accessorEndianness accessorNativeOrReverseEndianness(accessorEndianness e)
{
    accessorPrivateInitializeEndianness();                      // required to set up accessorPrivateNativeEndianness

    return accessorPrivateIsReverseEndianness[e] ? accessorReverse : accessorNative;
}



accessorEndianness accessorOppositeEndianness(accessorEndianness e)
{
    switch(e)
    {
    case accessorBig:     return accessorLittle;
    case accessorLittle:  return accessorBig;
    case accessorNative:  return accessorReverse;
    case accessorReverse: return accessorNative;
    }
}



accessorEndianness accessorCurrentEndianness(const accessor_t * a)
{
    return a->endianness;
}



accessorStatus accessorSetCurrentEndianness(accessor_t * a, accessorEndianness e)
{
    a->endianness = e;

    return accessorOk;
}



accessorEndianness accessorDefaultEndianness(void)
{
    return accessorPrivateDefaultEndianness;
}



accessorStatus accessorSetDefaultEndianness(accessorEndianness e)
{
    accessorPrivateDefaultEndianness = e;

    return accessorOk;
}



accessorStatus accessorSwap(accessor_t ** a1, accessor_t ** a2)
{
    accessor_t * tmp;


    if (*a1 != ACCESSOR_INIT && *a2 != ACCESSOR_INIT && (!(*a1)->writeEnabled || !(*a2)->writeEnabled))
        (*a1)->writeEnabled = (*a2)->writeEnabled = 0;

    tmp = *a1;
    *a1 = *a2;
    *a2 = tmp;

    return accessorOk;
}



accessorCoverageOption accessorIsCoverageAllowed(const accessor_t * a)
{
    return a->coverageEnabled ? accessorEnableCoverage : accessorDisableCoverage;
}




void accessorAllowCoverage(accessor_t * a, accessorCoverageOption option)
{
    a->coverageEnabled = option == accessorEnableCoverage ? 1 : 0;
}



void accessorSuspendCoverage(accessor_t * a)
{
    if (a->coverageSuspendCount < UINTMAX_MAX)
        a->coverageSuspendCount++;
}



void accessorResumeCoverage(accessor_t * a)
{
    if (a->coverageSuspendCount > 0)
        a->coverageSuspendCount--;
}



static inline void accessorPrivateOpenCoverage(accessor_t * a)
{
    if (a->coverageEnabled && a->coverageSuspendCount == 0)
        a->coverageStartOffset = a->cursor;
}



static void accessorPrivateCloseCoverage(accessor_t * a)
{
    if (a->coverageEnabled && a->coverageSuspendCount == 0)
    {
        a->coverageArraySize++;
        if (a->coverageArraySize > a->coverageArrayAllocation)
        {
            if (a->coverageArrayAllocation < 64) a->coverageArrayAllocation = 64;
            a->coverageArrayAllocation *= 2;
            a->coverageArray = realloc(a->coverageArray, a->coverageArrayAllocation * sizeof(accessorCoverageRecord));
            if (a->coverageArray == NULL)
            {
                perror("fatal: can't allocate coverage structure");
                exit(127);
            }
        }
        a->coverageArray[a->coverageArraySize - 1].offset = a->coverageStartOffset;
        a->coverageArray[a->coverageArraySize - 1].size = a->cursor - a->coverageStartOffset;
        a->coverageArray[a->coverageArraySize - 1].usage1 = a->coverageUsage1;
        a->coverageArray[a->coverageArraySize - 1].usage2 = a->coverageUsage2;
    }
}



void accessorSetCoverageUsage(accessor_t * a, uintmax_t usage1, const void * usage2)
{
    a->coverageUsage1 = usage1;
    a->coverageUsage2 = usage2;
}



void accessorAddCoverageRecord(accessor_t * a, size_t offset, size_t count, uintmax_t usage1, const void * usage2, accessorCoverageForceOption forceOption)
{
    if ((a->coverageEnabled || forceOption) && a->coverageSuspendCount == 0)
    {
        if (offset > a->windowSize)                                             // only add valid coverage records
            return;
        if (count == ACCESSOR_UNTIL_END)
            count = a->windowSize - offset;
        if (offset + count > a->windowSize)                                     // only add valid coverage records
            return;

        a->coverageArraySize++;
        if (a->coverageArraySize > a->coverageArrayAllocation)
        {
            if (a->coverageArrayAllocation < 64) a->coverageArrayAllocation = 64;
            a->coverageArrayAllocation *= 2;
            a->coverageArray = realloc(a->coverageArray, a->coverageArrayAllocation * sizeof(accessorCoverageRecord));
            if (a->coverageArray == NULL)
            {
                perror("fatal: can't allocate coverage structure");
                exit(127);
            }
        }
        a->coverageArray[a->coverageArraySize - 1].offset = offset;
        a->coverageArray[a->coverageArraySize - 1].size = count;
        a->coverageArray[a->coverageArraySize - 1].usage1 = usage1;
        a->coverageArray[a->coverageArraySize - 1].usage2 = usage2;
    }
}



const accessorCoverageRecord * accessorCoverageArray(const accessor_t * a, size_t * size)
{
    *size = a->coverageArraySize;

    return a->coverageArray;
}



// default coverage sort function: increasing offset, decreasing size, increasing user1 and user2
static int accessorPrivateCoverageCompare(const void * p1, const void * p2)
{
    accessorCoverageRecord * c1 = (accessorCoverageRecord *) p1;
    accessorCoverageRecord * c2 = (accessorCoverageRecord *) p2;

    if (c1->offset < c2->offset) return -1; // sort by increasing offset
    if (c1->offset > c2->offset) return +1;

    if (c1->size < c2->size)     return +1; // then by decreasing size
    if (c1->size > c2->size)     return -1;

    if (c1->usage1 < c2->usage1) return -1; // then by increasing usage1
    if (c1->usage1 > c2->usage1) return +1;

    if (c1->usage2 < c2->usage2) return -1; // then by increasing usage2
    if (c1->usage2 > c2->usage2) return +1;

    return 0;
}



// if merging occur, merged result is in p1
static accessorMergeResult accessorPrivateCoverageMerge(void * p1, const void * p2)
{
    accessorCoverageRecord * c1 = (accessorCoverageRecord *) p1;
    accessorCoverageRecord * c2 = (accessorCoverageRecord *) p2;
    size_t offset1, offset2;
    size_t size1, size2;
    uintmax_t usage11, usage12;
    const void * usage21, * usage22;

    offset1 = c1->offset;
    size1 = c1->size;
    usage11 = c1->usage1;
    usage21 = c1->usage2;

    offset2 = c2->offset;
    size2 = c2->size;
    usage12 = c2->usage1;
    usage22 = c2->usage2;

    if (usage11 == usage12 && usage21 == usage22 && offset1 <= offset2 && offset1 + size1 >= offset2)
    {
        if (offset2 + size2 > offset1 + size1)
            c1->size = offset2 + size2 - offset1;

        return accessorDidMerge;
    }

    return accessorDidntMerge;
}



void accessorSummarizeCoverage(accessor_t * a, int (*compare)(const void * a, const void * b), accessorMergeResult (*merge)(void * a, const void * b))
{
    size_t c1, c2;
    int (*compareFunction)(const void * a, const void * b);
    accessorMergeResult (*mergeFunction)(void * a, const void * b);


    if (a->coverageArraySize == 0)
        return;

    compareFunction = compare;
    if (compareFunction == NULL)
        compareFunction = accessorPrivateCoverageCompare;

    mergeFunction = merge;
    if (mergeFunction == NULL)
        mergeFunction = accessorPrivateCoverageMerge;

    qsort(a->coverageArray, a->coverageArraySize, sizeof(accessorCoverageRecord), compareFunction);

    if (a->coverageArraySize >= 2)
    {
        // merging from end to begin of array minimizes memmove() sizes
        c1 = a->coverageArraySize - 1;
        c2 = a->coverageArraySize;
        while (c1 > 0 && c2 > 0)
        {
            c1--;
            c2--;
            if (mergeFunction(&a->coverageArray[c1], &a->coverageArray[c2]) == accessorDidMerge)
            {
                a->coverageArraySize--;
                memmove(&a->coverageArray[c2], &a->coverageArray[c2 + 1], (a->coverageArraySize - c2) * sizeof(accessorCoverageRecord));
            }
        }
    }
}



static inline uintmax_t accessorPrivateReadUIntAtPointer(const void * ptr, accessorEndianness e, size_t nbytes)
{
    uintmax_t result;
    const uint8_t * bytes = (const uint8_t *) ptr;


    result = 0;
    if (accessorPrivateIsBigEndianness[e])
        for (size_t i = 0; i < nbytes; i++) result = (result << 8) | bytes[i];
    else
        for (size_t i = 0; i < nbytes; i++) result |= ((uintmax_t)bytes[i]) << (i * 8);

    return result;
}



static inline intmax_t accessorPrivateReadIntAtPointer(const void * ptr, accessorEndianness e, size_t nbytes)
{
    uintmax_t result;
    const uint8_t * bytes = (const uint8_t *) ptr;
    uintmax_t signBit;
    uintmax_t signExtension;


    signBit = 0x80;
    signExtension = 0xff;
    signExtension = ~signExtension;
    result = 0;
    if (accessorPrivateIsBigEndianness[e])
        for (size_t i = 0; i < nbytes; i++)
        {
            result = (result << 8) | bytes[i];
            if (i)
            {
                signBit <<= 8;
                signExtension <<= 8;
            }
        }
    else
        for (size_t i = 0; i < nbytes; i++)
        {
            result |= ((uintmax_t) bytes[i]) << (i * 8);
            if (i)
            {
                signBit <<= 8;
                signExtension <<= 8;
            }
        }

    if (result & signBit) result |= signExtension;

    return (intmax_t) result;
}



static inline void accessorPrivateWriteUIntAtPointer(void * ptr, uintmax_t x, accessorEndianness e, size_t nbytes)
{
    uint8_t * bytes = (uint8_t *) ptr;


    if (accessorPrivateIsBigEndianness[e])
    {
        for (size_t i = nbytes; i > 0; )
        {
            bytes[--i] = (uint8_t) x;
            x >>= 8;
        }
    }
    else
    {
        for (size_t i = 0; i < nbytes; i++)
        {
            bytes[i] = (uint8_t) x;
            x >>= 8;
        }
    }
}



static inline uint16_t accessorPrivateReadUInt16AtPointer(const uint8_t * ptr, accessorEndianness e)
{
    if (accessorPrivateIsBigEndianness[e])
        return (uint16_t) ((uint16_t) ptr[0] << 8 | (uint16_t) ptr[1]);
    else
        return (uint16_t) ((uint16_t) ptr[1] << 8 | (uint16_t) ptr[0]);
}



static inline uint32_t accessorPrivateReadUInt24AtPointer(const uint8_t * ptr, accessorEndianness e)
{
    if (accessorPrivateIsBigEndianness[e])
        return (uint32_t) ((uint32_t) ptr[0] << 16 | (uint32_t) ptr[1] << 8 | (uint32_t) ptr[2]);
    else
        return ((uint32_t) (uint32_t) ptr[2] << 16 | (uint32_t) ptr[1] << 8 | (uint32_t) ptr[0]);
}



static inline uint32_t accessorPrivateReadUInt32AtPointer(const uint8_t * ptr, accessorEndianness e)
{
    if (accessorPrivateIsBigEndianness[e])
        return (uint32_t) ((uint32_t) ptr[0] << 24 | (uint32_t) ptr[1] << 16 | (uint32_t) ptr[2] << 8 | (uint32_t) ptr[3]);
    else
        return (uint32_t) ((uint32_t) ptr[3] << 24 | (uint32_t) ptr[2] << 16 | (uint32_t) ptr[1] << 8 | (uint32_t) ptr[0]);
}



static inline uint64_t accessorPrivateReadUInt64AtPointer(const uint8_t * ptr, accessorEndianness e)
{
    if (accessorPrivateIsBigEndianness[e])
        return (uint64_t) ((uint64_t) ptr[0] << 56 | (uint64_t) ptr[1] << 48 | (uint64_t) ptr[2] << 40 | (uint64_t) ptr[3] << 32 | (uint64_t) ptr[4] << 24 | (uint64_t) ptr[5] << 16 | (uint64_t) ptr[6] << 8 | (uint64_t) ptr[7]);
    else
        return (uint64_t) ((uint64_t) ptr[7] << 56 | (uint64_t) ptr[6] << 48 | (uint64_t) ptr[5] << 40 | (uint64_t) ptr[4] << 32 | (uint64_t) ptr[3] << 24 | (uint64_t) ptr[2] << 16 | (uint64_t) ptr[1] << 8 | (uint64_t) ptr[0]);
}



static inline int16_t accessorPrivateReadInt16AtPointer(const uint8_t * ptr, accessorEndianness e)
{
    if (accessorPrivateIsBigEndianness[e])
        return (int16_t) ((uint16_t) ptr[0] << 8 | (uint16_t) ptr[1]);
    else
        return (int16_t) ((uint16_t) ptr[1] << 8 | (uint16_t) ptr[0]);
}



static inline int32_t accessorPrivateReadInt24AtPointer(const uint8_t * ptr, accessorEndianness e)
{
    int32_t x;


    if (accessorPrivateIsBigEndianness[e])
        x = (int32_t) ((uint32_t) ptr[0] << 16 | (uint32_t) ptr[1] << 8 | (uint32_t) ptr[2]);
    else
        x = (int32_t) ((uint32_t) ptr[2] << 16 | (uint32_t) ptr[1] << 8 | (uint32_t) ptr[0]);

    if (x & 0x800000)
        x |= 0xff000000;

    return x;
}



static inline int32_t accessorPrivateReadInt32AtPointer(const uint8_t * ptr, accessorEndianness e)
{
    if (accessorPrivateIsBigEndianness[e])
        return (int32_t) ((uint32_t) ptr[0] << 24 | (uint32_t) ptr[1] << 16 | (uint32_t) ptr[2] << 8 | (uint32_t) ptr[3]);
    else
        return (int32_t) ((uint32_t) ptr[3] << 24 | (uint32_t) ptr[2] << 16 | (uint32_t) ptr[1] << 8 | (uint32_t) ptr[0]);
}



static inline int64_t accessorPrivateReadInt64AtPointer(const uint8_t * ptr, accessorEndianness e)
{
    if (accessorPrivateIsBigEndianness[e])
        return (int64_t) ((uint64_t) ptr[0] << 56 | (uint64_t) ptr[1] << 48 | (uint64_t) ptr[2] << 40 | (uint64_t) ptr[3] << 32 | (uint64_t) ptr[4] << 24 | (uint64_t) ptr[5] << 16 | (uint64_t) ptr[6] << 8 | (uint64_t) ptr[7]);
    else
        return (int64_t) ((uint64_t) ptr[7] << 56 | (uint64_t) ptr[6] << 48 | (uint64_t) ptr[5] << 40 | (uint64_t) ptr[4] << 32 | (uint64_t) ptr[3] << 24 | (uint64_t) ptr[2] << 16 | (uint64_t) ptr[1] << 8 | (uint64_t) ptr[0]);
}



static inline void accessorPrivateWriteUInt16AtPointer(uint8_t * ptr, uint16_t x, accessorEndianness e)
{
    if (accessorPrivateIsBigEndianness[e])
        x = accessorSwapUInt16(x);

    ptr[0] = (uint8_t) x;
    ptr[1] = (uint8_t) (x >> 8);
}



static inline void accessorPrivateWriteUInt24AtPointer(uint8_t * ptr, uint32_t x, accessorEndianness e)
{
    if (accessorPrivateIsBigEndianness[e])
        x = accessorSwapUInt24(x);

    ptr[0] = (uint8_t) x;
    ptr[1] = (uint8_t) (x >> 8);
    ptr[2] = (uint8_t) (x >> 16);
}



static inline void accessorPrivateWriteUInt32AtPointer(uint8_t * ptr, uint32_t x, accessorEndianness e)
{
    if (accessorPrivateIsBigEndianness[e])
        x = accessorSwapUInt32(x);

    ptr[0] = (uint8_t) x;
    ptr[1] = (uint8_t) (x >> 8);
    ptr[2] = (uint8_t) (x >> 16);
    ptr[3] = (uint8_t) (x >> 24);
}



static inline void accessorPrivateWriteUInt64AtPointer(uint8_t * ptr, uint64_t x, accessorEndianness e)
{
    if (accessorPrivateIsBigEndianness[e])
        x = accessorSwapUInt64(x);

    ptr[0] = (uint8_t) x;
    ptr[1] = (uint8_t) (x >> 8);
    ptr[2] = (uint8_t) (x >> 16);
    ptr[3] = (uint8_t) (x >> 24);
    ptr[4] = (uint8_t) (x >> 32);
    ptr[5] = (uint8_t) (x >> 40);
    ptr[6] = (uint8_t) (x >> 48);
    ptr[7] = (uint8_t) (x >> 56);
}



accessorStatus accessorReadUInt(accessor_t * a, uintmax_t * x, size_t nbytes)
{
    return accessorReadEndianUInt(a, x, a->endianness, nbytes);
}



accessorStatus accessorReadUInt8(accessor_t * a, uint8_t * x)
{
    if (a->availableBytes < 1)
        return accessorBeyondEnd;

    *x = * (uint8_t *) (a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor);

    accessorPrivateOpenCoverage(a);

    a->cursor += 1;
    a->availableBytes -= 1;

    accessorPrivateCloseCoverage(a);

    return accessorOk;
}



accessorStatus accessorReadUInt16(accessor_t * a, uint16_t * x)
{
    if (a->availableBytes < 2)
        return accessorBeyondEnd;

    *x = accessorPrivateReadUInt16AtPointer(a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor, a->endianness);

    accessorPrivateOpenCoverage(a);

    a->cursor += 2;
    a->availableBytes -= 2;

    accessorPrivateCloseCoverage(a);

    return accessorOk;
}



accessorStatus accessorReadUInt24(accessor_t * a, uint32_t * x)
{
    if (a->availableBytes < 3)
        return accessorBeyondEnd;

    *x = accessorPrivateReadUInt24AtPointer(a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor, a->endianness);

    accessorPrivateOpenCoverage(a);

    a->cursor += 3;
    a->availableBytes -= 3;

    accessorPrivateCloseCoverage(a);

    return accessorOk;
}



accessorStatus accessorReadUInt32(accessor_t * a, uint32_t * x)
{
    if (a->availableBytes < 4)
        return accessorBeyondEnd;

    *x = accessorPrivateReadUInt32AtPointer(a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor, a->endianness);

    accessorPrivateOpenCoverage(a);

    a->cursor += 4;
    a->availableBytes -= 4;

    accessorPrivateCloseCoverage(a);

    return accessorOk;
}



accessorStatus accessorReadUInt64(accessor_t * a, uint64_t * x)
{
    if (a->availableBytes < 8)
        return accessorBeyondEnd;

    *x = accessorPrivateReadUInt64AtPointer(a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor, a->endianness);

    accessorPrivateOpenCoverage(a);

    a->cursor += 8;
    a->availableBytes -= 8;

    accessorPrivateCloseCoverage(a);

    return accessorOk;
}



accessorStatus accessorReadInt(accessor_t * a, intmax_t * x, size_t nbytes)
{
    return accessorReadEndianInt(a, x, a->endianness, nbytes);
}



accessorStatus accessorReadInt8(accessor_t * a, int8_t * x)
{
    if (a->availableBytes < 1)
        return accessorBeyondEnd;

    *x = * (int8_t *) (a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor);

    accessorPrivateOpenCoverage(a);

    a->cursor += 1;
    a->availableBytes -= 1;

    accessorPrivateCloseCoverage(a);

    return accessorOk;
}



accessorStatus accessorReadInt16(accessor_t * a, int16_t * x)
{
    if (a->availableBytes < 2)
        return accessorBeyondEnd;

    *x = accessorPrivateReadInt16AtPointer(a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor, a->endianness);

    accessorPrivateOpenCoverage(a);

    a->cursor += 2;
    a->availableBytes -= 2;

    accessorPrivateCloseCoverage(a);

    return accessorOk;
}



accessorStatus accessorReadInt24(accessor_t * a, int32_t * x)
{
    if (a->availableBytes < 3)
        return accessorBeyondEnd;

    *x = accessorPrivateReadInt24AtPointer(a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor, a->endianness);

    accessorPrivateOpenCoverage(a);

    a->cursor += 3;
    a->availableBytes -= 3;

    accessorPrivateCloseCoverage(a);

    return accessorOk;
}



accessorStatus accessorReadInt32(accessor_t * a, int32_t * x)
{
    if (a->availableBytes < 4)
        return accessorBeyondEnd;

    *x = accessorPrivateReadInt32AtPointer(a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor, a->endianness);

    accessorPrivateOpenCoverage(a);

    a->cursor += 4;
    a->availableBytes -= 4;

    accessorPrivateCloseCoverage(a);

    return accessorOk;
}



accessorStatus accessorReadInt64(accessor_t * a, int64_t * x)
{
    if (a->availableBytes < 8)
        return accessorBeyondEnd;

    *x = accessorPrivateReadInt64AtPointer(a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor, a->endianness);

    accessorPrivateOpenCoverage(a);

    a->cursor += 8;
    a->availableBytes -= 8;

    accessorPrivateCloseCoverage(a);

    return accessorOk;
}



accessorStatus accessorReadFloat32(accessor_t * a, float * x)
{
    return accessorReadUInt32(a, (uint32_t *) x);
}



accessorStatus accessorReadFloat64(accessor_t * a, double * x)
{
    return accessorReadUInt64(a, (uint64_t *) x);
}



accessorStatus accessorReadEndianUInt(accessor_t * a, uintmax_t * x, accessorEndianness e, size_t nbytes)
{
    if (nbytes > sizeof(uintmax_t))
        return accessorInvalidParameter;

    if (a->availableBytes < nbytes)
        return accessorBeyondEnd;

    *x = accessorPrivateReadUIntAtPointer(a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor, e, nbytes);

    accessorPrivateOpenCoverage(a);

    a->cursor += nbytes;
    a->availableBytes -= nbytes;

    accessorPrivateCloseCoverage(a);

    return accessorOk;
}



accessorStatus accessorReadEndianUInt16(accessor_t * a, uint16_t * x, accessorEndianness e)
{
    if (a->availableBytes < 2)
        return accessorBeyondEnd;

    *x = accessorPrivateReadUInt16AtPointer(a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor, e);

    accessorPrivateOpenCoverage(a);

    a->cursor += 2;
    a->availableBytes -= 2;

    accessorPrivateCloseCoverage(a);

    return accessorOk;
}



accessorStatus accessorReadEndianUInt24(accessor_t * a, uint32_t * x, accessorEndianness e)
{
    if (a->availableBytes < 3)
        return accessorBeyondEnd;

    *x = accessorPrivateReadUInt24AtPointer(a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor, e);

    accessorPrivateOpenCoverage(a);

    a->cursor += 3;
    a->availableBytes -= 3;

    accessorPrivateCloseCoverage(a);

    return accessorOk;
}



accessorStatus accessorReadEndianUInt32(accessor_t * a, uint32_t * x, accessorEndianness e)
{
    if (a->availableBytes < 4)
        return accessorBeyondEnd;

    *x = accessorPrivateReadUInt32AtPointer(a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor, e);

    accessorPrivateOpenCoverage(a);

    a->cursor += 4;
    a->availableBytes -= 4;

    accessorPrivateCloseCoverage(a);

    return accessorOk;
}



accessorStatus accessorReadEndianUInt64(accessor_t * a, uint64_t * x, accessorEndianness e)
{
    if (a->availableBytes < 8)
        return accessorBeyondEnd;

    *x = accessorPrivateReadUInt64AtPointer(a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor, e);

    accessorPrivateOpenCoverage(a);

    a->cursor += 8;
    a->availableBytes -= 8;

    accessorPrivateCloseCoverage(a);

    return accessorOk;
}



accessorStatus accessorReadEndianInt(accessor_t * a, intmax_t * x, accessorEndianness e, size_t nbytes)
{
    if (nbytes > sizeof(intmax_t))
        return accessorInvalidParameter;

    if (a->availableBytes < nbytes)
        return accessorBeyondEnd;

    *x = accessorPrivateReadIntAtPointer(a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor, e, nbytes);

    accessorPrivateOpenCoverage(a);

    a->cursor += nbytes;
    a->availableBytes -= nbytes;

    accessorPrivateCloseCoverage(a);

    return accessorOk;
}



accessorStatus accessorReadEndianInt16(accessor_t * a, int16_t * x, accessorEndianness e)
{
    if (a->availableBytes < 2)
        return accessorBeyondEnd;

    *x = accessorPrivateReadInt16AtPointer(a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor, e);

    accessorPrivateOpenCoverage(a);

    a->cursor += 2;
    a->availableBytes -= 2;

    accessorPrivateCloseCoverage(a);

    return accessorOk;
}



accessorStatus accessorReadEndianInt24(accessor_t * a, int32_t * x, accessorEndianness e)
{
    if (a->availableBytes < 3)
        return accessorBeyondEnd;

    *x = accessorPrivateReadInt24AtPointer(a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor, e);

    accessorPrivateOpenCoverage(a);

    a->cursor += 3;
    a->availableBytes -= 3;

    accessorPrivateCloseCoverage(a);

    return accessorOk;
}



accessorStatus accessorReadEndianInt32(accessor_t * a, int32_t * x, accessorEndianness e)
{
    if (a->availableBytes < 4)
        return accessorBeyondEnd;

    *x = accessorPrivateReadInt32AtPointer(a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor, e);

    accessorPrivateOpenCoverage(a);

    a->cursor += 4;
    a->availableBytes -= 4;

    accessorPrivateCloseCoverage(a);

    return accessorOk;
}



accessorStatus accessorReadEndianInt64(accessor_t * a, int64_t * x, accessorEndianness e)
{
    if (a->availableBytes < 8)
        return accessorBeyondEnd;

    *x = accessorPrivateReadInt64AtPointer(a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor, e);

    accessorPrivateOpenCoverage(a);

    a->cursor += 8;
    a->availableBytes -= 8;

    accessorPrivateCloseCoverage(a);

    return accessorOk;
}



accessorStatus accessorReadEndianFloat32(accessor_t * a, float * x, accessorEndianness e)
{
    return accessorReadEndianUInt32(a, (uint32_t *) x, e);
}



accessorStatus accessorReadEndianFloat64(accessor_t * a, double * x, accessorEndianness e)
{
    return accessorReadEndianUInt64(a, (uint64_t *) x, e);
}



accessorStatus accessorReadVarInt(accessor_t * a, uintmax_t * x)
{
    uint8_t byte;
    unsigned int shiftCount;
    uintmax_t result;
    size_t nbytes;
    size_t availableBytes;
    uint8_t * ptr;


    ptr = a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor;
    availableBytes = a->availableBytes;
    nbytes = 0;

    result = 0;
    shiftCount = 0;
    do
    {
        if (availableBytes < 1)
            return accessorBeyondEnd;
        byte = *ptr++;
        availableBytes--;
        nbytes++;
        result |= ((uintmax_t) (byte & 0x7f)) << shiftCount;
        shiftCount += 7;
    } while ((byte & 0x80) && (shiftCount < (sizeof(uintmax_t) * 8)));

    if (byte & 0x80)
        return accessorInvalidReadData;

    a->availableBytes -= nbytes;
    a->cursor += nbytes;

    *x = result;

    return accessorOk;
}



accessorStatus accessorReadZigZagInt(accessor_t * a, intmax_t * x)
{
    accessorStatus status;
    uintmax_t varint;


    status = accessorReadVarInt(a, &varint);
    if (status != 0)
        return status;

    *x = (varint >> 1) ^ - (varint & 1);        // varint is unsigned, right shifts are OK

    return accessorOk;
}



accessorStatus accessorWriteEndianUInt(accessor_t * a, uintmax_t x, accessorEndianness e, size_t nbytes)
{
    accessorStatus status;


    if (!a->writeEnabled)
        return accessorReadOnlyError;

    if (nbytes > sizeof(uintmax_t))
        return accessorInvalidParameter;

    if (a->availableBytes < nbytes)
    {
        status = accessorPrivateGrow(a->baseAccessor, a->cursor + nbytes);
        if (status != accessorOk)
            return status;
    }
    accessorPrivateWriteUIntAtPointer(a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor, x, e, nbytes);

    a->cursor += nbytes;
    a->availableBytes -= nbytes;

    return accessorOk;
}



accessorStatus accessorWriteEndianUInt16(accessor_t * a, uint16_t x, accessorEndianness e)
{
    accessorStatus status;


    if (!a->writeEnabled)
        return accessorReadOnlyError;

    if (a->availableBytes < 2)
    {
        status = accessorPrivateGrow(a->baseAccessor, a->cursor + 2);
        if (status != accessorOk)
            return status;
    }

    accessorPrivateWriteUInt16AtPointer(a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor, x, e);

    a->cursor += 2;
    a->availableBytes -= 2;

    return accessorOk;
}



accessorStatus accessorWriteEndianUInt24(accessor_t * a, uint32_t x, accessorEndianness e)
{
    accessorStatus status;


    if (!a->writeEnabled)
        return accessorReadOnlyError;

    if (a->availableBytes < 3)
    {
        status = accessorPrivateGrow(a->baseAccessor, a->cursor + 3);
        if (status != accessorOk)
            return status;
    }

    accessorPrivateWriteUInt24AtPointer(a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor, x, e);

    a->cursor += 3;
    a->availableBytes -= 3;

    return accessorOk;
}



accessorStatus accessorWriteEndianUInt32(accessor_t * a, uint32_t x, accessorEndianness e)
{
    accessorStatus status;


    if (!a->writeEnabled)
        return accessorReadOnlyError;

    if (a->availableBytes < 4)
    {
        status = accessorPrivateGrow(a->baseAccessor, a->cursor + 4);
        if (status != accessorOk)
            return status;
    }

    accessorPrivateWriteUInt32AtPointer(a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor, x, e);

    a->cursor += 4;
    a->availableBytes -= 4;

    return accessorOk;
}



accessorStatus accessorWriteEndianUInt64(accessor_t * a, uint64_t x, accessorEndianness e)
{
    accessorStatus status;


    if (!a->writeEnabled)
        return accessorReadOnlyError;

    if (a->availableBytes < 8)
    {
        status = accessorPrivateGrow(a->baseAccessor, a->cursor + 8);
        if (status != accessorOk)
            return status;
    }

    accessorPrivateWriteUInt64AtPointer(a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor, x, e);

    a->cursor += 8;
    a->availableBytes -= 8;

    return accessorOk;
}



accessorStatus accessorWriteEndianInt(accessor_t * a, intmax_t x, accessorEndianness e, size_t nbytes)
{
    return accessorWriteEndianUInt(a, (uintmax_t) x, e, nbytes);
}



accessorStatus accessorWriteEndianInt16(accessor_t * a, int16_t x, accessorEndianness e)
{
    accessorStatus status;


    if (!a->writeEnabled)
        return accessorReadOnlyError;

    if (a->availableBytes < 2)
    {
        status = accessorPrivateGrow(a->baseAccessor, a->cursor + 2);
        if (status != accessorOk)
            return status;
    }

    accessorPrivateWriteUInt16AtPointer(a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor, (uint16_t) x, e);

    a->cursor += 2;
    a->availableBytes -= 2;

    return accessorOk;
}



accessorStatus accessorWriteEndianInt24(accessor_t * a, int32_t x, accessorEndianness e)
{
    accessorStatus status;


    if (!a->writeEnabled)
        return accessorReadOnlyError;

    if (a->availableBytes < 3)
    {
        status = accessorPrivateGrow(a->baseAccessor, a->cursor + 3);
        if (status != accessorOk)
            return status;
    }

    accessorPrivateWriteUInt24AtPointer(a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor, (uint32_t) x, e);

    a->cursor += 3;
    a->availableBytes -= 3;

    return accessorOk;
}



accessorStatus accessorWriteEndianInt32(accessor_t * a, int32_t x, accessorEndianness e)
{
    accessorStatus status;


    if (!a->writeEnabled)
        return accessorReadOnlyError;

    if (a->availableBytes < 4)
    {
        status = accessorPrivateGrow(a->baseAccessor, a->cursor + 4);
        if (status != accessorOk)
            return status;
    }

    accessorPrivateWriteUInt32AtPointer(a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor, (uint32_t) x, e);

    a->cursor += 4;
    a->availableBytes -= 4;

    return accessorOk;
}



accessorStatus accessorWriteEndianInt64(accessor_t * a, int64_t x, accessorEndianness e)
{
    accessorStatus status;


    if (!a->writeEnabled)
        return accessorReadOnlyError;

    if (a->availableBytes < 8)
    {
        status = accessorPrivateGrow(a->baseAccessor, a->cursor + 8);
        if (status != accessorOk)
            return status;
    }

    accessorPrivateWriteUInt64AtPointer(a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor, (uint64_t) x, e);

    a->cursor += 8;
    a->availableBytes -= 8;

    return accessorOk;
}



accessorStatus accessorWriteEndianFloat32(accessor_t * a, float x, accessorEndianness e)
{
    return accessorWriteEndianUInt32(a, * (uint32_t *) &x, e);
}



accessorStatus accessorWriteEndianFloat64(accessor_t * a, double x, accessorEndianness e)
{
    return accessorWriteEndianUInt64(a, * (uint64_t *) &x, e);
}



accessorStatus accessorWriteUInt(accessor_t * a, uintmax_t x, size_t nbytes)
{
    return accessorWriteEndianUInt(a, x, a->endianness, nbytes);
}



accessorStatus accessorWriteUInt8(accessor_t * a, uint8_t x)
{
    accessorStatus status;


    if (!a->writeEnabled)
        return accessorReadOnlyError;

    if (a->availableBytes < 1)
    {
        status = accessorPrivateGrow(a->baseAccessor, a->cursor + 1);
        if (status != accessorOk)
            return status;
    }

    * (uint8_t *) (a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor) = x;

    a->cursor += 1;
    a->availableBytes -= 1;

    return accessorOk;
}



accessorStatus accessorWriteUInt16(accessor_t * a, uint16_t x)
{
    accessorStatus status;


    if (!a->writeEnabled)
        return accessorReadOnlyError;

    if (a->availableBytes < 2)
    {
        status = accessorPrivateGrow(a->baseAccessor, a->cursor + 2);
        if (status != accessorOk)
            return status;
    }

    accessorPrivateWriteUInt16AtPointer(a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor, x, a->endianness);

    a->cursor += 2;
    a->availableBytes -= 2;

    return accessorOk;
}



accessorStatus accessorWriteUInt24(accessor_t * a, uint32_t x)
{
    accessorStatus status;


    if (!a->writeEnabled)
        return accessorReadOnlyError;

    if (a->availableBytes < 3)
    {
        status = accessorPrivateGrow(a->baseAccessor, a->cursor + 3);
        if (status != accessorOk)
            return status;
    }

    accessorPrivateWriteUInt24AtPointer(a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor, x, a->endianness);

    a->cursor += 3;
    a->availableBytes -= 3;

    return accessorOk;
}



accessorStatus accessorWriteUInt32(accessor_t * a, uint32_t x)
{
    accessorStatus status;


    if (!a->writeEnabled)
        return accessorReadOnlyError;

    if (a->availableBytes < 4)
    {
        status = accessorPrivateGrow(a->baseAccessor, a->cursor + 4);
        if (status != accessorOk)
            return status;
    }

    accessorPrivateWriteUInt32AtPointer(a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor, x, a->endianness);

    a->cursor += 4;
    a->availableBytes -= 4;

    return accessorOk;
}



accessorStatus accessorWriteUInt64(accessor_t * a, uint64_t x)
{
    accessorStatus status;


    if (!a->writeEnabled)
        return accessorReadOnlyError;

    if (a->availableBytes < 8)
    {
        status = accessorPrivateGrow(a->baseAccessor, a->cursor + 8);
        if (status != accessorOk)
            return status;
    }

    accessorPrivateWriteUInt64AtPointer(a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor, x, a->endianness);

    a->cursor += 8;
    a->availableBytes -= 8;

    return accessorOk;
}



accessorStatus accessorWriteInt(accessor_t * a, intmax_t x, size_t nbytes)
{
    return accessorWriteEndianInt(a, x, a->endianness, nbytes);
}



accessorStatus accessorWriteInt8(accessor_t * a, int8_t x)
{
    accessorStatus status;


    if (!a->writeEnabled)
        return accessorReadOnlyError;

    if (a->availableBytes < 1)
    {
        status = accessorPrivateGrow(a->baseAccessor, a->cursor + 1);
        if (status != accessorOk)
            return status;
    }

    * (int8_t *) (a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor) = x;

    a->cursor += 1;
    a->availableBytes -= 1;

    return accessorOk;
}



accessorStatus accessorWriteInt16(accessor_t * a, int16_t x)
{
    accessorStatus status;


    if (!a->writeEnabled)
        return accessorReadOnlyError;

    if (a->availableBytes < 2)
    {
        status = accessorPrivateGrow(a->baseAccessor, a->cursor + 2);
        if (status != accessorOk)
            return status;
    }

    accessorPrivateWriteUInt16AtPointer(a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor, (uint16_t) x, a->endianness);

    a->cursor += 2;
    a->availableBytes -= 2;

    return accessorOk;
}



accessorStatus accessorWriteInt24(accessor_t * a, int32_t x)
{
    accessorStatus status;


    if (!a->writeEnabled)
        return accessorReadOnlyError;

    if (a->availableBytes < 3)
    {
        status = accessorPrivateGrow(a->baseAccessor, a->cursor + 3);
        if (status != accessorOk)
            return status;
    }

    accessorPrivateWriteUInt24AtPointer(a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor, (uint32_t) x, a->endianness);

    a->cursor += 3;
    a->availableBytes -= 3;

    return accessorOk;
}



accessorStatus accessorWriteInt32(accessor_t * a, int32_t x)
{
    accessorStatus status;


    if (!a->writeEnabled)
        return accessorReadOnlyError;

    if (a->availableBytes < 4)
    {
        status = accessorPrivateGrow(a->baseAccessor, a->cursor + 4);
        if (status != accessorOk)
            return status;
    }

    accessorPrivateWriteUInt32AtPointer(a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor, (uint32_t) x, a->endianness);

    a->cursor += 4;
    a->availableBytes -= 4;

    return accessorOk;
}



accessorStatus accessorWriteInt64(accessor_t * a, int64_t x)
{
    accessorStatus status;


    if (!a->writeEnabled)
        return accessorReadOnlyError;

    if (a->availableBytes < 8)
    {
        status = accessorPrivateGrow(a->baseAccessor, a->cursor + 8);
        if (status != accessorOk)
            return status;
    }

    accessorPrivateWriteUInt64AtPointer(a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor, (uint64_t) x, a->endianness);

    a->cursor += 8;
    a->availableBytes -= 8;

    return accessorOk;
}



accessorStatus accessorWriteFloat32(accessor_t * a, float x)
{
    return accessorWriteUInt32(a, * (uint32_t *) &x);
}



accessorStatus accessorWriteFloat64(accessor_t * a, double x)
{
    return accessorWriteUInt64(a, * (uint64_t *) &x);
}



accessorStatus accessorWriteVarInt(accessor_t * a, uintmax_t x)
{
    accessorStatus status;
    size_t nbytes;
    uintmax_t tmp;
    uint8_t * ptr;


    if (!a->writeEnabled)
        return accessorReadOnlyError;

    tmp = x;
    nbytes = 0;
    do
    {
        nbytes++;
        tmp >>= 7;                      // tmp is unsigned, right shifts are OK
    } while (tmp != 0);

    if (a->availableBytes < nbytes)
    {
        status = accessorPrivateGrow(a->baseAccessor, a->cursor + nbytes);
        if (status != accessorOk)
            return status;
    }

    ptr = a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor;

    a->cursor += nbytes;                // must be done before modifying nbytes
    a->availableBytes -= nbytes;        // must be done before modifying nbytes

    while (--nbytes)                    // loop will be executed nbytes-1 times
    {
        *ptr++ = (x & 0x7f) | 0x80;     // another byte follows
        x >>= 7;                        // x is unsigned, right shifts are OK
    }
    *ptr++ = x & 0x7f;                  // last byte

    return accessorOk;
}



accessorStatus accessorWriteZigZagInt(accessor_t * a, intmax_t x)
{
    if (x >= 0)                         // avoid implementation dependent negative numbers right shifts
        return accessorWriteVarInt(a, (uintmax_t) x << 1);
    else
        return accessorWriteVarInt(a, ~((uintmax_t) x << 1));
}



accessorStatus accessorReadEndianUInt16Array(accessor_t * a, uint16_t ** array, size_t count, accessorEndianness e)
{
    size_t byteCount;
    uint16_t * dst;


    byteCount = count * 2;
    if (a->availableBytes < byteCount)
        return accessorBeyondEnd;

    dst = malloc(count * sizeof(**array));
    if (dst == NULL)
        return accessorOutOfMemory;

    memcpy(dst, a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor, byteCount);

    if (accessorPrivateIsReverseEndianness[e])
        for (size_t i = 0; i < count; i++)
            dst[i] = accessorSwapUInt16(dst[i]);

    accessorPrivateOpenCoverage(a);

    a->cursor += byteCount;
    a->availableBytes -= byteCount;

    accessorPrivateCloseCoverage(a);

    * array = dst;

    return accessorOk;
}



accessorStatus accessorReadEndianUInt24Array(accessor_t * a, uint32_t ** array, size_t count, accessorEndianness e)
{
    size_t byteCount;
    uint32_t * dst;
    uint8_t * src;


    byteCount = count * 3;
    if (a->availableBytes < byteCount)
        return accessorBeyondEnd;

    dst = malloc(count * sizeof(**array));
    if (dst == NULL)
        return accessorOutOfMemory;

    src = a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor;
    for (size_t i = 0; i < count; i++)
    {
        dst[i] = accessorPrivateReadUInt24AtPointer(src, e);
        src += 3;
    }

    accessorPrivateOpenCoverage(a);

    a->cursor += byteCount;
    a->availableBytes -= byteCount;

    accessorPrivateCloseCoverage(a);

    * array = dst;

    return accessorOk;
}



accessorStatus accessorReadEndianUInt32Array(accessor_t * a, uint32_t ** array, size_t count, accessorEndianness e)
{
    size_t byteCount;
    uint32_t * dst;


    byteCount = count * 4;
    if (a->availableBytes < byteCount)
        return accessorBeyondEnd;

    dst = malloc(count * sizeof(**array));
    if (dst == NULL)
        return accessorOutOfMemory;

    memcpy(dst, a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor, byteCount);

    if (accessorPrivateIsReverseEndianness[e])
        for (size_t i = 0; i < count; i++)
            dst[i] = accessorSwapUInt32(dst[i]);

    accessorPrivateOpenCoverage(a);

    a->cursor += byteCount;
    a->availableBytes -= byteCount;

    accessorPrivateCloseCoverage(a);

    * array = dst;

    return accessorOk;
}



accessorStatus accessorReadEndianUInt64Array(accessor_t * a, uint64_t ** array, size_t count, accessorEndianness e)
{
    size_t byteCount;
    uint64_t * dst;


    byteCount = count * 8;
    if (a->availableBytes < byteCount)
        return accessorBeyondEnd;

    dst = malloc(count * sizeof(**array));
    if (dst == NULL)
        return accessorOutOfMemory;

    memcpy(dst, a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor, byteCount);

    if (accessorPrivateIsReverseEndianness[e])
        for (size_t i = 0; i < count; i++)
            dst[i] = accessorSwapUInt64(dst[i]);

    accessorPrivateOpenCoverage(a);

    a->cursor += byteCount;
    a->availableBytes -= byteCount;

    accessorPrivateCloseCoverage(a);

    * array = dst;

    return accessorOk;
}



accessorStatus accessorReadEndianInt16Array(accessor_t * a, int16_t ** array, size_t count, accessorEndianness e)
{
    size_t byteCount;
    int16_t * dst;


    byteCount = count * 2;
    if (a->availableBytes < byteCount)
        return accessorBeyondEnd;

    dst = malloc(count * sizeof(**array));
    if (dst == NULL)
        return accessorOutOfMemory;

    memcpy(dst, a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor, byteCount);

    if (accessorPrivateIsReverseEndianness[e])
        for (size_t i = 0; i < count; i++)
            dst[i] = (int16_t) accessorSwapUInt16((uint16_t) dst[i]);

    accessorPrivateOpenCoverage(a);

    a->cursor += byteCount;
    a->availableBytes -= byteCount;

    accessorPrivateCloseCoverage(a);

    * array = dst;

    return accessorOk;
}



accessorStatus accessorReadEndianInt24Array(accessor_t * a, int32_t ** array, size_t count, accessorEndianness e)
{
    size_t byteCount;
    int32_t * dst;
    uint8_t * src;


    byteCount = count * 3;
    if (a->availableBytes < byteCount)
        return accessorBeyondEnd;

    dst = malloc(count * sizeof(**array));
    if (dst == NULL)
        return accessorOutOfMemory;

    src = a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor;
    for (size_t i = 0; i < count; i++)
    {
        dst[i] = accessorPrivateReadInt24AtPointer(src, e);
        src += 3;
    }

    accessorPrivateOpenCoverage(a);

    a->cursor += byteCount;
    a->availableBytes -= byteCount;

    accessorPrivateCloseCoverage(a);

    * array = dst;

    return accessorOk;
}



accessorStatus accessorReadEndianInt32Array(accessor_t * a, int32_t ** array, size_t count, accessorEndianness e)
{
    size_t byteCount;
    int32_t * dst;


    byteCount = count * 4;
    if (a->availableBytes < byteCount)
        return accessorBeyondEnd;

    dst = malloc(count * sizeof(**array));
    if (dst == NULL)
        return accessorOutOfMemory;

    memcpy(dst, a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor, byteCount);

    if (accessorPrivateIsReverseEndianness[e])
        for (size_t i = 0; i < count; i++)
            dst[i] = (int32_t) accessorSwapUInt32((uint32_t) dst[i]);

    accessorPrivateOpenCoverage(a);

    a->cursor += byteCount;
    a->availableBytes -= byteCount;

    accessorPrivateCloseCoverage(a);

    * array = dst;

    return accessorOk;
}



accessorStatus accessorReadEndianInt64Array(accessor_t * a, int64_t ** array, size_t count, accessorEndianness e)
{
    size_t byteCount;
    int64_t * dst;


    byteCount = count * 8;
    if (a->availableBytes < byteCount)
        return accessorBeyondEnd;

    dst = malloc(count * sizeof(**array));
    if (dst == NULL)
        return accessorOutOfMemory;

    memcpy(dst, a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor, byteCount);

    if (accessorPrivateIsReverseEndianness[e])
        for (size_t i = 0; i < count; i++)
            dst[i] = (int64_t) accessorSwapUInt64((uint64_t) dst[i]);

    accessorPrivateOpenCoverage(a);

    a->cursor += byteCount;
    a->availableBytes -= byteCount;

    accessorPrivateCloseCoverage(a);

    * array = dst;

    return accessorOk;
}



accessorStatus accessorReadEndianFloat32Array(accessor_t * a, float ** array, size_t count, accessorEndianness e)
{
    return accessorReadEndianUInt32Array(a, (uint32_t **) array, count, e);
}



accessorStatus accessorReadEndianFloat64Array(accessor_t * a, double ** array, size_t count, accessorEndianness e)
{
    return accessorReadEndianUInt64Array(a, (uint64_t **) array, count, e);
}



accessorStatus accessorReadUInt16Array(accessor_t * a, uint16_t ** array, size_t count)
{
    return accessorReadEndianUInt16Array(a, array, count, a->endianness);
}



accessorStatus accessorReadUInt24Array(accessor_t * a, uint32_t ** array, size_t count)
{
    return accessorReadEndianUInt24Array(a, array, count, a->endianness);
}



accessorStatus accessorReadUInt32Array(accessor_t * a, uint32_t ** array, size_t count)
{
    return accessorReadEndianUInt32Array(a, array, count, a->endianness);
}



accessorStatus accessorReadUInt64Array(accessor_t * a, uint64_t ** array, size_t count)
{
    return accessorReadEndianUInt64Array(a, array, count, a->endianness);
}



accessorStatus accessorReadInt16Array(accessor_t * a, int16_t ** array, size_t count)
{
    return accessorReadEndianInt16Array(a, array, count, a->endianness);
}



accessorStatus accessorReadInt24Array(accessor_t * a, int32_t ** array, size_t count)
{
    return accessorReadEndianInt24Array(a, array, count, a->endianness);
}



accessorStatus accessorReadInt32Array(accessor_t * a, int32_t ** array, size_t count)
{
    return accessorReadEndianInt32Array(a, array, count, a->endianness);
}



accessorStatus accessorReadInt64Array(accessor_t * a, int64_t ** array, size_t count)
{
    return accessorReadEndianInt64Array(a, array, count, a->endianness);
}



accessorStatus accessorReadFloat32Array(accessor_t * a, float ** array, size_t count)
{
    return accessorReadEndianFloat32Array(a, array, count, a->endianness);
}



accessorStatus accessorReadFloat64Array(accessor_t * a, double ** array, size_t count)
{
    return accessorReadEndianFloat64Array(a, array, count, a->endianness);
}



accessorStatus accessorWriteEndianUInt16Array(accessor_t * a, const uint16_t * array, size_t count, accessorEndianness e)
{
    accessorStatus status;
    size_t byteCount;
    uint8_t * dst;


    if (!a->writeEnabled)
        return accessorReadOnlyError;

    byteCount = count * 2;
    if (a->availableBytes < byteCount)
    {
        status = accessorPrivateGrow(a->baseAccessor, a->cursor + byteCount);
        if (status != accessorOk)
            return status;
    }

    dst = a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor;
    memcpy(dst, array, byteCount);

    if (accessorPrivateIsReverseEndianness[e])
        for (size_t i = 0; i < count; i++)
        {
            accessorPrivateWriteUInt16AtPointer(dst, accessorPrivateReadUInt16AtPointer(dst, accessorBig), accessorLittle);
            dst += 2;
        }

    a->cursor += byteCount;
    a->availableBytes -= byteCount;

    return accessorOk;
}



accessorStatus accessorWriteEndianUInt24Array(accessor_t * a, const uint32_t * array, size_t count, accessorEndianness e)
{
    accessorStatus status;
    size_t byteCount;
    uint8_t * dst;


    if (!a->writeEnabled)
        return accessorReadOnlyError;

    byteCount = count * 3;
    if (a->availableBytes < byteCount)
    {
        status = accessorPrivateGrow(a->baseAccessor, a->cursor + byteCount);
        if (status != accessorOk)
            return status;
    }

    dst = a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor;
    for (size_t i = 0; i < count; i++)
    {
        accessorPrivateWriteUInt24AtPointer(dst, array[i], e);
        dst += 3;
    }

    a->cursor += byteCount;
    a->availableBytes -= byteCount;

    return accessorOk;
}



accessorStatus accessorWriteEndianUInt32Array(accessor_t * a, const uint32_t * array, size_t count, accessorEndianness e)
{
    accessorStatus status;
    size_t byteCount;
    uint8_t * dst;


    if (!a->writeEnabled)
        return accessorReadOnlyError;

    byteCount = count * 4;
    if (a->availableBytes < byteCount)
    {
        status = accessorPrivateGrow(a->baseAccessor, a->cursor + byteCount);
        if (status != accessorOk)
            return status;
    }

    dst = a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor;
    memcpy(dst, array, byteCount);

    if (accessorPrivateIsReverseEndianness[e])
        for (size_t i = 0; i < count; i++)
        {
            accessorPrivateWriteUInt32AtPointer(dst, accessorPrivateReadUInt32AtPointer(dst, accessorBig), accessorLittle);
            dst += 4;
        }

    a->cursor += byteCount;
    a->availableBytes -= byteCount;

    return accessorOk;
}



accessorStatus accessorWriteEndianUInt64Array(accessor_t * a, const uint64_t * array, size_t count, accessorEndianness e)
{
    accessorStatus status;
    size_t byteCount;
    uint8_t * dst;


    if (!a->writeEnabled)
        return accessorReadOnlyError;

    byteCount = count * 8;
    if (a->availableBytes < byteCount)
    {
        status = accessorPrivateGrow(a->baseAccessor, a->cursor + byteCount);
        if (status != accessorOk)
            return status;
    }

    dst = a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor;
    memcpy(dst, array, byteCount);

    if (accessorPrivateIsReverseEndianness[e])
        for (size_t i = 0; i < count; i++)
        {
            accessorPrivateWriteUInt64AtPointer(dst, accessorPrivateReadUInt64AtPointer(dst, accessorBig), accessorLittle);
            dst += 8;
        }

    a->cursor += byteCount;
    a->availableBytes -= byteCount;

    return accessorOk;
}



accessorStatus accessorWriteEndianInt16Array(accessor_t * a, const int16_t * array, size_t count, accessorEndianness e)
{
    accessorStatus status;
    size_t byteCount;
    uint8_t * dst;


    if (!a->writeEnabled)
        return accessorReadOnlyError;

    byteCount = count * 2;
    if (a->availableBytes < byteCount)
    {
        status = accessorPrivateGrow(a->baseAccessor, a->cursor + byteCount);
        if (status != accessorOk)
            return status;
    }

    dst = a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor;
    memcpy(dst, array, byteCount);

    if (accessorPrivateIsReverseEndianness[e])
        for (size_t i = 0; i < count; i++)
        {
            accessorPrivateWriteUInt16AtPointer(dst, accessorPrivateReadUInt16AtPointer(dst, accessorBig), accessorLittle);
            dst += 2;
        }

    a->cursor += byteCount;
    a->availableBytes -= byteCount;

    return accessorOk;
}



accessorStatus accessorWriteEndianInt24Array(accessor_t * a, const int32_t * array, size_t count, accessorEndianness e)
{
    accessorStatus status;
    size_t byteCount;
    uint8_t * dst;


    if (!a->writeEnabled)
        return accessorReadOnlyError;

    byteCount = count * 3;
    if (a->availableBytes < byteCount)
    {
        status = accessorPrivateGrow(a->baseAccessor, a->cursor + byteCount);
        if (status != accessorOk)
            return status;
    }

    dst = a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor;
    for (size_t i = 0; i < count; i++)
    {
        accessorPrivateWriteUInt24AtPointer(dst, (uint32_t) array[i], e);
        dst += 3;
    }

    a->cursor += byteCount;
    a->availableBytes -= byteCount;

    return accessorOk;
}



accessorStatus accessorWriteEndianInt32Array(accessor_t * a, const int32_t * array, size_t count, accessorEndianness e)
{
    accessorStatus status;
    size_t byteCount;
    uint8_t * dst;


    if (!a->writeEnabled)
        return accessorReadOnlyError;

    byteCount = count * 4;
    if (a->availableBytes < byteCount)
    {
        status = accessorPrivateGrow(a->baseAccessor, a->cursor + byteCount);
        if (status != accessorOk)
            return status;
    }

    dst = a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor;
    memcpy(dst, array, byteCount);

    if (accessorPrivateIsReverseEndianness[e])
        for (size_t i = 0; i < count; i++)
        {
            accessorPrivateWriteUInt32AtPointer(dst, accessorPrivateReadUInt32AtPointer(dst, accessorBig), accessorLittle);
            dst += 4;
        }


    a->cursor += byteCount;
    a->availableBytes -= byteCount;

    return accessorOk;
}



accessorStatus accessorWriteEndianInt64Array(accessor_t * a, const int64_t * array, size_t count, accessorEndianness e)
{
    accessorStatus status;
    size_t byteCount;
    uint8_t * dst;


    if (!a->writeEnabled)
        return accessorReadOnlyError;

    byteCount = count * 8;
    if (a->availableBytes < byteCount)
    {
        status = accessorPrivateGrow(a->baseAccessor, a->cursor + byteCount);
        if (status != accessorOk)
            return status;
    }

    dst = a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor;
    memcpy(dst, array, byteCount);

    if (accessorPrivateIsReverseEndianness[e])
        for (size_t i = 0; i < count; i++)
        {
            accessorPrivateWriteUInt64AtPointer(dst, accessorPrivateReadUInt64AtPointer(dst, accessorBig), accessorLittle);
            dst += 8;
        }


    a->cursor += byteCount;
    a->availableBytes -= byteCount;

    return accessorOk;
}



accessorStatus accessorWriteEndianFloat32Array(accessor_t * a, float * array, size_t count, accessorEndianness e)
{
    return accessorWriteEndianUInt32Array(a, (uint32_t *) array, count, e);
}



accessorStatus accessorWriteEndianFloat64Array(accessor_t * a, double * array, size_t count, accessorEndianness e)
{
    return accessorWriteEndianUInt64Array(a, (uint64_t *) array, count, e);
}



accessorStatus accessorWriteUInt16Array(accessor_t * a, const uint16_t * array, size_t count)
{
    return accessorWriteEndianUInt16Array(a, array, count, a->endianness);
}



accessorStatus accessorWriteUInt24Array(accessor_t * a, const uint32_t * array, size_t count)
{
    return accessorWriteEndianUInt24Array(a, array, count, a->endianness);
}



accessorStatus accessorWriteUInt32Array(accessor_t * a, const uint32_t * array, size_t count)
{
    return accessorWriteEndianUInt32Array(a, array, count, a->endianness);
}



accessorStatus accessorWriteUInt64Array(accessor_t * a, const uint64_t * array, size_t count)
{
    return accessorWriteEndianUInt64Array(a, array, count, a->endianness);
}



accessorStatus accessorWriteInt16Array(accessor_t * a, const int16_t * array, size_t count)
{
    return accessorWriteEndianInt16Array(a, array, count, a->endianness);
}



accessorStatus accessorWriteInt24Array(accessor_t * a, const int32_t * array, size_t count)
{
    return accessorWriteEndianInt24Array(a, array, count, a->endianness);
}



accessorStatus accessorWriteInt32Array(accessor_t * a, const int32_t * array, size_t count)
{
    return accessorWriteEndianInt32Array(a, array, count, a->endianness);
}



accessorStatus accessorWriteInt64Array(accessor_t * a, const int64_t * array, size_t count)
{
    return accessorWriteEndianInt64Array(a, array, count, a->endianness);
}



accessorStatus accessorWriteFloat32Array(accessor_t * a, float * array, size_t count)
{
    return accessorWriteEndianFloat32Array(a, array, count, a->endianness);
}



accessorStatus accessorWriteFloat64Array(accessor_t * a, double * array, size_t count)
{
    return accessorWriteEndianFloat64Array(a, array, count, a->endianness);
}



accessorStatus accessorReadEndianBytes(accessor_t * a, void * ptr, size_t count, accessorEndianness e)
{
    if (a->availableBytes < count)
        return accessorBeyondEnd;

    memcpy(ptr, a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor, count);

    if (accessorPrivateIsReverseEndianness[e])
        accessorSwapBytes(ptr, count);

    accessorPrivateOpenCoverage(a);

    a->cursor += count;
    a->availableBytes -= count;

    accessorPrivateCloseCoverage(a);

    return accessorOk;
}



accessorStatus accessorReadBytes(accessor_t * a, void * ptr, size_t count)
{
    if (a->availableBytes < count)
        return accessorBeyondEnd;

    memcpy(ptr, a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor, count);

    accessorPrivateOpenCoverage(a);

    a->cursor += count;
    a->availableBytes -= count;

    accessorPrivateCloseCoverage(a);

    return accessorOk;
}



accessorStatus accessorReadAllocatedEndianBytes(accessor_t * a, void ** ptr, size_t count, accessorEndianness e)
{
    if (a->availableBytes < count)
    {
        *ptr = NULL;
        return accessorBeyondEnd;
    }

    if ((*ptr = malloc(count)) == NULL)
        return accessorBeyondEnd;

    memcpy(*ptr, a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor, count);

    if (accessorPrivateIsReverseEndianness[e])
        accessorSwapBytes(*ptr, count);

    accessorPrivateOpenCoverage(a);

    a->cursor += count;
    a->availableBytes -= count;

    accessorPrivateCloseCoverage(a);

    return accessorOk;
}



accessorStatus accessorReadAllocatedBytes(accessor_t * a, void ** ptr, size_t count)
{
    if (a->availableBytes < count)
        return accessorBeyondEnd;

    if ((*ptr = malloc(count)) == NULL)
        return accessorBeyondEnd;

    memcpy(*ptr, a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor, count);

    accessorPrivateOpenCoverage(a);

    a->cursor += count;
    a->availableBytes -= count;

    accessorPrivateCloseCoverage(a);

    return accessorOk;
}



size_t accessorLookAheadEndianBytes(const accessor_t * a, void * ptr, size_t count, accessorEndianness e)
{
    if (a->availableBytes < count)
        count = a->availableBytes;

    memcpy(ptr, a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor, count);

    if (accessorPrivateIsReverseEndianness[e])
        accessorSwapBytes(ptr, count);

    return count;
}



size_t accessorLookAheadBytes(const accessor_t * a, void * ptr, size_t count)
{
    if (a->availableBytes < count)
        count = a->availableBytes;

    memcpy(ptr, a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor, count);

    return count;
}



accessorStatus accessorLookAheadCountBytesBeforeDelimiter(const accessor_t * a, size_t * count, size_t countLimit, size_t delLength, const void * delimiter)
{
    size_t availableBytes;
    uint8_t * ptr;
    size_t nbytes;


    if (delLength < 1)
        return accessorInvalidParameter;

    if (a->availableBytes < delLength)
        return accessorBeyondEnd;

    if (countLimit == ACCESSOR_UNTIL_END)
        countLimit = a->availableBytes - delLength;

    ptr = a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor;
    availableBytes = a->availableBytes;
    if (availableBytes > countLimit)
        availableBytes = countLimit;

    nbytes = 0;
    switch(delLength)
    {
    case 1:     // slightly optimized version for delLength == 1
        while (ptr[0] != ((uint8_t *) delimiter)[0])
        {
            if (availableBytes-- == 0)
                return accessorBeyondEnd;
            ptr++;
            nbytes++;
        }
        break;

    case 2:     // slightly optimized version for delLength == 2
        while ((ptr[0] != ((uint8_t *) delimiter)[0] || ptr[1] != ((uint8_t *) delimiter)[1]))
        {
            if (availableBytes-- == 0)
                return accessorBeyondEnd;
            ptr++;
            nbytes++;
        }
        break;

    default:
        while (memcmp(ptr, delimiter, delLength) != 0)
        {
            if (availableBytes-- == 0)
                return accessorBeyondEnd;
            ptr++;
            nbytes++;
        }
        break;
    }
    *count = nbytes;

    return accessorOk;
}



size_t accessorLookAheadAvailableBytes(const accessor_t * a, const void ** ptr)
{
    *ptr = a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor;

    return a->availableBytes;
}



accessorStatus accessorWriteEndianBytes(accessor_t * a, const void * ptr, size_t count, accessorEndianness e)
{
    accessorStatus status;
    uint8_t * dst;


    if (!a->writeEnabled)
        return accessorReadOnlyError;

    status = accessorPrivateGetPointerForWrite(&dst, a, count);
    if (status != accessorOk)
        return status;

    memcpy(dst, ptr, count);

    if (accessorPrivateIsReverseEndianness[e])
        accessorSwapBytes(dst, count);

    return accessorOk;
}



accessorStatus accessorWriteBytes(accessor_t * a, const void * ptr, size_t count)
{
    accessorStatus status;
    uint8_t * dst;


    if (!a->writeEnabled)
        return accessorReadOnlyError;

    status = accessorPrivateGetPointerForWrite(&dst, a, count);
    if (status != accessorOk)
        return status;

    memcpy(dst, ptr, count);

    return accessorOk;
}



accessorStatus accessorWriteRepeatedByte(accessor_t * a, uint8_t byte, size_t count)
{
    accessorStatus status;
    uint8_t * dst;


    if (!a->writeEnabled)
        return accessorReadOnlyError;

    status = accessorPrivateGetPointerForWrite(&dst, a, count);
    if (status != accessorOk)
        return status;

    memset(dst, byte, count);

    return accessorOk;
}



accessorStatus accessorGetPointerForBytesToRead(accessor_t * a, const void ** ptr, size_t count)
{
    if (a->availableBytes < count)
        return accessorBeyondEnd;

    *ptr = a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor;

    accessorPrivateOpenCoverage(a);

    a->cursor += count;
    a->availableBytes -= count;

    accessorPrivateCloseCoverage(a);

    return accessorOk;
}



accessorStatus accessorGetPointerForBytesToWrite(accessor_t * a, void ** ptr, size_t count)
{
    accessorStatus status;


    if (!a->writeEnabled)
        return accessorReadOnlyError;

    status = accessorPrivateGetPointerForWrite((uint8_t **) ptr, a, count);
    if (status != accessorOk)
        return status;

    return accessorOk;
}



accessorStatus accessorReadCString(accessor_t * a, char ** str, size_t * length)
{
    size_t stringLength;
    size_t availableBytes;
    uint8_t * ptr;
    char * result;


    ptr = a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor;
    availableBytes = a->availableBytes;
    stringLength = 0;

    while (availableBytes >= sizeof(**str) && ptr[stringLength])
    {
        stringLength++;
        availableBytes--;
    }
    if (a->availableBytes < (stringLength + 1) * sizeof(**str))
        return accessorBeyondEnd;

    result = malloc(stringLength + 1);
    if (result == NULL)
        return accessorOutOfMemory;

    memcpy(result, ptr, stringLength + 1);

    accessorPrivateOpenCoverage(a);

    a->cursor += stringLength + 1;
    a->availableBytes -= stringLength + 1;

    accessorPrivateCloseCoverage(a);

    *str = result;

    if (length != NULL)
        *length = stringLength;

    return accessorOk;
}



accessorStatus accessorReadPString(accessor_t * a, char ** str, size_t * length)
{
    uint8_t stringLength;
    size_t availableBytes;
    uint8_t * ptr;
    char * result;


    availableBytes = a->availableBytes;

    if (availableBytes < 1)
        return accessorBeyondEnd;

    ptr = a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor;
    stringLength = *ptr++;

    if (a->availableBytes < (stringLength + 1) * sizeof(**str))
        return accessorBeyondEnd;

    result = malloc((stringLength + 1) * sizeof(**str));
    if (result == NULL)
        return accessorOutOfMemory;

    memcpy(result, ptr, stringLength);
    result[stringLength] = 0;

    *str = result;

    accessorPrivateOpenCoverage(a);

    a->cursor += stringLength + 1;
    a->availableBytes -= stringLength + 1;

    accessorPrivateCloseCoverage(a);

    if (length != NULL)
        *length = stringLength;

    return accessorOk;
}



accessorStatus accessorReadFixedLengthString(accessor_t * a, char ** str, size_t length)
{
    char * result;


    if (a->availableBytes < length)
        return accessorBeyondEnd;

    result = malloc((length + 1) * sizeof(**str));
    if (result == NULL)
        return accessorOutOfMemory;

    memcpy(result, a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor, length);
    result[length] = 0;

    *str = result;

    accessorPrivateOpenCoverage(a);

    a->cursor += length;
    a->availableBytes -= length;

    accessorPrivateCloseCoverage(a);

    return accessorOk;
}



accessorStatus accessorReadPaddedString(accessor_t * a, char ** str, size_t * length, char pad)
{
    char * result;
    size_t stringLength;


    stringLength = *length;

    if (a->availableBytes < stringLength)
        return accessorBeyondEnd;

    result = malloc((stringLength + 1) * sizeof(**str));
    if (result == NULL)
        return accessorOutOfMemory;

    memcpy(result, a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor, stringLength);
    result[stringLength] = 0;

    accessorPrivateOpenCoverage(a);

    a->cursor += stringLength;
    a->availableBytes -= stringLength;

    accessorPrivateCloseCoverage(a);

    while (stringLength && result[stringLength - 1] == pad)
        stringLength--;

    result[stringLength] = 0;

    *str = result;

    if (length != NULL)
        *length = stringLength;

    return accessorOk;
}



accessorStatus accessorReadEndianString16(accessor_t * a, uint16_t ** str, size_t * length, accessorEndianness e)
{
    size_t stringLength;
    size_t availableBytes;
    uint8_t * ptr;
    uint16_t * result;


    ptr = a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor;
    availableBytes = a->availableBytes;
    stringLength = 0;

    while (availableBytes >= sizeof(**str) && accessorPrivateReadUInt16AtPointer(ptr, e))
    {
        availableBytes -= sizeof(**str);
        ptr += sizeof(**str);
        stringLength++;
    }
    if (a->availableBytes < (stringLength + 1) * sizeof(**str))
        return accessorBeyondEnd;

    result = malloc((stringLength + 1) * sizeof(**str));
    if (result == NULL)
        return accessorOutOfMemory;

    memcpy(result, a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor, stringLength * sizeof(**str));
    result[stringLength] = 0;

    if (accessorPrivateIsReverseEndianness[e])
    {
        for (size_t i = 0; i < stringLength; i++)
            result[i] = accessorSwapUInt16(result[i]);
    }

    *str = result;

    if (length != NULL)
        *length = stringLength;

    accessorPrivateOpenCoverage(a);

    a->cursor += (stringLength + 1) * sizeof(**str);
    a->availableBytes -= (stringLength + 1) * sizeof(**str);

    accessorPrivateCloseCoverage(a);

    return accessorOk;
}



accessorStatus accessorReadEndianString32(accessor_t * a, uint32_t ** str, size_t * length, accessorEndianness e)
{
    size_t stringLength;
    size_t availableBytes;
    uint8_t * ptr;
    uint32_t * result;


    ptr = a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor;
    availableBytes = a->availableBytes;
    stringLength = 0;

    while (availableBytes >= sizeof(**str) && accessorPrivateReadUInt32AtPointer(ptr, e))
    {
        ptr += sizeof(**str);
        stringLength++;
        availableBytes -= sizeof(**str);
    }
    if (a->availableBytes < (stringLength + 1) * sizeof(**str))
        return accessorBeyondEnd;

    result = malloc((stringLength + 1) * sizeof(**str));
    if (result == NULL)
        return accessorOutOfMemory;

    memcpy(result, a->baseAccessor->data + a->baseAccessorWindowOffset + a->cursor, stringLength * sizeof(**str));
    result[stringLength] = 0;

    if (accessorPrivateIsReverseEndianness[e])
    {
        for (size_t i = 0; i < stringLength; i++)
            result[i] = accessorSwapUInt32(result[i]);
    }

    *str = result;

    if (length != NULL)
        *length = stringLength;

    accessorPrivateOpenCoverage(a);

    a->cursor += (stringLength + 1) * sizeof(**str);
    a->availableBytes -= (stringLength + 1) * sizeof(**str);

    accessorPrivateCloseCoverage(a);

    return accessorOk;
}



accessorStatus accessorReadString16(accessor_t * a, uint16_t ** str, size_t * length)
{
    return accessorReadEndianString16(a, str, length, a->endianness);
}



accessorStatus accessorReadString32(accessor_t * a, uint32_t ** str, size_t * length)
{
    return accessorReadEndianString32(a, str, length, a->endianness);
}



accessorStatus accessorWriteCStringWithLength(accessor_t * a, const char * str, size_t length)
{
    accessorStatus status;
    uint8_t * ptr;


    if (!a->writeEnabled)
        return accessorReadOnlyError;

    status = accessorPrivateGetPointerForWrite(&ptr, a, length + 1);
    if (status != accessorOk)
        return status;

    memcpy(ptr, str, length);
    ptr[length] = 0;            // str doesn't have to be NUL terminated

    return accessorOk;
}




accessorStatus accessorWritePStringWithLength(accessor_t * a, const char * str, size_t length)
{
    accessorStatus status;
    uint8_t * ptr;


    if (!a->writeEnabled)
        return accessorReadOnlyError;

    if (length > UINT8_MAX)
        return accessorInvalidParameter;

    status = accessorPrivateGetPointerForWrite(&ptr, a, length + 1);
    if (status != accessorOk)
        return status;

    *ptr++ = (uint8_t) length;
    memcpy(ptr, str, length);

    return accessorOk;
}




accessorStatus accessorWritePaddedStringWithLength(accessor_t * a, const char * str, size_t length, size_t paddedLength, char pad)
{
    accessorStatus status;
    uint8_t * ptr;


    if (!a->writeEnabled)
        return accessorReadOnlyError;

    if (length > paddedLength)
        return accessorInvalidParameter;

    status = accessorPrivateGetPointerForWrite(&ptr, a, paddedLength);
    if (status != accessorOk)
        return status;

    memcpy(ptr, str, length);
    if (length < paddedLength)
        memset(ptr + length, pad, paddedLength - length);

    return accessorOk;
}




accessorStatus accessorWriteEndianString16WithLength(accessor_t * a, const uint16_t * str, size_t length, accessorEndianness e)
{
    accessorStatus status;
    uint8_t * ptr;


    if (!a->writeEnabled)
        return accessorReadOnlyError;

    status = accessorPrivateGetPointerForWrite(&ptr, a, (length + 1) * 2);
    if (status != accessorOk)
        return status;

    memcpy(ptr, str, (length + 1) * 2);
    if (accessorPrivateIsReverseEndianness[e])
    {
        for (size_t i = 0; i < length; i++)
        {
            accessorPrivateWriteUInt16AtPointer(ptr, accessorPrivateReadUInt16AtPointer(ptr, accessorBig), accessorLittle);
            ptr += 2;
        }
    }

    return accessorOk;
}




accessorStatus accessorWriteEndianString32WithLength(accessor_t * a, const uint32_t * str, size_t length, accessorEndianness e)
{
    accessorStatus status;
    uint8_t * ptr;


    if (!a->writeEnabled)
        return accessorReadOnlyError;

    status = accessorPrivateGetPointerForWrite(&ptr, a, (length + 1) * 4);
    if (status != accessorOk)
        return status;

    memcpy(ptr, str, (length + 1) * 4);
    if (accessorPrivateIsReverseEndianness[e])
    {
        for (size_t i = 0; i < length; i++)
        {
            accessorPrivateWriteUInt32AtPointer(ptr, accessorPrivateReadUInt32AtPointer(ptr, accessorBig), accessorLittle);
            ptr += 4;
        }
    }

    return accessorOk;
}




accessorStatus accessorWriteString16WithLength(accessor_t * a, const uint16_t * str, size_t length)
{
    return accessorWriteEndianString16WithLength(a, str, length, a->endianness);
}




accessorStatus accessorWriteString32WithLength(accessor_t * a, const uint32_t * str, size_t length)
{
    return accessorWriteEndianString32WithLength(a, str, length, a->endianness);
}





accessorStatus accessorWriteCString(accessor_t * a, const char * str)
{
    return accessorWriteCStringWithLength(a, str, strlen(str));
}




accessorStatus accessorWritePString(accessor_t * a, const char * str)
{
    return accessorWritePStringWithLength(a, str, strlen(str));
}




accessorStatus accessorWritePaddedString(accessor_t * a, const char * str, size_t paddedLength, char pad)
{
    return accessorWritePaddedStringWithLength(a, str, strlen(str), paddedLength, pad);
}




accessorStatus accessorWriteEndianString16(accessor_t * a, const uint16_t * str, accessorEndianness e)
{
    size_t stringLength;


    stringLength = 0;
    while (str[stringLength])
        stringLength++;

    return accessorWriteEndianString16WithLength(a, str, stringLength, e);
}




accessorStatus accessorWriteEndianString32(accessor_t * a, const uint32_t * str, accessorEndianness e)
{
    size_t stringLength;


    stringLength = 0;
    while (str[stringLength])
        stringLength++;

    return accessorWriteEndianString32WithLength(a, str, stringLength, e);
}




accessorStatus accessorWriteString16(accessor_t * a, const uint16_t * str)
{
    return accessorWriteEndianString16(a, str, a->endianness);
}




accessorStatus accessorWriteString32(accessor_t * a, const uint32_t * str)
{
    return accessorWriteEndianString32(a, str, a->endianness);
}



// on mac, most accessorSwapUInt* functions are inspired by, if not copied from, <libkern/i386/_OSByteOrder.h>
void accessorSwapBytes(void * ptr, size_t nbytes)
{
    uint8_t * x;
    uint8_t * y;
    uint8_t tmp;


    x = (uint8_t *) ptr;
    y = (uint8_t *) ptr + nbytes - 1;
    for (size_t i = 0; i < nbytes / 2; i++)
    {
        tmp = *x;
        *x++ = *y;
        *y-- = tmp;
    }
}



uintmax_t accessorSwapUInt(uintmax_t x, size_t nbytes)
{
    uint8_t tmp[sizeof(uintmax_t)];
#if !defined(__llvm__) && !defined(__x86_64__) && !defined(__i386__)
    union
    {
        uint64_t _u64;
        uint32_t _u32[2];
    } _u;
#endif


    switch(nbytes)
    {
    case 0:
        return 0;

    case 1:
        return x & 0xff;

    case 2:
        return ((uint16_t)((x << 8) | ((x & 0xff00) >> 8)));

    case 3:
        return ((x & 0xff) << 16) | (x & 0xff00) | ((x & 0xff0000) >> 16);

    case 4:
#if defined(__llvm__)
        return __builtin_bswap32((uint32_t) x);
#elif defined(__x86_64__) || defined(__i386__)
        __asm__ ("bswap      %0" : "+r" (x));
        return x;
#else
        // This actually generates the best code
        return (((x ^ (x >> 16 | (x << 16))) & 0xff00ffff) >> 8) ^ (x >> 8 | x << 24);
#endif

    case 8:
#if defined(__llvm__)
        return __builtin_bswap64(x);
#else
#if defined(__x86_64__)
        __asm__ ("bswap      %0" : "+r" (x));
        return _data;
#elif defined(__i386__)
        __asm__ ("bswap      %%eax\n\t"
                 "bswap      %%edx\n\t"
                 "xchgl      %%eax, %%edx"
                 : "+A" (x));
        return x;
#else
        union
        {
            uint64_t _u64;
            uint32_t _u32[2];
        } _u;

        // This actually generates the best code
        _u._u32[0] = (uint32_t)(x >> 32);
        _u._u32[1] = (uint32_t)(x & 0xffffffff);
        _u._u32[0] = accessorSwapUInt32(_u._u32[0]);
        _u._u32[1] = accessorSwapUInt32(_u._u32[1]);

        return _u._u64;
#endif
#endif

    default:
        if (nbytes > sizeof(uintmax_t))
            return accessorInvalidParameter;

        accessorPrivateWriteUIntAtPointer(&tmp, x, accessorLittle, nbytes);
        return accessorPrivateReadUIntAtPointer(&tmp, accessorBig, nbytes);
    }
}



intmax_t accessorSwapInt(intmax_t x, size_t nbytes)
{
    uint8_t tmp[sizeof(uintmax_t)];
    int32_t tmp32;
#if !defined(__llvm__) && !defined(__x86_64__) && !defined(__i386__)
    union
    {
        uint64_t _u64;
        uint32_t _u32[2];
    } _u;
#endif


    switch(nbytes)
    {
    case 0:
        return 0;

    case 1:
        return x;

    case 2:
        return (intmax_t) ((int16_t)((((uint16_t) x) << 8) | ((x & 0xff00) >> 8)));

    case 3:
        tmp32 = (int32_t) ((x & 0xff) << 16) | (x & 0xff00) | ((x & 0xff0000) >> 16);
        if (tmp32 & 0x800000)
            return tmp32 | (int32_t) 0xff000000;
        else
            return tmp32;

    case 4:
#if defined(__llvm__)
        return (int32_t) __builtin_bswap32((uint32_t) x);
#elif defined(__x86_64__) || defined(__i386__)
        __asm__ ("bswap      %0" : "+r" (x));
        return x;
#else
        // This actually generates the best code
        return (((x ^ (x >> 16 | (x << 16))) & 0xff00ffff) >> 8) ^ (x >> 8 | x << 24);
#endif

    case 8:
#if defined(__llvm__)
        return (int64_t) __builtin_bswap64((uint64_t) x);
#else
#if defined(__x86_64__)
        __asm__ ("bswap      %0" : "+r" (x));
        return _data;
#elif defined(__i386__)
        __asm__ ("bswap      %%eax\n\t"
                 "bswap      %%edx\n\t"
                 "xchgl      %%eax, %%edx"
                 : "+A" (x));
        return x;
#else
        union
        {
            uint64_t _u64;
            uint32_t _u32[2];
        } _u;

        // This actually generates the best code
        _u._u32[0] = (uint32_t)(x >> 32);
        _u._u32[1] = (uint32_t)(x & 0xffffffff);
        _u._u32[0] = accessorSwapUInt32(_u._u32[0]);
        _u._u32[1] = accessorSwapUInt32(_u._u32[1]);

        return _u._u64;
#endif
#endif

    default:
        if (nbytes > sizeof(uintmax_t))
            return accessorInvalidParameter;

        accessorPrivateWriteUIntAtPointer(&tmp, (uintmax_t) x, accessorLittle, nbytes);
        return accessorPrivateReadIntAtPointer(&tmp, accessorBig, nbytes);
    }
}



uint16_t accessorSwapUInt16(uint16_t x)
{
    return ((uint16_t)((x << 8) | (x >> 8)));
}



uint32_t accessorSwapUInt24(uint32_t x)
{
    return ((x & 0xff) << 16) | (x & 0xff00) | ((x & 0xff0000) >> 16);
}



int32_t accessorSwapInt24(int32_t x)
{
    int32_t tmp;


    tmp = ((x & 0xff) << 16) | (x & 0xff00) | ((x & 0xff0000) >> 16);
    if (tmp & 0x800000)
        return tmp | (int32_t) 0xff000000;
    else
        return tmp;
}



uint32_t accessorSwapUInt32(uint32_t x)
{
#if defined(__llvm__)
    return __builtin_bswap32(x);
#elif defined(__x86_64__) || defined(__i386__)
    __asm__ ("bswap      %0" : "+r" (x));
    return x;
#else
    // This actually generates the best code
    return (((x ^ (x >> 16 | (x << 16))) & 0xff00ffff) >> 8) ^ (x >> 8 | x << 24);
#endif
}



uint64_t accessorSwapUInt64(uint64_t x)
{
#if defined(__llvm__)
    return __builtin_bswap64(x);
#else
#if defined(__x86_64__)
    __asm__ ("bswap      %0" : "+r" (x));
    return _data;
#elif defined(__i386__)
    __asm__ ("bswap      %%eax\n\t"
             "bswap      %%edx\n\t"
             "xchgl      %%eax, %%edx"
             : "+A" (x));
    return x;
#else
    union
    {
        uint64_t _u64;
        uint32_t _u32[2];
    } _u;

    // This actually generates the best code
    _u._u32[0] = (uint32_t)(x >> 32);
    _u._u32[1] = (uint32_t)(x & 0xffffffff);
    _u._u32[0] = accessorSwapUInt32(_u._u32[0]);
    _u._u32[1] = accessorSwapUInt32(_u._u32[1]);

    return _u._u64;
#endif
#endif
}



