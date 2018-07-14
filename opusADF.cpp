// opusADF.cpp : Defines the exported functions for the DLL application.
//

#include "stdafx.h"
#include "ADFlib/src/adflib.h"

DOpusPluginHelperFunction DOpus;
std::map<std::string, wpDevice> gOpenFiles;
std::map<std::string, wpVolume>     gOpenVolume;

bool adfIsLeap(int y) {
    return((bool)(!(y % 100) ? !(y % 400) : !(y % 4)));
}

void adfTime2AmigaTime(struct DateTime dt, int32_t *day, int32_t *min, int32_t *ticks) {
    int jm[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };


    *min = dt.hour * 60 + dt.min;                /* mins */
    *ticks = dt.sec * 50;                        /* ticks */

                                                 /*--- days ---*/

    *day = dt.day;                         /* current month days */

                                               /* previous months days downto january */
    if (dt.mon > 1) {                      /* if previous month exists */
        dt.mon--;
        if (dt.mon > 2 && adfIsLeap(dt.year))    /* months after a leap february */
            jm[2 - 1] = 29;
        while (dt.mon > 0) {
            *day = *day + jm[dt.mon - 1];
            dt.mon--;
        }
    }

    /* years days before current year downto 1978 */
    if (dt.year > 78) {
        dt.year--;
        while (dt.year >= 78) {
            if (adfIsLeap(dt.year))
                *day = *day + 366;
            else
                *day = *day + 365;
            dt.year--;
        }
    }
}

std::vector<std::string> tokenize(const std::string& in, const std::string& delim) {
    std::vector<std::string> tokens;

    std::string::size_type pos_begin, pos_end = 0;
    std::string input = in;

    while ((pos_begin = input.find_first_not_of(delim, pos_end)) != std::string::npos) {
        pos_end = input.find_first_of(delim, pos_begin);
        if (pos_end == std::string::npos) pos_end = input.length();

        tokens.push_back(input.substr(pos_begin, pos_end - pos_begin));
    }

    return tokens;
}

cADFPluginData::cADFPluginData() {
    mAdfDevice = 0;
    mAdfVolume = 0;
}

cADFPluginData::~cADFPluginData() {

}

FILETIME cADFPluginData::GetFileTime(const Entry *pEntry) {
    SYSTEMTIME AmigaTime, TZAdjusted;
    AmigaTime.wDayOfWeek = 0;
    AmigaTime.wMilliseconds = 0;
    AmigaTime.wYear = pEntry->year;
    AmigaTime.wMonth = pEntry->month;
    AmigaTime.wDay = pEntry->days;
    AmigaTime.wHour = pEntry->hour;
    AmigaTime.wMinute = pEntry->mins;
    AmigaTime.wSecond = pEntry->secs;
    TzSpecificLocalTimeToSystemTime(NULL, &AmigaTime, &TZAdjusted);
    FILETIME ft;
    SystemTimeToFileTime(&TZAdjusted, &ft);
    return ft;
}

DateTime ToDateTime(FILETIME pFT) {
    SYSTEMTIME AmigaTime, TZAdjusted;
    FileTimeToSystemTime(&pFT, &AmigaTime);
    SystemTimeToTzSpecificLocalTime(NULL, &AmigaTime, &TZAdjusted);

    DateTime dt;
    dt.year = TZAdjusted.wYear - 1900;
    dt.mon = TZAdjusted.wMonth;
    dt.day = TZAdjusted.wDay;
    dt.hour = TZAdjusted.wHour;
    dt.min = TZAdjusted.wMinute;
    dt.sec = TZAdjusted.wSecond;
    return dt;
}

void cADFPluginData::SetEntryTime(File *pFile, FILETIME pFT) {

    DateTime dt = ToDateTime(pFT);

    adfTime2AmigaTime(dt, &(pFile->fileHdr->days), &(pFile->fileHdr->mins), &(pFile->fileHdr->ticks));
}

LPVFSFILEDATAHEADER cADFPluginData::GetVFSforEntry(const Entry *pEntry, HANDLE pHeap) {
    LPVFSFILEDATAHEADER lpFDH;

    if (lpFDH = (LPVFSFILEDATAHEADER)HeapAlloc(pHeap, 0, sizeof(VFSFILEDATAHEADER) + sizeof(VFSFILEDATA))) {
        LPVFSFILEDATA lpFD = (LPVFSFILEDATA)(lpFDH + 1);

        lpFDH->cbSize = sizeof(VFSFILEDATAHEADER);
        lpFDH->lpNext = 0;
        lpFDH->iNumItems = 1;
        lpFDH->cbFileDataSize = sizeof(VFSFILEDATA);

        lpFD->dwFlags = 0;
        lpFD->lpszComment = 0;
        lpFD->iNumColumns = 0;
        lpFD->lpvfsColumnData = 0;

        GetWfdForEntry(pEntry, &lpFD->wfdData);
    }

    return lpFDH;
}

