/* The production implementation is included immediately before this runtime. */
typedef struct { int Count; } MOCK_EVENT;
struct MOCK_TIMER {
    MOCK_TIMER_CALLBACK Callback;
    PVOID Parameter;
    pthread_t Thread;
    int Armed;
    int HasThread;
};
typedef struct {
    MOCK_WORK_CALLBACK Callback;
    PVOID Parameter;
} MOCK_WORK;
typedef struct { HANDLE Handle; INT Result; } CANCEL_JOB;

static pthread_mutex_t MockLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t MockCondition = PTHREAD_COND_INITIALIZER;
static pthread_t Workers[8];
static int WorkerCount;
static int HoldWorkers;
static int ReleaseResolver;
static int ResolverCalls;
static int FastResolver;
static int FailHeap;
static int FailTimer;
static int FailSubmit;
static int GateOpen;
static int GateWaiters;
static int GateTimer;
static LONG HeapCount;
static LONG ResultCount;
static LONG TimerCount;
static PTP_TIMER CurrentTimer;
static __thread ULONG LastError;
static MOCK_EVENT CallbackEvent;
static PPADDRINFOEXW CallbackResultSlot;
static PHANDLE CallbackHandleSlot;
static LPOVERLAPPED CallbackOverlapped;
static int FreeCallerInCallback;
static int ReenterCallback;
static ULONG CallbackStatus;
static INT CallbackCancelStatus;

