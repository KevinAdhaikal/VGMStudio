#include "StrUtils.h"
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>

struct _codepage_conversion {
    UINT cpFrom;
    UINT cpTo;
};

UINT8 CPConv_Init(CPCONV** retCPC, const char* cpFrom, const char* cpTo) {
    if (!retCPC || !cpFrom || !cpTo)
        return 0xFF;

    CPCONV* cpc = (CPCONV*)calloc(1, sizeof(CPCONV));
    if (!cpc) return 0x02;

    if (!strcmp(cpFrom, "UTF-16LE") || !strcmp(cpFrom, "UTF-16"))
        cpc->cpFrom = 1200;
    else if (!strcmp(cpFrom, "CP932") || !strcmp(cpFrom, "SJIS"))
        cpc->cpFrom = 932;
    else if (!strcmp(cpFrom, "CP1252"))
        cpc->cpFrom = 1252;
    else
        cpc->cpFrom = CP_ACP;

    if (!strcmp(cpTo, "UTF-8"))
        cpc->cpTo = CP_UTF8;
    else
        cpc->cpTo = CP_ACP;

    *retCPC = cpc;
    return 0x00;
}

void CPConv_Deinit(CPCONV* cpc) {
    if (cpc) free(cpc);
}

UINT8 CPConv_StrConvert(CPCONV* cpc, size_t* outSize, char** outStr, size_t inSize, const char* inStr) {
    if (!cpc || !inStr || !outSize || !outStr)
        return 0xFF;

    if (inSize == 0)
        inSize = strlen(inStr) + 1;

    char* result = NULL;
    int written = 0;

    if (cpc->cpFrom == 1200) {
        // UTF-16LE to UTF-8
        int wchars = (int)(inSize / 2);
        int needed = WideCharToMultiByte(cpc->cpTo, 0, (const wchar_t*)inStr, wchars, NULL, 0, NULL, NULL);
        if (needed <= 0) {
            result = (char*)calloc(1, 1);
            written = 0;
        } else {
            result = (char*)malloc(needed + 1);
            WideCharToMultiByte(cpc->cpTo, 0, (const wchar_t*)inStr, wchars, result, needed, NULL, NULL);
            result[needed] = '\0';
            written = needed;
        }
    } else {
        // MultiByte (CP932 / CP1252) -> WideChar -> UTF-8
        int wneeded = MultiByteToWideChar(cpc->cpFrom, 0, inStr, (int)inSize, NULL, 0);
        if (wneeded <= 0) {
            result = (char*)calloc(1, 1);
            written = 0;
        } else {
            wchar_t* wbuf = (wchar_t*)malloc(wneeded * sizeof(wchar_t));
            MultiByteToWideChar(cpc->cpFrom, 0, inStr, (int)inSize, wbuf, wneeded);
            int needed = WideCharToMultiByte(cpc->cpTo, 0, wbuf, wneeded, NULL, 0, NULL, NULL);
            result = (char*)malloc(needed + 1);
            WideCharToMultiByte(cpc->cpTo, 0, wbuf, wneeded, result, needed, NULL, NULL);
            result[needed] = '\0';
            free(wbuf);
            written = needed;
        }
    }

    if (*outStr == NULL) {
        *outStr = result;
    } else {
        memcpy(*outStr, result, written + 1);
        free(result);
    }

    *outSize = written;
    return 0x00;
}

#else

#include <iconv.h>
#include <errno.h>

struct _codepage_conversion {
    iconv_t cd;
};

UINT8 CPConv_Init(CPCONV** retCPC, const char* cpFrom, const char* cpTo) {
    if (!retCPC || !cpFrom || !cpTo)
        return 0xFF;

    iconv_t cd = iconv_open(cpTo, cpFrom);
    if (cd == (iconv_t)-1) {
        *retCPC = NULL;
        return 0x01;
    }

    CPCONV* cpc = (CPCONV*)malloc(sizeof(CPCONV));
    if (!cpc) {
        iconv_close(cd);
        *retCPC = NULL;
        return 0x02;
    }

    cpc->cd = cd;
    *retCPC = cpc;
    return 0x00;
}

void CPConv_Deinit(CPCONV* cpc) {
    if (!cpc)
        return;
    if (cpc->cd != (iconv_t)-1)
        iconv_close(cpc->cd);
    free(cpc);
}

UINT8 CPConv_StrConvert(CPCONV* cpc, size_t* outSize, char** outStr, size_t inSize, const char* inStr) {
    if (!cpc || !inStr || !outSize || !outStr)
        return 0xFF;

    if (inSize == 0)
        inSize = strlen(inStr) + 1;

    size_t inBytesLeft = inSize;
    char* inBuf = (char*)inStr;

    size_t outBufAlloc = inSize * 4 + 4;
    size_t outBytesLeft = outBufAlloc;
    char* outAllocated = (char*)malloc(outBufAlloc);
    if (!outAllocated)
        return 0x02;

    char* outBuf = outAllocated;
    iconv(cpc->cd, NULL, NULL, NULL, NULL); // reset state

    iconv(cpc->cd, &inBuf, &inBytesLeft, &outBuf, &outBytesLeft);

    size_t written = outBufAlloc - outBytesLeft;
    outAllocated[written] = '\0';

    if (*outStr == NULL) {
        *outStr = outAllocated;
    } else {
        memcpy(*outStr, outAllocated, written + 1);
        free(outAllocated);
    }

    *outSize = written;
    return 0x00;
}

#endif