void cADFPluginData::GetWfdForEntry(const Entry *pEntry, LPWIN32_FIND_DATA pData) {

    strcpy(pData->cFileName, pEntry->name);

    pData->nFileSizeHigh = 0;
    pData->nFileSizeLow = pEntry->size;

    if (pEntry->type == ST_DIR)
        pData->dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
    else
        pData->dwFileAttributes = FILE_ATTRIBUTE_NORMAL;

    pData->dwReserved0 = 0;
    pData->dwReserved1 = 0;

    pData->ftCreationTime = {};
    pData->ftLastAccessTime = {};
    pData->ftLastWriteTime = GetFileTime(pEntry);
}

std::vector<std::string> cADFPluginData::GetPaths(std::string pPath) {
    std::vector<std::string> Depth;

    if (pPath.find(mPath) != std::string::npos) {
        pPath = pPath.replace(pPath.begin(), pPath.begin() + mPath.length(), "");
        Depth = tokenize(pPath, "\\\\");
    }

    return Depth;
}

bool cADFPluginData::AdfChangeToPath(const std::string& pPath, bool pIgnoreLast) {
    bool result = false;

    if (!LoadFile(pPath))
        return false;

    std::vector<std::string> Depth = GetPaths(pPath);

    if (pIgnoreLast) {
        if (Depth.size() > 0)
            Depth.erase(Depth.end() - 1);
    }

    if (mAdfVolume) {

        adfToRootDir(mAdfVolume.get());

        auto head = GetCurrentDirectoryList();
        List *cell = head.get();
        while (cell) {
            struct Entry* entry = (Entry*)cell->content;

            if (!(entry->type == ST_LFILE || entry->type == ST_LDIR || entry->type == ST_LSOFT)) {

                if (Depth.size()) {

                    // Found our sub directory?
                    if (!Depth[0].compare(entry->name)) {

                        // Free current cell, and load the next
                        adfChangeDir(mAdfVolume.get(), entry->name);

                        head = GetCurrentDirectoryList();
                        cell = head.get();
                        Depth.erase(Depth.begin());

                        // Empty folder?
                        if (!cell)
                            return true;

                        continue;
                    }
                }
                else {
                    result = true;
                    break;
                }
            }

            cell = cell->next;
        }
    }

    return result;
}

bool cADFPluginData::LoadFile(const std::string& pAdfPath) {
    std::string AdfPath = pAdfPath;

    std::transform(AdfPath.begin(), AdfPath.end(), AdfPath.begin(), ::tolower);
    size_t EndPos = AdfPath.find(".adf");
    if (EndPos == std::string::npos) {
        EndPos = AdfPath.find(".hdf");
    }
    if (EndPos == std::string::npos)
        return false;
    AdfPath = pAdfPath.substr(0, EndPos + 4);

    if (!mAdfDevice || !mAdfVolume) {
        // Find an open device for this disk image
        auto ExistingDev = gOpenFiles.find(AdfPath);
        if (ExistingDev != gOpenFiles.end() && ExistingDev->second.expired()) {
            gOpenFiles.erase(ExistingDev);
            ExistingDev = gOpenFiles.end();
        }
        if (ExistingDev != gOpenFiles.end()) {
            mAdfDevice = ExistingDev->second.lock();
        } else {
            mAdfDevice = std::move(std::shared_ptr<Device>(adfMountDev(const_cast<char*>(AdfPath.c_str()), true), adfUnMountDev));
            gOpenFiles.insert(std::make_pair(AdfPath, mAdfDevice));
        }
        // Do we have a device?
        if (mAdfDevice) {
            // Find an open volume for this device
            auto Vol = gOpenVolume.find(AdfPath);
            if (Vol != gOpenVolume.end() && Vol->second.expired()) {
                gOpenVolume.erase(Vol);
                Vol = gOpenVolume.end();
            }

            if (Vol != gOpenVolume.end())
                mAdfVolume = Vol->second.lock();
            else {
                mAdfVolume = std::move(std::shared_ptr<Volume>(adfMount(mAdfDevice.get(), 0, true), adfUnMount));
                gOpenVolume.insert(std::make_pair(std::string(AdfPath.c_str()), mAdfVolume));
            }
            if (mAdfVolume) {
                mPath = AdfPath;
                return true;
            }
        }

        return false;
    }

    return true;
}

