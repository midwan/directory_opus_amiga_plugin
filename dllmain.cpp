// dllmain.cpp : Defines the entry point for the DLL application.
#include "stdafx.h"

// 8040a29e-0754-4ed7-a532-3688e08fff41
static const GUID GUIDPlugin_ADF = { 0x8040a29e, 0x0754, 0x4ed7,{ 0xa5, 0x32, 0x36, 0x88, 0xe0, 0x8f,0xff, 0x41 } };

HINSTANCE g_hModuleInstance = 0;


extern "C"
{
    __declspec(dllexport) bool VFS_IdentifyA(LPVFSPLUGININFOA lpVFSInfo);
    __declspec(dllexport) bool VFS_ReadDirectoryA(HANDLE hData, LPVFSFUNCDATA lpVFSData, LPVFSREADDIRDATAA lpRDD);

    __declspec(dllexport) HANDLE VFS_Create(LPGUID pGuid);
    __declspec(dllexport) void VFS_Destroy(HANDLE hData);
    __declspec(dllexport) long VFS_GetLastError(HANDLE hData);

    __declspec(dllexport) HANDLE VFS_CreateFileA(HANDLE hData, LPVFSFUNCDATA lpVFSData, LPTSTR lpszPath, DWORD dwMode, DWORD dwFileAttr, DWORD dwFlags, LPFILETIME lpFT);
    __declspec(dllexport) bool VFS_DeleteFileA(HANDLE hData, LPVFSFUNCDATA lpFuncData, LPTSTR lpszPath, DWORD dwFlags, int iSecurePasses);
    __declspec(dllexport) bool VFS_ReadFile(HANDLE hData, LPVFSFUNCDATA lpVFSData, HANDLE hFile, LPVOID lpData, DWORD dwSize, LPDWORD lpdwReadSize);
    __declspec(dllexport) void VFS_CloseFile(HANDLE hData, LPVFSFUNCDATA lpVFSData, HANDLE hFile);
    __declspec(dllexport) int VFS_ContextVerbA(HANDLE hData, LPVFSFUNCDATA lpVFSData, LPVFSCONTEXTVERBDATAA lpVerbData);
    __declspec(dllexport) UINT VFS_BatchOperationA(HANDLE hData, LPVFSFUNCDATA lpVFSData, LPTSTR lpszPath, LPVFSBATCHDATAA lpBatchData);
    
    __declspec(dllexport) bool VFS_GetFreeDiskSpaceA(HANDLE hData, LPVFSFUNCDATA lpFuncData, LPTSTR lpszPath, unsigned __int64* piFreeBytesAvailable, unsigned __int64* piTotalBytes, unsigned __int64* piTotalFreeBytes);

    __declspec(dllexport) bool VFS_USBSafe(LPOPUSUSBSAFEDATA pUSBSafeData);
    __declspec(dllexport) bool VFS_Init(LPVFSINITDATA pInitData);
    __declspec(dllexport) void VFS_Uninit();
};


extern "C" int WINAPI DllMain(HINSTANCE hInstance, DWORD dwReason, LPVOID) {
    g_hModuleInstance = hInstance;
    return 1;
}

// Initialise plugin
bool VFS_Init(LPVFSINITDATA pInitData) {

    adfEnvInitDefault();
    return true;
}

void VFS_Uninit() {
    adfEnvCleanUp();
}

