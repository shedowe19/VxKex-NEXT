#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <shldisp.h>
typedef PVOID HSTRING;
typedef PVOID *PPVOID;
typedef enum {BaseTrust,PartialTrust,FullTrust} TrustLevel;
extern const IID IID_IInspectable;
extern const IID IID_IAgileObject;
HRESULT WindowsCreateString(PCWSTR,UINT,HSTRING*);
PVOID TestAlloc(SIZE_T);
VOID TestFree(PVOID);
#define CoTaskMemAlloc TestAlloc
#define CoTaskMemFree TestFree

HRESULT WINAPI TestCoCreateInstance(REFCLSID,IUnknown*,DWORD,REFIID,void**);
#define CoCreateInstance TestCoCreateInstance