bool cADFPluginData::ReadDirectory(LPVFSREADDIRDATAA lpRDD) {

    // Free directory if lister is closing (otherwise ignore free command)
    if (lpRDD->vfsReadOp == VFSREAD_FREEDIRCLOSE)
        return true;

    if (lpRDD->vfsReadOp == VFSREAD_FREEDIR)
        return true;

    // Do nothing if we have no path
    if (!lpRDD->lpszPath || !*lpRDD->lpszPath) {
        mLastError = ERROR_PATH_NOT_FOUND;
        return false;
    }

    if (!AdfChangeToPath(lpRDD->lpszPath))
        return false;

    if (lpRDD->vfsReadOp == VFSREAD_CHANGEDIR)
        return true;

    auto head = GetCurrentDirectoryList();
    List *cell = head.get();

    LPVFSFILEDATAHEADER lpLastHeader = 0;
    while (cell) {
        struct Entry* entry = (Entry*)cell->content;

        if (!(entry->type == ST_LFILE || entry->type == ST_LDIR || entry->type == ST_LSOFT)) {
            LPVFSFILEDATAHEADER lpFDH = GetVFSforEntry(entry, lpRDD->hMemHeap);

            // Add the entries for this folder
            if (lpFDH) {
                if (lpLastHeader)
                    lpLastHeader->lpNext = lpFDH;
                else
                    lpRDD->lpFileData = lpFDH;

                lpLastHeader = lpFDH;
            }
        }

        cell = cell->next;
    }

    return true;
}

bool cADFPluginData::ReadFile(File* pFile, size_t pBytes, std::uint8_t* pBuffer) {

    if (!pFile)
        return false;

    return (adfReadFile(pFile, pBytes, pBuffer) > 0);
}

File* cADFPluginData::OpenFile(std::string pPath) {

    if (AdfChangeToPath(pPath)) {

        auto Paths = GetPaths(pPath);

        return adfOpenFile(mAdfVolume.get(), (char*)Paths[Paths.size() - 1].c_str(), (char*) "r");
    }

    return 0;
}

void cADFPluginData::CloseFile(File* pFile) {

    adfCloseFile(pFile);
}

spList cADFPluginData::GetCurrentDirectoryList() {

    return std::shared_ptr<List>(adfGetDirEnt(mAdfVolume.get(), mAdfVolume->curDirPtr), adfFreeDirList);
}

int cADFPluginData::ContextVerb(LPVFSCONTEXTVERBDATAA lpVerbData) {

    if (AdfChangeToPath(lpVerbData->lpszPath, true)) {
        auto Depth = GetPaths(lpVerbData->lpszPath);

        auto head = GetCurrentDirectoryList();
        List *cell = head.get();

        while (cell) {
            struct Entry* entry = (Entry*)cell->content;

            if (!Depth[Depth.size() - 1].compare(entry->name)) {

                if (entry->type == ST_FILE)
                    return VFSCVRES_EXTRACT;

                if (entry->type == ST_DIR)
                    return VFSCVRES_DEFAULT;
            }

            cell = cell->next;
        }
    }

    return VFSCVRES_FAIL;
}

