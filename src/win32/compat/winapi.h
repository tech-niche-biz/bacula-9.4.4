/*
   Bacula(R) - The Network Backup Solution

   Copyright (C) 2000-2019 Kern Sibbald

   The original author of Bacula is Kern Sibbald, with contributions
   from many others, a complete list can be found in the file AUTHORS.

   You may use this file and others of this release according to the
   license defined in the LICENSE file, which includes the Affero General
   Public License, v3.0 ("AGPLv3") and some additional permissions and
   terms pursuant to its AGPLv3 Section 7.

   This notice must be preserved when any source code is
   conveyed and/or propagated.

   Bacula(R) is a registered trademark of Kern Sibbald.
*/
/*
 * Windows APIs that are different for each system.
 *   We use pointers to the entry points so that a
 *   single binary will run on all Windows systems.
 *
 *     Kern Sibbald MMIII
 */

#ifndef __WINAPI_H
#define __WINAPI_H

#ifndef POOLMEM
typedef char POOLMEM;
#endif

// unicode enabling of win 32 needs some defines and functions

// using an average of 3 bytes per character is probably fine in
// practice but I believe that Windows actually uses UTF-16 encoding
// as opposed to UCS2 which means characters 0x10000-0x10ffff are
// valid and result in 4 byte UTF-8 encodings.
#define MAX_PATH_UTF8    MAX_PATH*4  // strict upper bound on UTF-16 to UTF-8 conversion

// from
// http://msdn.microsoft.com/library/default.asp?url=/library/en-us/fileio/fs/getfileattributesex.asp
// In the ANSI version of this function, the name is limited to
// MAX_PATH characters. To extend this limit to 32,767 wide
// characters, call the Unicode version of the function and prepend
// "\\?\" to the path. For more information, see Naming a File.
#define MAX_PATH_W 32767

int wchar_2_UTF8(POOLMEM **pszUTF, const wchar_t *pszUCS);
int wchar_2_UTF8(char *pszUTF, const WCHAR *pszUCS, int cchChar = MAX_PATH_UTF8);
int UTF8_2_wchar(POOLMEM **pszUCS, const char *pszUTF);
int make_win32_path_UTF8_2_wchar(POOLMEM **pszUCS, const char *pszUTF, BOOL* pBIsRawPath = NULL);

// init with win9x, but maybe set to NT in InitWinAPI
extern DWORD DLL_IMP_EXP g_platform_id;
extern DWORD DLL_IMP_EXP g_MinorVersion;
extern DWORD DLL_IMP_EXP g_MajorVersion;

/* In ADVAPI32.DLL */
typedef BOOL (WINAPI * t_OpenProcessToken)(HANDLE, DWORD, PHANDLE);
typedef BOOL (WINAPI * t_AdjustTokenPrivileges)(HANDLE, BOOL,
          PTOKEN_PRIVILEGES, DWORD, PTOKEN_PRIVILEGES, PDWORD);
typedef BOOL (WINAPI * t_LookupPrivilegeValue)(LPCTSTR, LPCTSTR, PLUID);

extern t_OpenProcessToken      DLL_IMP_EXP p_OpenProcessToken;
extern t_AdjustTokenPrivileges DLL_IMP_EXP p_AdjustTokenPrivileges;
extern t_LookupPrivilegeValue  DLL_IMP_EXP p_LookupPrivilegeValue;

/* In MSVCRT.DLL */
typedef int (__cdecl * t_wunlink) (const wchar_t *);
typedef int (__cdecl * t_wmkdir) (const wchar_t *);
typedef int (__cdecl * t_wopen)  (const wchar_t *, int, ...);

extern t_wunlink   DLL_IMP_EXP p_wunlink;
extern t_wmkdir    DLL_IMP_EXP p_wmkdir;

/* In KERNEL32.DLL */
typedef BOOL (WINAPI * t_GetFileAttributesExA)(LPCSTR, GET_FILEEX_INFO_LEVELS,
       LPVOID);
typedef BOOL (WINAPI * t_GetFileAttributesExW)(LPCWSTR, GET_FILEEX_INFO_LEVELS,
       LPVOID);

typedef DWORD (WINAPI * t_GetFileAttributesA)(LPCSTR);
typedef DWORD (WINAPI * t_GetFileAttributesW)(LPCWSTR);
typedef BOOL  (WINAPI * t_SetFileAttributesA)(LPCSTR, DWORD);
typedef BOOL  (WINAPI * t_SetFileAttributesW)(LPCWSTR, DWORD);

typedef HANDLE (WINAPI * t_CreateFileA) (LPCSTR, DWORD ,DWORD, LPSECURITY_ATTRIBUTES,
        DWORD , DWORD, HANDLE);
typedef HANDLE (WINAPI * t_CreateFileW) (LPCWSTR, DWORD ,DWORD, LPSECURITY_ATTRIBUTES,
        DWORD , DWORD, HANDLE);

