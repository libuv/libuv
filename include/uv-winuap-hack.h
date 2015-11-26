
// Declarations from Windows headers

#define FORMAT_MESSAGE_ALLOCATE_BUFFER          0x00000100

#define HasOverlappedIoCompleted(lpOverlapped)  (((DWORD)(lpOverlapped)->Internal) != STATUS_PENDING)

#define IF_TYPE_SOFTWARE_LOOPBACK               24

#define LOAD_WITH_ALTERED_SEARCH_PATH           0x00000008

// maybe align on 4 bytes?
typedef struct _MEMORYSTATUSEX {
    DWORD dwLength;
    DWORD dwMemoryLoad;
    DWORDLONG ullTotalPhys;
    DWORDLONG ullAvailPhys;
    DWORDLONG ullTotalPageFile;
    DWORDLONG ullAvailPageFile;
    DWORDLONG ullTotalVirtual;
    DWORDLONG ullAvailVirtual;
    DWORDLONG ullAvailExtendedVirtual;
} MEMORYSTATUSEX, *LPMEMORYSTATUSEX;

// Functions from KernelBase.dll
typedef HMODULE (WINAPI* fptr_GetModuleHandleA)(LPCSTR lpModuleName);
typedef HMODULE (WINAPI* fptr_LoadLibraryExW)(LPCWSTR lpLibFileName,HANDLE hFile,DWORD dwFlags);
typedef BOOL (WINAPI* fptr_GlobalMemoryStatusEx)(LPMEMORYSTATUSEX lpBuffer);
typedef HLOCAL (WINAPI* fptr_LocalFree)(HLOCAL hMem);

typedef HANDLE (WINAPI* fptr_CreateIoCompletionPort)(HANDLE FileHandle, HANDLE ExistingCompletionPort, ULONG_PTR CompletionKey, DWORD NumberOfConcurrentThreads);
typedef BOOL (WINAPI* fptr_GetQueuedCompletionStatus)(HANDLE CompletionPort, LPDWORD lpNumberOfBytesTransferred, PULONG_PTR lpCompletionKey, LPOVERLAPPED * lpOverlapped, DWORD dwMilliseconds);
typedef BOOL (WINAPI* fptr_PostQueuedCompletionStatus)(HANDLE CompletionPort, DWORD dwNumberOfBytesTransferred, ULONG_PTR dwCompletionKey, LPOVERLAPPED lpOverlapped);
typedef BOOL (WINAPI* fptr_CancelIo)(HANDLE hFile);

typedef BOOL (WINAPI* fptr_QueueUserWorkItem)(LPTHREAD_START_ROUTINE Function, PVOID Context, ULONG Flags);

typedef BOOL (WINAPI* fptr_RegisterWaitForSingleObjectEx)(PHANDLE phNewWaitObject, HANDLE hObject, WAITORTIMERCALLBACK Callback, PVOID Context, ULONG dwMilliseconds, ULONG dwFlags);
typedef BOOL (WINAPI* fptr_UnregisterWaitEx)(HANDLE WaitHandle, HANDLE CompletionEvent);

extern fptr_GetModuleHandleA GetModuleHandleA;
extern fptr_LoadLibraryExW LoadLibraryExW;
extern fptr_GlobalMemoryStatusEx GlobalMemoryStatusEx;
extern fptr_LocalFree LocalFree;

extern fptr_CreateIoCompletionPort CreateIoCompletionPort;
extern fptr_GetQueuedCompletionStatus GetQueuedCompletionStatus;
extern fptr_PostQueuedCompletionStatus PostQueuedCompletionStatus;
extern fptr_CancelIo CancelIo;

extern fptr_QueueUserWorkItem QueueUserWorkItem;

extern fptr_UnregisterWaitEx UnregisterWaitEx;
extern fptr_RegisterWaitForSingleObjectEx RegisterWaitForSingleObjectEx;