int cADFPluginData::Delete(LPVFSBATCHDATAA lpBatchData, const std::string& pPath, const std::string& pFile, bool pAll) {
    int result = 0;
    DOpus.UpdateFunctionProgressBar(lpBatchData->lpFuncData, PROGRESSACTION_STATUSTEXT, (DWORD_PTR)"Deleting");

    auto Depth = tokenize(pFile, "\\\\");

    if (AdfChangeToPath(pPath)) {

        auto head = GetCurrentDirectoryList();
        List *cell = head.get();

        while (cell) {
            if (lpBatchData->hAbortEvent && WaitForSingleObject(lpBatchData->hAbortEvent, 0) == WAIT_OBJECT_0) {
                return 1;
            }

            struct Entry* entry = (Entry*)cell->content;

            // Entry match?
            if (pAll || !Depth[Depth.size() - 1].compare(entry->name)) {
                DOpus.UpdateFunctionProgressBar(lpBatchData->lpFuncData, PROGRESSACTION_SETFILENAME, (DWORD_PTR)entry->name);
                DOpus.UpdateFunctionProgressBar(lpBatchData->lpFuncData, PROGRESSACTION_SETFILESIZE, (DWORD_PTR)entry->size);

                std::string Filename = pPath;

                if (Filename[Filename.size() - 1] != '\\')
                    Filename += "\\";
                
                Filename += entry->name;

                if (entry->type == ST_FILE) {
                    result |= adfRemoveEntry(mAdfVolume.get(), entry->parent, entry->name);
                    if (!result)
                        DOpus.AddFunctionFileChange(lpBatchData->lpFuncData, true, OPUSFILECHANGE_REMDIR, Filename.c_str());
                }

                if (entry->type == ST_DIR) {

                    Delete(lpBatchData, Filename, entry->name, true);
                    result |= adfRemoveEntry(mAdfVolume.get(), entry->parent, entry->name);
                    AdfChangeToPath(pPath);
                    if (!result)
                        DOpus.AddFunctionFileChange(lpBatchData->lpFuncData, true, OPUSFILECHANGE_REMDIR, Filename.c_str());
                }
            }

            cell = cell->next;
        }
    }

    return 0;
}

cADFFindData* cADFPluginData::FindFirstFile(LPTSTR lpszPath, LPWIN32_FIND_DATA lpwfdData, HANDLE hAbortEvent) {

    cADFFindData* findData = new cADFFindData();

    if (AdfChangeToPath(lpszPath, true)) {

        auto depth = tokenize(lpszPath, "\\\\");
        auto filemask = depth[depth.size() - 1];
        findData->mFindMask = std::regex("." + filemask);

        findData->mHead = GetCurrentDirectoryList();
        findData->mCell = findData->mHead.get();

        while (findData->mCell) {
            struct Entry* entry = (Entry*)findData->mCell->content;

            if (std::regex_match(entry->name, findData->mFindMask)) {

                if (!(entry->type == ST_LFILE || entry->type == ST_LDIR || entry->type == ST_LSOFT)) {

                    GetWfdForEntry(entry, lpwfdData);
                    findData->mCell = findData->mCell->next;
                    break;
                }
            }

            findData->mCell = findData->mCell->next;
        }
    }

    return findData;
}

bool cADFPluginData::FindNextFile(cADFFindData* pFindData, LPWIN32_FIND_DATA lpwfdData) {

    while (pFindData->mCell) {
        struct Entry* entry = (Entry*)pFindData->mCell->content;

        if (std::regex_match(entry->name, pFindData->mFindMask)) {

            if (!(entry->type == ST_LFILE || entry->type == ST_LDIR || entry->type == ST_LSOFT)) {

                GetWfdForEntry(entry, lpwfdData);

                pFindData->mCell = pFindData->mCell->next;
                return true;
            }
        }

        pFindData->mCell = pFindData->mCell->next;
    }

    return false;
}

void cADFPluginData::FindClose(cADFFindData* pFindData) {
    delete pFindData;
}