typedef DWORD (WINAPI * t_OpenEncryptedFileRawA) (LPCSTR, ULONG, PVOID);
typedef DWORD (WINAPI * t_OpenEncryptedFileRawW) (LPCWSTR, ULONG, PVOID);
typedef DWORD (WINAPI * t_ReadEncryptedFileRaw) (PFE_EXPORT_FUNC, PVOID, PVOID);
typedef DWORD (WINAPI * t_WriteEncryptedFileRaw) (PFE_IMPORT_FUNC, PVOID, PVOID);
typedef void  (WINAPI * t_CloseEncryptedFileRaw) (PVOID);

typedef BOOL (WINAPI * t_CreateDirectoryA) (LPCSTR, LPSECURITY_ATTRIBUTES);
typedef BOOL (WINAPI * t_CreateDirectoryW) (LPCWSTR, LPSECURITY_ATTRIBUTES);

typedef DWORD (WINAPI * t_GetFileInformationByHandleEx)(HANDLE, FILE_INFO_BY_HANDLE_CLASS, LPVOID, DWORD);

typedef BOOL (WINAPI * t_SetProcessShutdownParameters)(DWORD, DWORD);
typedef BOOL (WINAPI * t_BackupRead)(HANDLE,LPBYTE,DWORD,LPDWORD,BOOL,BOOL,LPVOID*);
typedef BOOL (WINAPI * t_BackupWrite)(HANDLE,LPBYTE,DWORD,LPDWORD,BOOL,BOOL,LPVOID*);

typedef int (WINAPI * t_WideCharToMultiByte) (UINT CodePage, DWORD , LPCWSTR, int,
                                              LPSTR, int, LPCSTR, LPBOOL);

typedef int (WINAPI * t_MultiByteToWideChar) (UINT, DWORD, LPCSTR, int, LPWSTR, int);
typedef HANDLE (WINAPI * t_FindFirstFileA) (LPCSTR, LPWIN32_FIND_DATAA);
typedef HANDLE (WINAPI * t_FindFirstFileW) (LPCWSTR, LPWIN32_FIND_DATAW);

typedef BOOL (WINAPI * t_FindNextFileA) (HANDLE, LPWIN32_FIND_DATAA);
typedef BOOL (WINAPI * t_FindNextFileW) (HANDLE, LPWIN32_FIND_DATAW);

typedef BOOL (WINAPI * t_SetCurrentDirectoryA) (LPCSTR);
typedef BOOL (WINAPI * t_SetCurrentDirectoryW) (LPCWSTR);

typedef DWORD (WINAPI * t_GetCurrentDirectoryA) (DWORD, LPSTR);
typedef DWORD (WINAPI * t_GetCurrentDirectoryW) (DWORD, LPWSTR);

typedef BOOL (WINAPI * t_GetVolumePathNameW) (LPCWSTR, LPWSTR, DWORD);
typedef BOOL (WINAPI * t_GetVolumeNameForVolumeMountPointW) (LPCWSTR, LPWSTR, DWORD);

typedef BOOL (WINAPI * t_CreateSymbolicLinkA) (LPCSTR,  LPCSTR,  DWORD);
typedef BOOL (WINAPI * t_CreateSymbolicLinkW) (LPCWSTR, LPCWSTR, DWORD);
/* See: http://msdn.microsoft.com/en-us/library/windows/desktop/cc805844%28v=vs.85%29.aspx */
typedef int (WSAAPI * t_InetPton)(int, const char *, void *);


typedef BOOL (WINAPI * t_AttachConsole) (DWORD);

typedef BOOL (WINAPI *t_CreateProcessA) (
   LPCSTR,
   LPSTR,
   LPSECURITY_ATTRIBUTES,
   LPSECURITY_ATTRIBUTES,
   BOOL,
   DWORD,
   PVOID,
   LPCSTR,
   LPSTARTUPINFOA,
   LPPROCESS_INFORMATION);
typedef BOOL (WINAPI *t_CreateProcessW) (
   LPCWSTR,
   LPWSTR,
   LPSECURITY_ATTRIBUTES,
   LPSECURITY_ATTRIBUTES,
   BOOL,
   DWORD,
   PVOID,
   LPCWSTR,
   LPSTARTUPINFOW,
   LPPROCESS_INFORMATION);

typedef BOOL (WINAPI *t_GetProcessMemoryInfo) (
  HANDLE                   Process,
  PPROCESS_MEMORY_COUNTERS ppsmemCounters,
  DWORD                    cb);

typedef BOOL (WINAPI *t_EmptyWorkingSet) (HANDLE hProcess);

typedef struct _VSS_SNAPSHOT_PROP VSS_SNAPSHOT_PROP;
class IVssBackupComponents;
class IVssExamineWriterMetadata;