bool VFS_IdentifyA(LPVFSPLUGININFOA lpVFSInfo) {
    // Initialise plugin information
    lpVFSInfo->idPlugin = GUIDPlugin_ADF;
    lpVFSInfo->dwFlags = VFSF_CANCONFIGURE | VFSF_NONREENTRANT;
    lpVFSInfo->dwCapabilities = VFSCAPABILITY_CASESENSITIVE | VFSCAPABILITY_POSTCOPYREREAD;

    strcpy_s(lpVFSInfo->lpszHandleExts, lpVFSInfo->cchHandleExtsMax, ".adf;.hdf");
    strcpy_s(lpVFSInfo->lpszName, lpVFSInfo->cchNameMax, "ADF");
    strcpy_s(lpVFSInfo->lpszDescription, lpVFSInfo->cchDescriptionMax, "ADF Ext");
    strcpy_s(lpVFSInfo->lpszCopyright, lpVFSInfo->cchCopyrightMax, "(c) Copyright 2018 Robert Crossfield");
    strcpy_s(lpVFSInfo->lpszURL, lpVFSInfo->cchURLMax, "github.com/segrax");

    return true;
}

bool VFS_USBSafe(LPOPUSUSBSAFEDATA pUSBSafeData) {
    return true;
}

HANDLE VFS_Create(LPGUID pGuid) {
    return (HANDLE)new cADFPluginData();
}

void VFS_Destroy(HANDLE hData) {
    delete (cADFPluginData*)hData;
}

bool VFS_ReadDirectoryA(HANDLE hData, LPVFSFUNCDATA lpFuncData, LPVFSREADDIRDATAA lpRDD) {
    return (hData) ? ((cADFPluginData*)hData)->ReadDirectory(lpRDD) : false;
}

bool VFS_ReadFile(HANDLE hData, LPVFSFUNCDATA lpVFSData, HANDLE hFile, LPVOID lpData, DWORD dwSize, LPDWORD lpdwReadSize) {
    return (hData) ? ((cADFPluginData*)hData)->ReadFile((File*) hFile, dwSize, (std::uint8_t*) lpData) : false;
}

void VFS_CloseFile(HANDLE hData, LPVFSFUNCDATA lpVFSData, HANDLE hFile) {
    ((cADFPluginData*)hData)->CloseFile((File*) hFile);
}

HANDLE VFS_CreateFileA(HANDLE hData, LPVFSFUNCDATA lpVFSData, LPTSTR lpszPath, DWORD dwMode, DWORD dwFileAttr, DWORD dwFlags, LPFILETIME lpFT) {
    return (hData) ? ((cADFPluginData*)hData)->OpenFile(lpszPath) : 0;
}

int VFS_ContextVerbA(HANDLE hData, LPVFSFUNCDATA lpVFSData, LPVFSCONTEXTVERBDATAA lpVerbData) {

    return (hData) ? ((cADFPluginData*)hData)->ContextVerb(lpVerbData) : VFSCVRES_FAIL;
}

UINT VFS_BatchOperationA(HANDLE hData, LPVFSFUNCDATA lpVFSData, LPTSTR lpszPath, LPVFSBATCHDATAA lpBatchData) {

    return (hData) ? ((cADFPluginData*)hData)->BatchOperation(lpszPath, lpBatchData) : VFSCVRES_FAIL;
}

bool VFS_GetFreeDiskSpaceA(HANDLE hData, LPVFSFUNCDATA lpFuncData, LPTSTR lpszPath, unsigned __int64* piFreeBytesAvailable, unsigned __int64* piTotalBytes, unsigned __int64* piTotalFreeBytes) {

    if (!hData)
        return false;

    if(piFreeBytesAvailable)
        *piFreeBytesAvailable = ((cADFPluginData*)hData)->TotalFreeBlocks(lpszPath);

    if(piTotalFreeBytes)
        *piTotalFreeBytes = ((cADFPluginData*)hData)->TotalFreeBlocks(lpszPath);

    if(piTotalBytes)
        *piTotalBytes = ((cADFPluginData*)hData)->TotalDiskBlocks(lpszPath);
    
    return true;
}

bool VFS_DeleteFileA(HANDLE hData, LPVFSFUNCDATA lpFuncData, LPTSTR lpszPath, DWORD dwFlags, int iSecurePasses) {
//    return (hData) ? ((cADFPluginData*)hData)->Delete(lpszPath) : VFSCVRES_FAIL;
    return false;
}