static void LockMock(void) { assert(pthread_mutex_lock(&MockLock) == 0); }
static void UnlockMock(void) { assert(pthread_mutex_unlock(&MockLock) == 0); }
static void NotifyMock(void) { assert(pthread_cond_broadcast(&MockCondition) == 0); }
static void WaitMock(void)
{
    struct timespec Deadline;
    int Status;
    assert(clock_gettime(CLOCK_REALTIME, &Deadline) == 0);
    Deadline.tv_sec += 10;
    Status = pthread_cond_timedwait(&MockCondition, &MockLock, &Deadline);
    assert(Status == 0);
}
static void SetLastError(ULONG Error) { LastError = Error; }
static PVOID GetProcessHeap(void) { return (PVOID) (uintptr_t) 1; }
static PVOID HeapAlloc(PVOID Heap, ULONG Flags, size_t Size)
{
    PVOID Memory;
    (void) Heap;
    (void) Flags;
    if (FailHeap) return NULL;
    Memory = malloc(Size);
    assert(Memory != NULL);
    InterlockedIncrement(&HeapCount);
    return Memory;
}
static BOOL HeapFree(PVOID Heap, ULONG Flags, PVOID Memory)
{
    (void) Heap;
    (void) Flags;
    assert(Memory != NULL);
    InterlockedDecrement(&HeapCount);
    free(Memory);
    return TRUE;
}
static BOOL SetEvent(HANDLE Handle)
{
    MOCK_EVENT *Event;
    Event = (MOCK_EVENT *) Handle;
    assert(Event != NULL);
    LockMock();
    ++Event->Count;
    NotifyMock();
    UnlockMock();
    return TRUE;
}
static void WaitEvent(MOCK_EVENT *Event)
{
    LockMock();
    while (!Event->Count) WaitMock();
    UnlockMock();
}
static HRESULT StringCchCopy(WCHAR *Destination, size_t Count, PCWSTR Source)
{
    size_t Length;
    Length = wcslen(Source);
    if (Length >= Count) return -1;
    memcpy(Destination, Source, (Length + 1) * sizeof(WCHAR));
    return 0;
}
static void *RunWork(void *Parameter)
{
    MOCK_WORK Work;
    Work = *(MOCK_WORK *) Parameter;
    free(Parameter);
    LockMock();
    while (HoldWorkers) WaitMock();
    UnlockMock();
    Work.Callback(NULL, Work.Parameter);
    return NULL;
}
static BOOL TrySubmitThreadpoolCallback(MOCK_WORK_CALLBACK Callback, PVOID Parameter,
    TP_CALLBACK_ENVIRON *Environment)
{
    MOCK_WORK *Work;
    (void) Environment;
    if (FailSubmit) return FALSE;
    Work = (MOCK_WORK *) malloc(sizeof(*Work));
    assert(Work != NULL);
    Work->Callback = Callback;
    Work->Parameter = Parameter;
    LockMock();
    assert(WorkerCount < (int) ARRAYSIZE(Workers));
    assert(pthread_create(&Workers[WorkerCount], NULL, RunWork, Work) == 0);
    ++WorkerCount;
    UnlockMock();
    return TRUE;
}
static PTP_TIMER CreateThreadpoolTimer(MOCK_TIMER_CALLBACK Callback, PVOID Parameter,
    TP_CALLBACK_ENVIRON *Environment)
{
    PTP_TIMER Timer;
    (void) Environment;
    if (FailTimer) return NULL;
    Timer = (PTP_TIMER) calloc(1, sizeof(*Timer));
    assert(Timer != NULL);
    Timer->Callback = Callback;
    Timer->Parameter = Parameter;
    LockMock();
    CurrentTimer = Timer;
    UnlockMock();
    InterlockedIncrement(&TimerCount);
    return Timer;
}
static void SetThreadpoolTimer(PTP_TIMER Timer, PFILETIME DueTime, ULONG Period, ULONG Window)
{
    LONGLONG Due;
    (void) Period;
    (void) Window;
    LockMock();
    if (DueTime) {
        memcpy(&Due, DueTime, sizeof(Due));
        assert(Due < 0);
        Timer->Armed = TRUE;
    } else {
        Timer->Armed = FALSE;
    }
    NotifyMock();
    UnlockMock();
}
static void *RunTimer(void *Parameter)
{
    PTP_TIMER Timer;
    int Fire;
    Timer = (PTP_TIMER) Parameter;
    LockMock();
    if (GateTimer) {
        ++GateWaiters;
        NotifyMock();
        while (!GateOpen) WaitMock();
    }
    Fire = Timer->Armed;
    UnlockMock();
    if (Fire) Timer->Callback(NULL, Timer->Parameter, Timer);
    return NULL;
}
static void FireTimer(void)
{
    PTP_TIMER Timer;
    LockMock();
    Timer = CurrentTimer;
    assert(Timer != NULL && Timer->Armed && !Timer->HasThread);
    Timer->HasThread = TRUE;
    assert(pthread_create(&Timer->Thread, NULL, RunTimer, Timer) == 0);
    UnlockMock();
}
static void WaitForThreadpoolTimerCallbacks(PTP_TIMER Timer, BOOL CancelPending)
{
    int HasThread;
    (void) CancelPending;
    LockMock();
    HasThread = Timer->HasThread;
    UnlockMock();
    if (HasThread) assert(pthread_join(Timer->Thread, NULL) == 0);
}
static void CloseThreadpoolTimer(PTP_TIMER Timer)
{
    LockMock();
    assert(!Timer->Armed);
    if (CurrentTimer == Timer) CurrentTimer = NULL;
    UnlockMock();
    InterlockedDecrement(&TimerCount);
    free(Timer);
}
static INT GetAddrInfoExW(PCWSTR Name, PCWSTR ServiceName, ULONG Namespace, LPGUID Guid,
    PCADDRINFOEXW Hints, PPADDRINFOEXW Result, PTIMEVAL Timeout, LPOVERLAPPED Overlapped,
    LPLOOKUPSERVICE_COMPLETION_ROUTINE Completion, PHANDLE Cancel)
{
    (void) ServiceName;
    (void) Namespace;
    (void) Guid;
    (void) Hints;
    (void) Timeout;
    (void) Overlapped;
    (void) Completion;
    (void) Cancel;
    assert(Name != NULL && wcscmp(Name, L"test.invalid") == 0);
    LockMock();
    ++ResolverCalls;
    NotifyMock();
    while (!ReleaseResolver && !FastResolver) WaitMock();
    UnlockMock();
    *Result = (PADDRINFOEXW) calloc(1, sizeof(**Result));
    assert(*Result != NULL);
    InterlockedIncrement(&ResultCount);
    return NO_ERROR;
}
static void FreeAddrInfoExW(PADDRINFOEXW Result)
{
    if (Result) {
        InterlockedDecrement(&ResultCount);
        free(Result);
    }
}
static void WaitResolver(void)
{
    LockMock();
    while (!ResolverCalls) WaitMock();
    UnlockMock();
}
static void ReleaseAll(void)
{
    LockMock();
    HoldWorkers = FALSE;
    ReleaseResolver = TRUE;
    GateOpen = TRUE;
    NotifyMock();
    UnlockMock();
}
static void JoinWorkers(void)
{
    int Index;
    for (Index = 0; Index < WorkerCount; ++Index) {
        assert(pthread_join(Workers[Index], NULL) == 0);
    }
    WorkerCount = 0;
}
static void ResetMock(void)
{
    assert(WorkerCount == 0);
    assert(HeapCount == 0);
    assert(ResultCount == 0);
    assert(TimerCount == 0);
    assert(GaiHandleList == NULL);
    HoldWorkers = ReleaseResolver = ResolverCalls = FastResolver = 0;
    FailHeap = FailTimer = FailSubmit = GateOpen = GateWaiters = GateTimer = 0;
    CurrentTimer = NULL;
    CallbackEvent.Count = 0;
    CallbackResultSlot = NULL;
    CallbackHandleSlot = NULL;
    CallbackOverlapped = NULL;
    FreeCallerInCallback = ReenterCallback = 0;
    CallbackStatus = 0;
    CallbackCancelStatus = -1;
}
static void InitOverlapped(OVERLAPPED *Overlapped, MOCK_EVENT *Event)
{
    memset(Overlapped, 0, sizeof(*Overlapped));
    Event->Count = 0;
    Overlapped->hEvent = Event;
}
static INT Start(OVERLAPPED *Overlapped, PPADDRINFOEXW Result, PHANDLE Handle, int Timeout)
{
    TIMEVAL Duration;
    Duration.tv_sec = 1;
    Duration.tv_usec = 0;
    return Ext_GetAddrInfoExW(L"test.invalid", NULL, 0, NULL, NULL, Result,
        Timeout ? &Duration : NULL, Overlapped, NULL, Handle);
}
static void CheckInvalidHandles(void)
{
    HANDLE Handle;
    assert(GetAddrInfoExCancel(NULL) == WSA_INVALID_HANDLE);
    Handle = NULL;
    assert(GetAddrInfoExCancel(&Handle) == WSA_INVALID_HANDLE);
    Handle = (HANDLE) (uintptr_t) 0xdeadbeef;
    assert(GetAddrInfoExCancel(&Handle) == WSA_INVALID_HANDLE && Handle == NULL);
    Handle = INVALID_HANDLE_VALUE;
    assert(GetAddrInfoExCancel(&Handle) == WSA_INVALID_HANDLE);
}
static void TestNormalCompletion(void)
{
    OVERLAPPED Overlapped;
    MOCK_EVENT Event;
    PADDRINFOEXW Result;
    HANDLE Handle;
    HANDLE OldHandle;
    ResetMock();
    InitOverlapped(&Overlapped, &Event);
    Result = NULL;
    Handle = NULL;
    assert(Start(&Overlapped, &Result, &Handle, TRUE) == WSA_IO_PENDING);
    OldHandle = Handle;
    WaitResolver();
    CheckInvalidHandles();
    ReleaseAll();
    WaitEvent(&Event);
    JoinWorkers();
    assert(Event.Count == 1 && Overlapped.Internal == NO_ERROR && Result != NULL);
    assert(GetAddrInfoExOverlappedResult(&Overlapped) == NO_ERROR);
    FreeAddrInfoExW(Result);
    assert(GetAddrInfoExCancel(&Handle) == WSA_INVALID_HANDLE);
    assert(GetAddrInfoExCancel(&OldHandle) == WSA_INVALID_HANDLE);
}
static void TestCancelBeforeWorker(void)
{
    OVERLAPPED Overlapped;
    MOCK_EVENT Event;
    PADDRINFOEXW Result;
    HANDLE Handle;
    HANDLE Copy;
    ResetMock();
    HoldWorkers = TRUE;
    InitOverlapped(&Overlapped, &Event);
    Result = NULL;
    assert(Start(&Overlapped, &Result, &Handle, TRUE) == WSA_IO_PENDING);
    Copy = Handle;
    assert(GetAddrInfoExCancel(&Handle) == NO_ERROR);
    assert(Handle == NULL && Event.Count == 1 && Result == NULL);
    assert(Overlapped.Internal == WSA_E_CANCELLED);
    assert(GetAddrInfoExCancel(&Copy) == WSA_INVALID_HANDLE);
    ReleaseAll();
    JoinWorkers();
    assert(ResolverCalls == 0 && Event.Count == 1);
}
static void TestCancelDuringLookup(void)
{
    OVERLAPPED Overlapped;
    MOCK_EVENT Event;
    PADDRINFOEXW Result;
    HANDLE Handle;
    HANDLE Stale;
    HANDLE NewHandle;
    ResetMock();
    InitOverlapped(&Overlapped, &Event);
    Result = NULL;
    assert(Start(&Overlapped, &Result, &Handle, FALSE) == WSA_IO_PENDING);
    Stale = Handle;
    WaitResolver();
    assert(GetAddrInfoExCancel(&Handle) == NO_ERROR);
    assert(Overlapped.Internal == WSA_E_CANCELLED && Result == NULL);
    ReleaseAll();
    JoinWorkers();
    assert(Event.Count == 1 && ResultCount == 0);
    ResetMock();
    InitOverlapped(&Overlapped, &Event);
    assert(Start(&Overlapped, &Result, &NewHandle, FALSE) == WSA_IO_PENDING);
    WaitResolver();
    assert(NewHandle != Stale);
    assert(GetAddrInfoExCancel(&Stale) == WSA_INVALID_HANDLE);
    assert(Event.Count == 0);
    assert(GetAddrInfoExCancel(&NewHandle) == NO_ERROR);
    ReleaseAll();
    JoinWorkers();
    assert(Event.Count == 1);
}
static void TestTimeout(void)
{
    OVERLAPPED Overlapped;
    MOCK_EVENT Event;
    PADDRINFOEXW Result;
    HANDLE Handle;
    ResetMock();
    InitOverlapped(&Overlapped, &Event);
    Result = NULL;
    assert(Start(&Overlapped, &Result, &Handle, TRUE) == WSA_IO_PENDING);
    WaitResolver();
    FireTimer();
    WaitEvent(&Event);
    assert(Overlapped.Internal == WSAETIMEDOUT && Result == NULL);
    assert(GetAddrInfoExCancel(&Handle) == WSA_INVALID_HANDLE);
    ReleaseAll();
    JoinWorkers();
    assert(Event.Count == 1 && ResultCount == 0);
}
static void *RaceCancel(void *Parameter)
{
    CANCEL_JOB *Job;
    Job = (CANCEL_JOB *) Parameter;
    LockMock();
    ++GateWaiters;
    NotifyMock();
    while (!GateOpen) WaitMock();
    UnlockMock();
    Job->Result = GetAddrInfoExCancel(&Job->Handle);
    return NULL;
}
static void TestCompletionRaces(int Iterations)
{
    int Index;
    for (Index = 0; Index < Iterations; ++Index) {
        OVERLAPPED Overlapped;
        MOCK_EVENT Event;
        PADDRINFOEXW Result;
        HANDLE Handle;
        CANCEL_JOB Job;
        pthread_t Canceller;
        ResetMock();
        GateTimer = TRUE;
        InitOverlapped(&Overlapped, &Event);
        Result = NULL;
        assert(Start(&Overlapped, &Result, &Handle, TRUE) == WSA_IO_PENDING);
        WaitResolver();
        Job.Handle = Handle;
        Job.Result = -1;
        FireTimer();
        assert(pthread_create(&Canceller, NULL, RaceCancel, &Job) == 0);
        LockMock();
        while (GateWaiters != 2) WaitMock();
        UnlockMock();
        ReleaseAll();
        WaitEvent(&Event);
        assert(pthread_join(Canceller, NULL) == 0);
        JoinWorkers();
        assert(Event.Count == 1);
        assert(Overlapped.Internal == NO_ERROR || Overlapped.Internal == WSA_E_CANCELLED ||
            Overlapped.Internal == WSAETIMEDOUT);
        assert(Job.Result == NO_ERROR || Job.Result == WSA_INVALID_HANDLE);
        assert((Overlapped.Internal == WSA_E_CANCELLED) == (Job.Result == NO_ERROR));
        if (Overlapped.Internal == NO_ERROR) {
            assert(Result != NULL);
            FreeAddrInfoExW(Result);
        } else {
            assert(Result == NULL && ResultCount == 0);
        }
        assert(GetAddrInfoExCancel(&Handle) == WSA_INVALID_HANDLE);
    }
}
static void Completion(ULONG Status, ULONG Bytes, LPOVERLAPPED Overlapped)
{
    HANDLE Copy;
    (void) Bytes;
    CallbackStatus = Status;
    assert(Overlapped == CallbackOverlapped);
    assert(Overlapped->Internal == Status);
    if (ReenterCallback) {
        Copy = *CallbackHandleSlot;
        CallbackCancelStatus = GetAddrInfoExCancel(&Copy);
    }
    if (FreeCallerInCallback) {
        FreeAddrInfoExW(*CallbackResultSlot);
        free(CallbackResultSlot);
        free(CallbackHandleSlot);
        free(CallbackOverlapped);
    }
    SetEvent(&CallbackEvent);
}
static void TestCallbackFreeingCaller(int Timeout)
{
    TIMEVAL Duration;
    INT Status;
    ResetMock();
    CallbackResultSlot = (PPADDRINFOEXW) calloc(1, sizeof(*CallbackResultSlot));
    CallbackHandleSlot = (PHANDLE) calloc(1, sizeof(*CallbackHandleSlot));
    CallbackOverlapped = (LPOVERLAPPED) calloc(1, sizeof(*CallbackOverlapped));
    assert(CallbackResultSlot && CallbackHandleSlot && CallbackOverlapped);
    FreeCallerInCallback = ReenterCallback = TRUE;
    FastResolver = !Timeout;
    Duration.tv_sec = 1;
    Duration.tv_usec = 0;
    Status = Ext_GetAddrInfoExW(L"test.invalid", NULL, 0, NULL, NULL, CallbackResultSlot,
        Timeout ? &Duration : NULL, CallbackOverlapped, Completion, CallbackHandleSlot);
    assert(Status == WSA_IO_PENDING);
    if (Timeout) {
        WaitResolver();
        FireTimer();
    }
    WaitEvent(&CallbackEvent);
    ReleaseAll();
    JoinWorkers();
    assert(CallbackEvent.Count == 1);
    assert(CallbackStatus == (ULONG) (Timeout ? WSAETIMEDOUT : NO_ERROR));
    assert(CallbackCancelStatus == WSA_INVALID_HANDLE);
}
static void TestSubmissionFailures(void)
{
    OVERLAPPED Overlapped;
    MOCK_EVENT Event;
    PADDRINFOEXW Result;
    HANDLE Handle;
    int Mode;
    for (Mode = 0; Mode != 3; ++Mode) {
        ResetMock();
        InitOverlapped(&Overlapped, &Event);
        Result = NULL;
        Handle = (HANDLE) (uintptr_t) 123;
        FailHeap = Mode == 0;
        FailTimer = Mode == 1;
        FailSubmit = Mode == 2;
        assert(Start(&Overlapped, &Result, &Handle, TRUE) ==
            (Mode == 2 ? WSASYSCALLFAILURE : WSA_NOT_ENOUGH_MEMORY));
        assert(Handle == NULL && Result == NULL && Event.Count == 0 && WorkerCount == 0);
    }
    ResetMock();
}
static void TestTokenExhaustion(void)
{
    OVERLAPPED Overlapped;
    MOCK_EVENT Event;
    PADDRINFOEXW Result;
    HANDLE Handle;
    ResetMock();
    GaiNextHandle = (ULONG_PTR) -2;
    InitOverlapped(&Overlapped, &Event);
    Result = NULL;
    Handle = (HANDLE) (uintptr_t) 123;
    assert(Start(&Overlapped, &Result, &Handle, TRUE) == WSA_NOT_ENOUGH_MEMORY);
    assert(Handle == NULL && WorkerCount == 0 && Event.Count == 0);
    ResetMock();
}
static void TestMultipleOperations(void)
{
    OVERLAPPED FirstOverlapped;
    OVERLAPPED SecondOverlapped;
    MOCK_EVENT FirstEvent;
    MOCK_EVENT SecondEvent;
    PADDRINFOEXW FirstResult;
    PADDRINFOEXW SecondResult;
    HANDLE FirstHandle;
    HANDLE SecondHandle;
    ResetMock();
    InitOverlapped(&FirstOverlapped, &FirstEvent);
    InitOverlapped(&SecondOverlapped, &SecondEvent);
    FirstResult = SecondResult = NULL;
    assert(Start(&FirstOverlapped, &FirstResult, &FirstHandle, FALSE) == WSA_IO_PENDING);
    assert(Start(&SecondOverlapped, &SecondResult, &SecondHandle, FALSE) == WSA_IO_PENDING);
    LockMock();
    while (ResolverCalls != 2) WaitMock();
    UnlockMock();
    assert(FirstHandle != SecondHandle);
    assert(GetAddrInfoExCancel(&FirstHandle) == NO_ERROR);
    assert(FirstEvent.Count == 1 && SecondEvent.Count == 0);
    ReleaseAll();
    WaitEvent(&SecondEvent);
    JoinWorkers();
    assert(FirstEvent.Count == 1 && FirstResult == NULL);
    assert(SecondEvent.Count == 1 && SecondResult != NULL);
    assert(GetAddrInfoExCancel(&SecondHandle) == WSA_INVALID_HANDLE);
    FreeAddrInfoExW(SecondResult);
}
static void TestWithoutCancelHandle(void)
{
    OVERLAPPED Overlapped;
    MOCK_EVENT Event;
    PADDRINFOEXW Result;
    ResetMock();
    InitOverlapped(&Overlapped, &Event);
    Result = NULL;
    assert(Start(&Overlapped, &Result, NULL, TRUE) == WSA_IO_PENDING);
    WaitResolver();
    assert(GaiHandleList == NULL);
    FireTimer();
    WaitEvent(&Event);
    ReleaseAll();
    JoinWorkers();
    assert(Overlapped.Internal == WSAETIMEDOUT && Result == NULL && Event.Count == 1);
    ResetMock();
    FastResolver = TRUE;
    assert(Ext_GetAddrInfoExW(L"test.invalid", NULL, 0, NULL, NULL, &Result,
        NULL, NULL, NULL, NULL) == NO_ERROR);
    assert(Result != NULL && HeapCount == 0 && WorkerCount == 0);
    FreeAddrInfoExW(Result);
}
int main(int ArgumentCount, char **Arguments)
{
    int Iterations;
    Iterations = ArgumentCount > 1 ? atoi(Arguments[1]) : 300;
    assert(Iterations > 0);
    CheckInvalidHandles();
    TestNormalCompletion();
    TestCancelBeforeWorker();
    TestCancelDuringLookup();
    TestTimeout();
    TestMultipleOperations();
    TestWithoutCancelHandle();
    TestCompletionRaces(Iterations);
    TestCallbackFreeingCaller(FALSE);
    TestCallbackFreeingCaller(TRUE);
    TestSubmissionFailures();
    TestTokenExhaustion();
    ResetMock();
    puts("PASS: normal/cancel/timeout, invalid/stale handles, callback frees, allocation/submission failures");
    printf("PASS: %d concurrent completion/cancel/timeout races; all workers/timers joined, no live contexts/results\n", Iterations);
    return 0;
}