int cADFPluginData::ImportFile(LPVFSBATCHDATAA lpBatchData, const std::string& pFile, const std::string& pPath) {

    auto Depth = tokenize(pPath, "\\\\");

    std::ifstream t(pPath, std::ios::binary);
    std::string FileData((std::istreambuf_iterator<char>(t)), std::istreambuf_iterator<char>());

    DOpus.UpdateFunctionProgressBar(lpBatchData->lpFuncData, PROGRESSACTION_SETFILENAME, (DWORD_PTR)Depth[Depth.size()-1].c_str());
    DOpus.UpdateFunctionProgressBar(lpBatchData->lpFuncData, PROGRESSACTION_SETFILESIZE, (DWORD_PTR)FileData.size());


    FILETIME ft;
    HANDLE filename = CreateFile(pPath.c_str(), FILE_READ_ATTRIBUTES, 0, 0, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    ::GetFileTime(filename, 0, 0, &ft);
    CloseHandle(filename);

    auto File = adfOpenFile(mAdfVolume.get(), (char*)Depth[Depth.size() - 1].c_str(), (char*) "w");
    if (!File)
        return 1;

    SetEntryTime(File, ft);
    adfWriteFile(File, FileData.size(), reinterpret_cast<uint8_t*>(&FileData[0]));
    adfCloseFile(File);

    std::string Final = pFile;
    if (Final[Final.size() - 1] != '\\')
        Final += "\\";
    Final += Depth[Depth.size() - 1];

    DOpus.AddFunctionFileChange(lpBatchData->lpFuncData, true, OPUSFILECHANGE_CREATE, Final.c_str());

    return 0;
}

std::vector<std::string> directoryList(const std::string pPath) {
    WIN32_FIND_DATA fdata;
    HANDLE dhandle;
    std::vector<std::string> results;

    if ((dhandle = ::FindFirstFile(pPath.c_str(), &fdata)) == INVALID_HANDLE_VALUE)
        return results;

    results.push_back(std::string(fdata.cFileName));

    while (1) {
        if (::FindNextFile(dhandle, &fdata)) {
            results.push_back(std::string(fdata.cFileName));
        }
        else {
            if (GetLastError() == ERROR_NO_MORE_FILES) {
                break;
            }
            else {
                FindClose(dhandle);
                return results;
            }
        }
    }

    FindClose(dhandle);
    return results;
}

int cADFPluginData::ImportPath(LPVFSBATCHDATAA lpBatchData, const std::string& pFile, const std::string& pPath) {

    std::string FinalPath = pPath;
    if (FinalPath[FinalPath.size() - 1] != '\\')
        FinalPath += "\\";

    auto Depth = tokenize(pPath, "\\\\");
    std::string Final = pFile;
    if (Final[Final.size() - 1] != '\\')
        Final += "\\";
    Final += Depth[Depth.size() - 1];

    FILETIME ft;
    HANDLE filename = CreateFile(pPath.c_str(), FILE_READ_ATTRIBUTES, 0, 0, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    ::GetFileTime(filename, 0, 0, &ft);
    CloseHandle(filename);
    DateTime dt = ToDateTime(ft);

    adfCreateDir(mAdfVolume.get(), mAdfVolume->curDirPtr, (char*)Depth[Depth.size() - 1].c_str(), dt);
    DOpus.AddFunctionFileChange(lpBatchData->lpFuncData, true, OPUSFILECHANGE_MAKEDIR, Final.c_str());

    auto head = GetCurrentDirectoryList();
    List *cell = head.get();

    while (cell) {
        struct Entry* entry = (Entry*)cell->content;

        if (!Depth[Depth.size() - 1].compare(entry->name)) {

            auto contents = directoryList(FinalPath + "*.*");

            for (auto& File : contents) {
                if (File == "." || File == "..")
                    continue;

                Import(lpBatchData, pFile + "\\" + Depth[Depth.size() - 1] + "\\", FinalPath + File);
            }

        }

        cell = cell->next;
    }


    return 0;
}

int cADFPluginData::Import(LPVFSBATCHDATAA lpBatchData, const std::string& pFile, const std::string& pPath) {

    if (AdfChangeToPath(pFile, false)) {

        if (lpBatchData->hAbortEvent && WaitForSingleObject(lpBatchData->hAbortEvent, 0) == WAIT_OBJECT_0) {
            return 1;
        }

        // is pPath a directory?
        auto Attribs = GetFileAttributesA(pPath.c_str());

        if (Attribs & FILE_ATTRIBUTE_DIRECTORY)
            ImportPath(lpBatchData, pFile, pPath);
        else {
            ImportFile(lpBatchData, pFile, pPath);
        }
    }

    return 0;
}

int cADFPluginData::Extract(LPVFSBATCHDATAA lpBatchData, const std::string& pFile, const std::string& pDest) {
    DOpus.UpdateFunctionProgressBar(lpBatchData->lpFuncData, PROGRESSACTION_STATUSTEXT, (DWORD_PTR)"Copying");

    int result = 1;

    if (AdfChangeToPath(pFile, true)) {
        auto Depth = GetPaths(pFile);

        auto head = GetCurrentDirectoryList();
        List *cell = head.get();

        while (cell) {

            if (lpBatchData->hAbortEvent && WaitForSingleObject(lpBatchData->hAbortEvent, 0) == WAIT_OBJECT_0) {
                return 1;
            }

            struct Entry* entry = (Entry*)cell->content;

            std::string FinalName = pDest;
            if (FinalName[FinalName.size() - 1] != '\\') {
                FinalName += '\\';
            }
            FinalName += entry->name;

            // Entry match?
            if (!Depth[Depth.size() - 1].compare(entry->name)) {

                DOpus.UpdateFunctionProgressBar(lpBatchData->lpFuncData, PROGRESSACTION_SETFILENAME, (DWORD_PTR)entry->name);
                DOpus.UpdateFunctionProgressBar(lpBatchData->lpFuncData, PROGRESSACTION_SETFILESIZE, (DWORD_PTR)entry->size);

                // Create a directory, or extract a file?
                if (entry->type == ST_DIR) {
                    CreateDirectory(FinalName.c_str(), 0);
                    result = ExtractPath(lpBatchData, pFile, FinalName);
                    DOpus.AddFunctionFileChange(lpBatchData->lpFuncData, false, OPUSFILECHANGE_CREATE, lpBatchData->pszDestPath);

                    if (!result) {
                        HANDLE filename = CreateFile(FinalName.c_str(), FILE_WRITE_ATTRIBUTES, 0, 0, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
                        SetFileTime(filename, 0, 0, &GetFileTime(entry));
                        CloseHandle(filename);
                    }
                }
                else {
                    result = ExtractFile(lpBatchData, entry, FinalName);
                }

                DOpus.UpdateFunctionProgressBar(lpBatchData->lpFuncData, PROGRESSACTION_NEXTFILE, 0);
                break;
            }

            cell = cell->next;
        }
    }

    return result;
}

int cADFPluginData::ExtractPath(LPVFSBATCHDATAA lpBatchData, const std::string& pPath, const std::string& pDest) {
    int result = 0;

    if (AdfChangeToPath(pPath)) {

        auto head = GetCurrentDirectoryList();
        List *cell = head.get();

        while (cell) {
            struct Entry* entry = (Entry*)cell->content;

            std::string FinalPath = pPath;
            if (FinalPath[FinalPath.size() - 1] != '\\') {
                FinalPath += '\\';
            }
            FinalPath += entry->name;

            result |= Extract(lpBatchData, FinalPath, pDest);

            cell = cell->next;
        }
    }

    return result;
}
int cADFPluginData::ExtractFile(LPVFSBATCHDATAA lpBatchData, const Entry* pEntry, const std::string& pDest) {
    std::string buffer(pEntry->size, 0);

    auto file = adfOpenFile(mAdfVolume.get(), pEntry->name, (char*)"r");
    if (!file)
        return 1;

    auto n = adfReadFile(file, pEntry->size, reinterpret_cast<uint8_t*>(&buffer[0]));
    if (!n)
        return 2;

    std::ofstream ofile(pDest, std::ios::binary);
    ofile.write(buffer.c_str(), buffer.size());
    ofile.close();

    DOpus.UpdateFunctionProgressBar(lpBatchData->lpFuncData, PROGRESSACTION_STEPBYTES, (DWORD_PTR)pEntry->size);

    HANDLE filename = CreateFile(pDest.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    SetFileTime(filename, 0, 0, &GetFileTime(pEntry));
    CloseHandle(filename);

    adfCloseFile(file);
    DOpus.AddFunctionFileChange(lpBatchData->lpFuncData, false, OPUSFILECHANGE_CREATE, lpBatchData->pszDestPath);
    return 0;
}

size_t cADFPluginData::TotalFreeBlocks(const std::string& pFile) {

    if (!LoadFile(pFile))
        return false;

    size_t blocks = adfCountFreeBlocks(mAdfVolume.get());
    blocks *= mAdfVolume->blockSize;

    return blocks;
}

size_t cADFPluginData::TotalDiskBlocks(const std::string& pFile) {
    return (size_t)mAdfDevice->size;
}

UINT cADFPluginData::BatchOperation(LPTSTR lpszPath, LPVFSBATCHDATAA lpBatchData) {
    DOpus.UpdateFunctionProgressBar(lpBatchData->lpFuncData, PROGRESSACTION_SETPERCENT, (DWORD_PTR)0);
    
    auto result = VFSBATCHRES_COMPLETE;

    auto File = lpBatchData->pszFiles;

    for (int i = 0; i < lpBatchData->iNumFiles; ++i) {

        if (lpBatchData->uiOperation == VFSBATCHOP_EXTRACT) {
            lpBatchData->piResults[i] = Extract(lpBatchData, File, lpBatchData->pszDestPath);
        }

        if (lpBatchData->uiOperation == VFSBATCHOP_ADD) {
            lpBatchData->piResults[i] = Import(lpBatchData, lpszPath, File);
        }

        if (lpBatchData->uiOperation == VFSBATCHOP_DELETE) {
            lpBatchData->piResults[i] = Delete(lpBatchData, lpszPath, File);
        }

        if (lpBatchData->piResults[i]) {
            result = VFSBATCHRES_ABORT;
            break;
        }

        File += strlen(File) + 1;
    }

    return result;
}
