// Exact, bounded overlapped pipe I/O. Pending operations never reference a
// caller's stack after timeout; cancellation owns a private buffer and event.
#pragma once
#ifndef KEX_UAC_TEST
#include <KexUac.h>
#endif

typedef struct {
    OVERLAPPED Overlapped;
    HANDLE Event;
    BYTE Buffer[1];
} KXU_IO_OPERATION;

static DWORD WINAPI KxuDrainCancelledIo(PVOID Parameter)
{
    KXU_IO_OPERATION *Operation = (KXU_IO_OPERATION *) Parameter;
    WaitForSingleObject(Operation->Event, INFINITE);
    CloseHandle(Operation->Event);
    HeapFree(GetProcessHeap(), 0, Operation);
    return 0;
}

static VOID KxuReleaseIo(KXU_IO_OPERATION *Operation, BOOLEAN Pending, HANDLE Pipe)
{
    HANDLE Thread;

    if (Pending) {
        CancelIoEx(Pipe, &Operation->Overlapped);
        if (WaitForSingleObject(Operation->Event, 0) != WAIT_OBJECT_0) {
            Thread = CreateThread(NULL, 0, KxuDrainCancelledIo, Operation, 0, NULL);
            if (Thread) CloseHandle(Thread);
            // On thread-allocation failure, retain the still-pending storage.
            // Freeing it would permit a kernel write into freed memory. The
            // owning process releases it at exit; callers still return on time.
            return;
        }
    }
    CloseHandle(Operation->Event);
    HeapFree(GetProcessHeap(), 0, Operation);
}

static DWORD KxuTransfer(HANDLE Pipe, PVOID Buffer, DWORD Length, BOOLEAN Write)
{
    KXU_IO_OPERATION *Operation;
    DWORD Offset, Transferred, Error, Started, Elapsed, WaitResult;
    BOOL Completed;
    BOOLEAN Pending;

    if (!Buffer || !Length || Length > sizeof(KXU_REQUEST)) {
        return ERROR_INVALID_PARAMETER;
    }
    Operation = (KXU_IO_OPERATION *) HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
        FIELD_OFFSET(KXU_IO_OPERATION, Buffer) + Length);
    if (!Operation) return ERROR_NOT_ENOUGH_MEMORY;
    Operation->Event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!Operation->Event) {
        Error = GetLastError();
        HeapFree(GetProcessHeap(), 0, Operation);
        return Error;
    }
    if (Write) CopyMemory(Operation->Buffer, Buffer, Length);
    Offset = 0;
    Error = ERROR_SUCCESS;
    Pending = FALSE;
    Started = GetTickCount();
    while (Offset < Length) {
        Elapsed = GetTickCount() - Started;
        if (Elapsed >= KXU_TIMEOUT) {
            Error = ERROR_TIMEOUT;
            break;
        }
        ZeroMemory(&Operation->Overlapped, sizeof(Operation->Overlapped));
        Operation->Overlapped.hEvent = Operation->Event;
        if (!ResetEvent(Operation->Event)) {
            Error = GetLastError();
            break;
        }
        Transferred = 0;
        Completed = Write ?
            WriteFile(Pipe, Operation->Buffer + Offset, Length - Offset,
                &Transferred, &Operation->Overlapped) :
            ReadFile(Pipe, Operation->Buffer + Offset, Length - Offset,
                &Transferred, &Operation->Overlapped);
        if (!Completed) {
            Error = GetLastError();
            if (Error != ERROR_IO_PENDING) break;
            Pending = TRUE;
            Elapsed = GetTickCount() - Started;
            if (Elapsed >= KXU_TIMEOUT) {
                Error = ERROR_TIMEOUT;
                break;
            }
            WaitResult = WaitForSingleObject(Operation->Event, KXU_TIMEOUT - Elapsed);
            if (WaitResult != WAIT_OBJECT_0) {
                Error = WaitResult == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError();
                break;
            }
            Pending = FALSE;
            if (!GetOverlappedResult(Pipe, &Operation->Overlapped, &Transferred, FALSE)) {
                Error = GetLastError();
                break;
            }
        }
        Error = ERROR_SUCCESS;
        if (!Transferred || Transferred > Length - Offset) {
            Error = ERROR_BROKEN_PIPE;
            break;
        }
        Offset += Transferred;
    }
    if (!Error && !Write) CopyMemory(Buffer, Operation->Buffer, Length);
    KxuReleaseIo(Operation, Pending, Pipe);
    return Error;
}

static DWORD KxuRead(HANDLE Pipe, PVOID Buffer, DWORD Length)
{
    return KxuTransfer(Pipe, Buffer, Length, FALSE);
}

static DWORD KxuWrite(HANDLE Pipe, CONST VOID *Buffer, DWORD Length)
{
    return KxuTransfer(Pipe, (PVOID) Buffer, Length, TRUE);
}

static DWORD KxuConnect(HANDLE Pipe, HANDLE HelperProcess)
{
    KXU_IO_OPERATION *Operation;
    HANDLE WaitHandles[2];
    DWORD Error, WaitResult, Transferred;
    BOOLEAN Pending;

    if (!HelperProcess || HelperProcess == INVALID_HANDLE_VALUE) return ERROR_INVALID_PARAMETER;
    Operation = (KXU_IO_OPERATION *) HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
        sizeof(KXU_IO_OPERATION));
    if (!Operation) return ERROR_NOT_ENOUGH_MEMORY;
    Operation->Event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!Operation->Event) {
        Error = GetLastError();
        HeapFree(GetProcessHeap(), 0, Operation);
        return Error;
    }
    Operation->Overlapped.hEvent = Operation->Event;
    Pending = FALSE;
    if (ConnectNamedPipe(Pipe, &Operation->Overlapped)) {
        Error = ERROR_SUCCESS;
    } else {
        Error = GetLastError();
        if (Error == ERROR_PIPE_CONNECTED) {
            Error = ERROR_SUCCESS;
        } else if (Error == ERROR_IO_PENDING) {
            Pending = TRUE;
            WaitHandles[0] = Operation->Event;
            WaitHandles[1] = HelperProcess;
            WaitResult = WaitForMultipleObjects(2, WaitHandles, FALSE, KXU_TIMEOUT);
            if (WaitResult == WAIT_OBJECT_0) {
                Pending = FALSE;
                Transferred = 0;
                Error = GetOverlappedResult(Pipe, &Operation->Overlapped,
                    &Transferred, FALSE) ? ERROR_SUCCESS : GetLastError();
            } else if (WaitResult == WAIT_OBJECT_0 + 1) {
                Error = ERROR_PROCESS_ABORTED;
            } else {
                Error = WaitResult == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError();
            }
        }
    }
    KxuReleaseIo(Operation, Pending, Pipe);
    return Error;
}
