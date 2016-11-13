/*++

Copyright (c) 1992-2000  Microsoft Corporation

Module Name:
    pooltag.cpp

Abstract:


Environment:

    User Mode.

Revision History:

    Kshitiz K. Sharma (kksharma) 3/5/2001

--*/

#include "precomp.h"
#include <copystr.h>


ULONG
MatchPoolTag(
    PSTR Tag,
    PSTR TagToMatch
    )
{
    BOOL Partial = FALSE;
    ULONG CharMatched = 0;
    for (ULONG i = 0; i<4; i++)
    {
        switch (Tag[i])
        {
        case '*':
            // Match the reminder
            return CharMatched;
        case '?':
            Partial = TRUE;
            break;
        default:
            // Cannot match a non widcard character after '?'
            if (!Partial && (Tag[i] == TagToMatch[i]))
            {
                CharMatched=i+1;
            } else
            {
                return FALSE;
            }
        }
    }
    return CharMatched;
}

BOOL
FindOwnerAndComponent(
    PSTR pszDesc,
    PDEBUG_POOLTAG_DESCRIPTION pDetails
    )
{
    PSTR pSrch, szTmp, pCurr, pLast;

    //
    // POOLTAG - filename.bin - description text - owner
    //           ^
    //            pszDesc starts from here
    //
    if (pszDesc) {
        pLast = pszDesc + strlen(pszDesc);
        pSrch = strchr(pszDesc, '-');

        if (pSrch)
        {
            CHAR PossibleBinName[50]={0};

            StringCchCatN(PossibleBinName, sizeof(PossibleBinName), pszDesc,
                          (ULONG) ((ULONG_PTR)pSrch - (ULONG_PTR)pszDesc));
            pCurr = PossibleBinName + strlen(PossibleBinName);

            // eat up trailing spaces
            while ((*pCurr == '\0' || *pCurr == ' ' || *pCurr == '\t')  && (pCurr > PossibleBinName) )
            {
                *pCurr = '\0';
                --pCurr;
            }

            if (strcmp(PossibleBinName, "<unknown>"))
            {
                StringCchCopy(pDetails->Binary, sizeof(PossibleBinName), PossibleBinName);
            }
            ++pSrch;
            while (*pSrch == ' ' || *pSrch == '\t') ++pSrch;

            pszDesc = pSrch;
        }

        pSrch = strrchr(pszDesc, '-');
        if (pSrch) {
            pCurr = pSrch;
            pSrch++;


            while (*pSrch == ' ' || *pSrch == '\t') ++pSrch;

            if (!strncmp(pSrch, "OWNER", 5)) {

                pSrch += strlen("OWNER");
                while (*pSrch == ' ' || *pSrch == '\t') ++pSrch;
                szTmp = pSrch;
                if (strlen(szTmp) < sizeof(pDetails->Owner)) {
                    strcpy(pDetails->Owner, szTmp);
                }

                pSrch = strchr(pszDesc, '-');
                pLast = pCurr;
            }
        }

        if (((ULONG) (pLast - pszDesc)+1) < sizeof(pDetails->Description)) {
            pDetails->Description[0] = 0;
            StringCchCatN(pDetails->Description, sizeof(pDetails->Description), pszDesc, (ULONG) (pLast - pszDesc));
            while (*pLast == '\n' || *pLast == '\r' || *pLast == '\0')
            {
                *pLast = 0;
                --pLast;
            }
            pDetails->Description[(ULONG) (pLast - pszDesc)+1] = 0;
        }
    } else {
        return FALSE;
    }
    return TRUE;
}

PSTR
GetNextLine(
    HANDLE hFile,
    BOOL bRestart
    )