typedef HRESULT (STDAPICALLTYPE* t_CreateVssBackupComponents)(IVssBackupComponents **);
typedef void (APIENTRY* t_VssFreeSnapshotProperties)(VSS_SNAPSHOT_PROP*);
typedef HRESULT (WINAPI* t_CreateVssExamineWriterMetadata)(BSTR, IVssExamineWriterMetadata **);

extern t_CreateVssBackupComponents DLL_IMP_EXP p_CreateVssBackupComponents;
extern t_VssFreeSnapshotProperties DLL_IMP_EXP p_VssFreeSnapshotProperties;
extern t_CreateVssExamineWriterMetadata DLL_IMP_EXP p_CreateVssExamineWriterMetadata;

extern t_EmptyWorkingSet DLL_IMP_EXP p_EmptyWorkingSet;
extern t_GetProcessMemoryInfo DLL_IMP_EXP p_GetProcessMemoryInfo;
extern t_CreateProcessA DLL_IMP_EXP p_CreateProcessA;
extern t_CreateProcessW DLL_IMP_EXP p_CreateProcessW;

extern t_GetFileAttributesA   DLL_IMP_EXP p_GetFileAttributesA;
extern t_GetFileAttributesW   DLL_IMP_EXP p_GetFileAttributesW;

extern t_GetFileAttributesExA   DLL_IMP_EXP p_GetFileAttributesExA;
extern t_GetFileAttributesExW   DLL_IMP_EXP p_GetFileAttributesExW;

extern t_SetFileAttributesA   DLL_IMP_EXP p_SetFileAttributesA;
extern t_SetFileAttributesW   DLL_IMP_EXP p_SetFileAttributesW;

extern t_CreateFileA   DLL_IMP_EXP p_CreateFileA;
extern t_CreateFileW   DLL_IMP_EXP p_CreateFileW;

extern t_OpenEncryptedFileRawA DLL_IMP_EXP p_OpenEncryptedFileRawA;
extern t_OpenEncryptedFileRawW DLL_IMP_EXP p_OpenEncryptedFileRawW;
extern t_ReadEncryptedFileRaw  DLL_IMP_EXP p_ReadEncryptedFileRaw;
extern t_WriteEncryptedFileRaw DLL_IMP_EXP p_WriteEncryptedFileRaw;
extern t_CloseEncryptedFileRaw DLL_IMP_EXP p_CloseEncryptedFileRaw;

extern t_CreateDirectoryA   DLL_IMP_EXP p_CreateDirectoryA;
extern t_CreateDirectoryW   DLL_IMP_EXP p_CreateDirectoryW;

extern t_GetFileInformationByHandleEx DLL_IMP_EXP p_GetFileInformationByHandleEx;

extern t_SetProcessShutdownParameters DLL_IMP_EXP p_SetProcessShutdownParameters;
extern t_BackupRead         DLL_IMP_EXP p_BackupRead;
extern t_BackupWrite        DLL_IMP_EXP p_BackupWrite;

extern t_WideCharToMultiByte DLL_IMP_EXP p_WideCharToMultiByte;
extern t_MultiByteToWideChar DLL_IMP_EXP p_MultiByteToWideChar;

extern t_FindFirstFileA DLL_IMP_EXP p_FindFirstFileA;
extern t_FindFirstFileW DLL_IMP_EXP p_FindFirstFileW;

extern t_FindNextFileA DLL_IMP_EXP p_FindNextFileA;
extern t_FindNextFileW DLL_IMP_EXP p_FindNextFileW;

extern t_SetCurrentDirectoryA DLL_IMP_EXP p_SetCurrentDirectoryA;
extern t_SetCurrentDirectoryW DLL_IMP_EXP p_SetCurrentDirectoryW;

extern t_GetCurrentDirectoryA DLL_IMP_EXP p_GetCurrentDirectoryA;
extern t_GetCurrentDirectoryW DLL_IMP_EXP p_GetCurrentDirectoryW;

extern t_GetVolumePathNameW DLL_IMP_EXP p_GetVolumePathNameW;
extern t_GetVolumeNameForVolumeMountPointW DLL_IMP_EXP p_GetVolumeNameForVolumeMountPointW;

extern t_AttachConsole DLL_IMP_EXP p_AttachConsole;

extern t_CreateSymbolicLinkW DLL_IMP_EXP p_CreateSymbolicLinkW;
extern t_CreateSymbolicLinkA DLL_IMP_EXP p_CreateSymbolicLinkA;
extern t_InetPton DLL_IMP_EXP p_InetPton;

extern HMODULE DLL_IMP_EXP vsslib;

void InitWinAPIWrapper();

/* In SHFolder.dll on older systems, and now Shell32.dll */
typedef BOOL (WINAPI * t_SHGetFolderPath)(HWND, int, HANDLE, DWORD, LPTSTR);
extern t_SHGetFolderPath  DLL_IMP_EXP p_SHGetFolderPath;

#endif /* __WINAPI_H */