// Returns next line in the file hFile
// Returns NULL if EOF is reached
{
    static CHAR FileLines1[MAX_PATH] = {0}, FileLines2[MAX_PATH] = {0};
    static CHAR FileLine[MAX_PATH];
    PCHAR pEOL;
    ULONG BytesRead;
    PCHAR pEndOfBuff;
    ULONG BuffLen, ReadLen;

    pEOL = NULL;
    if (bRestart)
    {
        FileLines1[0] = 0;
    }
    if (!(pEOL = strchr(FileLines1, '\n'))) {
        // We have something that was already read but it isn't enough for a whole line
        // We need to read the data

        BuffLen = strlen(FileLines1);

        // sanity check
        if (BuffLen >= sizeof(FileLines1)) {
            return NULL;
        }

        pEndOfBuff = &FileLines1[0] + BuffLen;
        ReadLen = sizeof(FileLines1) - BuffLen;

        ZeroMemory(pEndOfBuff, ReadLen);
        if (ReadFile(hFile, pEndOfBuff, ReadLen - 1, &BytesRead, NULL)) {
            pEOL = strchr(FileLines1, '\n');
        }
    }

    if (pEOL) {
        FileLine[0] = 0;

        strncat(FileLine,FileLines1, (ULONG) (pEOL - &FileLines1[0]));
        strcpy(FileLines2, pEOL+1);
        strcpy(FileLines1, FileLines2);
        pEOL = strchr(FileLine, '\n');
        if (pEOL) *pEOL = 0;
        return FileLine;
    }

    return NULL;
}


BOOL
GetPoolTagDescriptionFromTxtFile(
    IN ULONG PoolTag,
    PDEBUG_POOLTAG_DESCRIPTION pDetails
    )
//
// Get pool tag description from pooltag.txt file, if present, in current dir
//
{
    const LPSTR PoolTagTxtFile = "triage\\pooltag.txt";
    static CHAR TagDesc[MAX_PATH];
    CHAR *pPoolTagTxtFile, ExeDir[MAX_PATH+50];  // make it big enough to append PoolTagTxtFile
    PSTR pCurrLine, pDescription, pOwner;
    CHAR PoolTagStr[5], PossiblePoolTag[5];
    ULONG i=0;
    ULONG Match, PrevMatch;
    BOOL bRestart = TRUE;
    HANDLE hFile;


    // Get the directory the debugger executable is in.
    if (!GetModuleFileName(NULL, ExeDir, MAX_PATH)) {
        // Error.  Use the current directory.
        strcpy(ExeDir, ".");
    } else {
        // Remove the executable name.
        PCHAR pszTmp = strrchr(ExeDir, '\\');
        if (pszTmp)
        {
            *pszTmp = 0;
        }
    }
    strcat(ExeDir, "\\");
    strcat(ExeDir, PoolTagTxtFile);
    pPoolTagTxtFile = &ExeDir[0];
    hFile = CreateFile(pPoolTagTxtFile, GENERIC_READ, FILE_SHARE_READ,
                       NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN,
                       NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        return FALSE;
    }

    *((PULONG) PoolTagStr) = PoolTag;
    PoolTagStr[4] = 0;

    PrevMatch = 0;
    while (pCurrLine = GetNextLine(hFile, bRestart)) {
        bRestart = FALSE;
        ++i;
        while (*pCurrLine == ' ' || *pCurrLine == '\t') {
            ++pCurrLine;
        }

        if (*pCurrLine == '%' || !_strnicmp(pCurrLine, "rem", 3)) {
            // a commented line
            continue;
        }

        strncpy(PossiblePoolTag, pCurrLine, 4);
        PossiblePoolTag[4] = 0;

        pCurrLine +=4;
        while (*pCurrLine == ' ' || *pCurrLine == '\t') {
            ++pCurrLine;
        }
        if (*pCurrLine == '-' && (Match = MatchPoolTag(PossiblePoolTag, PoolTagStr))) {
            // Its a match
            ++pCurrLine;
            while (*pCurrLine == ' ' || *pCurrLine == '\t') {
                ++pCurrLine;
            }
            if (Match > PrevMatch && (Match > 1)) // Match at least 2 chars
            {
                PrevMatch = Match;
                FindOwnerAndComponent(pCurrLine, pDetails);
            }
            if (Match == 4)
            {
                // Found exact match
                break;
            }
        }
    }

    CloseHandle(hFile);

    return TRUE;

}

BOOL
GetPoolTagDescription(
    IN ULONG PoolTag,
    OUT PDEBUG_POOLTAG_DESCRIPTION pDescription
    )
{
    ULONG lo;

    return GetPoolTagDescriptionFromTxtFile(PoolTag, pDescription);
}

EXTENSION_API ( GetPoolTagDescription ) (
     IN ULONG PoolTag,
     OUT PDEBUG_POOLTAG_DESCRIPTION pDescription
     )
{
    if (!pDescription || (pDescription->SizeOfStruct != sizeof(DEBUG_POOLTAG_DESCRIPTION))) {
        return E_INVALIDARG;
    }
    if (GetPoolTagDescription(PoolTag, pDescription)) {
        return S_OK;
    }
    return E_FAIL;
}
