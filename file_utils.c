/****************************************************************************/
/*                                                                          */
/*   metapad 3.6+                                                           */
/*                                                                          */
/*   Copyright (C) 2021 SoBiT Corp                                          */
/*   Copyright (C) 2013 Mario Rugiero                                       */
/*   Copyright (C) 1999-2011 Alexander Davidson                             */
/*                                                                          */
/*   This program is free software: you can redistribute it and/or modify   */
/*   it under the terms of the GNU General Public License as published by   */
/*   the Free Software Foundation, either version 3 of the License, or      */
/*   (at your option) any later version.                                    */
/*                                                                          */
/*   This program is distributed in the hope that it will be useful,        */
/*   but WITHOUT ANY WARRANTY; without even the implied warranty of         */
/*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          */
/*   GNU General Public License for more details.                           */
/*                                                                          */
/*   You should have received a copy of the GNU General Public License      */
/*   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */
/*                                                                          */
/****************************************************************************/

#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0400

#include <windows.h>
#include <tchar.h>

#ifdef USE_RICH_EDIT
#include <richedit.h>
#include <commdlg.h>
#endif

#include "include/globals.h"
#include "include/consts.h"
#include "include/resource.h"
#include "include/tmp_protos.h"
#include "include/typedefs.h"
#include "include/strings.h"
#include "include/macros.h"

extern HANDLE globalHeap;
extern BOOL bUnix;
extern int nEncodingType;
extern HWND client;
extern HWND hdlgFind;
extern HWND hwnd;
extern LPTSTR szCaptionFile;
extern LPTSTR szDir;
extern LPTSTR szFile;
extern lpszShadow, lpszShadowRange;
extern shadowLen, shadowRngStart, shadowRngLen, shadowAlloc;

extern option_struct options;

/**
 * Calculate the size of the current file.
 *
 * @return Current file's size.
 */
DWORD CalculateFileSize(void)
{
	DWORD nBytes;
	extern HANDLE globalHeap;
	if (nEncodingType == TYPE_UTF_16 || nEncodingType == TYPE_UTF_16_BE) {
		nBytes = GetWindowTextLength(client) * 2 + SIZEOFBOM_UTF_16;
	}
	else if (nEncodingType == TYPE_UTF_8) {
		nBytes = GetWindowTextLength(client);
#ifdef UNICODE
		if (sizeof(TCHAR) > 1) {
			/* TODO		This can get quite expensive for very large files. Future alternatives:
				- Count chars instead
				- Ability to disable this in options
				- Do this in the background with ratelimiting
				- Keep a local copy of the text buffer
			*/
			LPTSTR szBuffer = GetShadowBuffer(&nBytes);
			nBytes = WideCharToMultiByte(CP_UTF8, 0, szBuffer, nBytes, NULL, 0, NULL, NULL);
			for ( ; *szBuffer; szBuffer++)
				if (*szBuffer == _T('\r'))
					nBytes--;
		}
#endif
		nBytes += SIZEOFBOM_UTF_8;
	}
	else {
		nBytes = GetWindowTextLength(client) - (bUnix ? (SendMessage(client, EM_GETLINECOUNT, 0, 0)) - 1 : 0);
		//BUG! Wrapped lines erroneously decrease byte count (LE)!
	}
	return nBytes;
}

void SetFileFormat(int nFormat)
{
	switch (nFormat) {
	case FILE_FORMAT_DOS:
		SendMessage(hwnd, WM_COMMAND, MAKEWPARAM(ID_DOS_FILE, 0), 0);
		break;
	case FILE_FORMAT_UNIX:
		SendMessage(hwnd, WM_COMMAND, MAKEWPARAM(ID_UNIX_FILE, 0), 0);
		break;
	case FILE_FORMAT_UTF8:
		SendMessage(hwnd, WM_COMMAND, MAKEWPARAM(ID_UTF8_FILE, 0), 0);
		break;
	case FILE_FORMAT_UTF8_UNIX:
		SendMessage(hwnd, WM_COMMAND, MAKEWPARAM(ID_UTF8_UNIX_FILE, 0), 0);
		break;
	case FILE_FORMAT_UNICODE:
		SendMessage(hwnd, WM_COMMAND, MAKEWPARAM(ID_UNICODE_FILE, 0), 0);
		break;
	case FILE_FORMAT_UNICODE_BE:
		SendMessage(hwnd, WM_COMMAND, MAKEWPARAM(ID_UNICODE_BE_FILE, 0), 0);
		break;
	case FILE_FORMAT_BINARY:
		SendMessage(hwnd, WM_COMMAND, MAKEWPARAM(ID_BINARY_FILE, 0), 0);
		break;
	}
}

int FixShortFilename(LPCTSTR szSrc, TCHAR *szDest)
{
	WIN32_FIND_DATA FindFileData;
	HANDLE hHandle;
	TCHAR sDir[MAXFN], sName[MAXFN];
	int nDestPos=0, nSrcPos=0, i;
	BOOL bOK = TRUE;

	// Copy drive letter over
	if (szSrc[1] == _T(':')) {
		szDest[nDestPos++] = szSrc[nSrcPos++];
		szDest[nDestPos++] = szSrc[nSrcPos++];
	}

	while (szSrc[nSrcPos]) {
		// If the next TCHAR is '\' we are starting from the root and want to add '\*' to sDir.
		// Otherwise we are doing relative search, so we just append '*' to sDir
		if (szSrc[nSrcPos]==_T('\\')) {
			szDest[nDestPos++] = szSrc[nSrcPos++];

			if (szSrc[nSrcPos] == _T('\\')) { // get UNC server name
				szDest[nDestPos++] = szSrc[nSrcPos++];

				while (szSrc[nSrcPos] && szSrc[nSrcPos - 1]!=_T('\\')) {
					szDest[nDestPos++] = szSrc[nSrcPos++];
				}
			}
		}

		_tcsncpy(sDir, szDest, nDestPos);
		sDir[nDestPos] = _T('*');
		sDir[nDestPos + 1] = _T('\0');

		for (i=0; szSrc[nSrcPos] && szSrc[nSrcPos]!=_T('\\'); i++)
			sName[i] = szSrc[nSrcPos++];
		sName[i] = _T('\0');

		hHandle = FindFirstFile(sDir, &FindFileData);
		bOK = (hHandle != INVALID_HANDLE_VALUE);
		while (bOK && lstrcmpi(FindFileData.cFileName, sName) != 0 && lstrcmpi(FindFileData.cAlternateFileName, sName) != 0)
			bOK = FindNextFile(hHandle, &FindFileData);

    	if (bOK)
    		_tcscpy(&szDest[nDestPos], FindFileData.cFileName);
    	else
    		_tcscpy(&szDest[nDestPos], sName);

		// Fix the length of szDest
		nDestPos = _tcslen(szDest);
		if (hHandle)
			FindClose(hHandle);
	}
	return !bOK;
}

void ExpandFilename(LPCTSTR szBuffer, LPTSTR* szOut)
{
	WIN32_FIND_DATA FileData;
	HANDLE hSearch;
	TCHAR szTmp[MAXFN+6] = _T("\\\\?\\");
	LPTSTR szTmpFn = szTmp+4;
	LPTSTR szTmpDir;

	if (szBuffer){
		lstrcpy(szTmpFn, szBuffer);
		FixShortFilename(szBuffer, szTmpFn);
		szBuffer = szTmpFn;
		if (szOut) SSTRCPY(*szOut, szBuffer);
	}

	if (SCNUL(szDir)[0] != _T('\0'))
		SetCurrentDirectory(szDir);

	hSearch = FindFirstFile(szTmp, &FileData);
	FREE(szCaptionFile);
	if (hSearch != INVALID_HANDLE_VALUE) {
		LPCTSTR pdest;
		szTmpDir = (LPTSTR)HeapAlloc(globalHeap, 0, (MAX(lstrlen(szBuffer)+2, lstrlen(SCNUL(szDir))+1)) * sizeof(TCHAR));
		lstrcpy(szTmpDir, SCNUL(szDir));
		pdest = _tcsrchr(szBuffer, _T('\\'));
		if (pdest) {
			int result;
			result = pdest - szBuffer + 1;
			lstrcpyn(szTmpDir, szBuffer, result);
		}
		if (szTmpDir[lstrlen(szTmpDir) - 1] != _T('\\'))
			lstrcat(szTmpDir, _T("\\"));
		szCaptionFile = (LPTSTR)HeapAlloc(globalHeap, 0, (lstrlen(szTmpDir)+lstrlen(FileData.cFileName)+1) * sizeof(TCHAR));
		szCaptionFile[0] = _T('\0');
		if (!options.bNoCaptionDir)
			lstrcat(szCaptionFile, szTmpDir);
		lstrcat(szCaptionFile, FileData.cFileName);
		FindClose(hSearch);
		if (szDir) HeapFree(globalHeap, 0, (HGLOBAL)szDir);
		szDir = szTmpDir;
	} else {
		int i = (lstrlen(SCNUL(szDir))+lstrlen(SCNUL(szFile))+1) * sizeof(TCHAR);
		szCaptionFile = (LPTSTR)HeapAlloc(globalHeap, 0, (lstrlen(SCNUL(szDir))+lstrlen(SCNUL(szFile))+1) * sizeof(TCHAR));
		szCaptionFile[0] = _T('\0');
		if (!options.bNoCaptionDir)
			lstrcat(szCaptionFile, SCNUL(szDir));
		lstrcat(szCaptionFile, SCNUL(szFile));
	}
}






LPCTSTR GetShadowBuffer(DWORD* len) {
	if (!lpszShadow || SendMessage(client, EM_GETMODIFY, 0, 0)) {
		shadowLen = GetWindowTextLength(client);
		if (shadowLen + 1 > shadowAlloc) {
			FREE(lpszShadow);
			lpszShadow = (LPTSTR) HeapAlloc(globalHeap, 0, shadowAlloc * sizeof(TCHAR));
			if (!lpszShadow) ReportLastError();
			shadowAlloc = shadowLen + 1;
		}
#ifdef USE_RICH_EDIT
		{
			TEXTRANGE tr;
			tr.chrg.cpMin = 0;
			tr.chrg.cpMax = -1;
			tr.lpstrText = lpszShadow;
			SendMessage(client, EM_GETTEXTRANGE, 0, (LPARAM)&tr);
		}
#else
		GetWindowText(client, lpszShadow, nSize);
#endif
		SendMessage(client, EM_SETMODIFY, (WPARAM) FALSE, 0);
	}
	if (len) *len = shadowLen;
	return lpszShadow;
}
LPCTSTR GetShadowRange(LONG min, LONG max, DWORD* len) {
	LPCTSTR sbuf, osbuf = lpszShadow;
	DWORD len = max - min;
	if (max <= min && max >= 0){
		if (len) *len = 0;
		return _T("");
	}
	sbuf = GetShadowBuffer();
	if (min < 0) min = 0;
	if (max < 0) len = shadowLen;
	if (!lpszShadowRange || sbuf != osbuf || min != shadowRngStart || len != shadowRngLen){
		shadowRngStart = min;
		shadowRngLen = len;
		FREE(lpszShadowRange);
		lpszShadowRange = (LPTSTR) HeapAlloc(globalHeap, 0, (len+1) * sizeof(TCHAR));
		lstrcpyn(lpszShadowRange, sbuf + min, len + 1);
	}
	if (len) *len = shadowRngLen;
	return lpszShadowRange;
}
LPCTSTR GetShadowSelection(DWORD* len) {
	CHARRANGE cr;
#ifdef USE_RICH_EDIT
	SendMessage(client, EM_EXGETSEL, 0, (LPARAM)&cr);
#else
	SendMessage(client, EM_GETSEL, (WPARAM)&cr.cpMin, (LPARAM)&cr.cpMax);
#endif
	return GetShadowRange(cr.cpMin, cr.cpMax, len);
}




BOOL DoSearch(LPCTSTR szText, LONG lStart, LONG lEnd, BOOL bDown, BOOL bWholeWord, BOOL bCase, BOOL bFromTop, LPBYTE anys)
{
	LONG lSize;
	LPCTSTR szBuffer = GetShadowBuffer();
	LPCTSTR lpszStop, lpsz, lpfs = NULL, lpszFound = NULL;
	DWORD cf, nFindLen = lstrlen(szText), f = 1;

	lSize = GetWindowTextLength(client);

	if (!szBuffer) {
		ReportLastError();
		return FALSE;
	}

	if (bDown) {
		if (nReplaceMax > -1) {
			lpszStop = szBuffer + nReplaceMax - 1;
		}
		else {
			lpszStop = szBuffer + lSize - 1;
		}
		lpsz = szBuffer + (bFromTop ? 0 : lStart + (lStart == lEnd ? 0 : 1));
	}
	else {
		lpszStop = szBuffer + (bFromTop ? lSize : lStart + (nFindLen == 1 && lStart > 0 ? -1 : 0));
		lpsz = szBuffer;
	}

	for (cf = 0; lpszStop != szBuffer && lpsz <= lpszStop - (bDown ? 0 : nFindLen-1) && (!bDown || (bDown && lpszFound == NULL)); lpsz++, f = 0) {
		if ((anys && *anys++) || *lpsz == szText[cf] || (!bCase && (TCHAR)(DWORD_PTR)CharLower((LPTSTR)(DWORD_PTR)(BYTE)*lpsz) == (TCHAR)(DWORD_PTR)CharLower((LPTSTR)(DWORD_PTR)(BYTE)szText[cf]))) {
			if (!lpfs) lpfs = lpsz;
			if (++cf == nFindLen && (!bWholeWord || ( (f || !(_istalnum(*(lpfs-1)) || *(lpfs-1) == _T('_'))) && !(*(lpsz+1) && (_istalnum(*(lpsz+1)) || *(lpsz+1) == _T('_'))) )))
				lpszFound = lpsz;
		} else if (lpfs) {
			lpfs = NULL;
			cf = 0;
			continue;
		}
		lpsz++;
	}

	if (lpszFound != NULL) {
		LONG lEnd;
		lStart = lpszFound - szBuffer;
		lEnd = lStart + nFindLen;
		SendMessage(client, EM_SETSEL, (WPARAM)lStart, (LPARAM)lEnd);
		if (!bReplacingAll)	UpdateStatus();
		return TRUE;
	}
	return FALSE;
}

BOOL SearchFile(LPCTSTR szText, BOOL bCase, BOOL bReplaceAll, BOOL bDown, BOOL bWholeWord, LPBYTE anys)
{
	BOOL bRes;
	HCURSOR hcur = SetCursor(LoadCursor(NULL, IDC_WAIT));
	CHARRANGE cr;

#ifdef USE_RICH_EDIT
	SendMessage(client, EM_EXGETSEL, 0, (LPARAM)&cr);
#else
	SendMessage(client, EM_GETSEL, (WPARAM)&cr.cpMin, (LPARAM)&cr.cpMax);
#endif
	bRes = DoSearch(szText, cr.cpMin, cr.cpMax, bDown, bWholeWord, bCase, FALSE, anys);

	if (bRes || bReplaceAll) {
		SetCursor(hcur);
		return bRes;
	}

	if (!options.bFindAutoWrap && MessageBox(hdlgFind ? hdlgFind : client, bDown ? GetString(IDS_QUERY_SEARCH_TOP) : GetString(IDS_QUERY_SEARCH_BOTTOM), STR_METAPAD, MB_OKCANCEL|MB_ICONQUESTION) == IDCANCEL) {
		SetCursor(hcur);
		return FALSE;
	}
	else if (options.bFindAutoWrap) MessageBeep(MB_OK);

	bRes = DoSearch(szText, cr.cpMin, cr.cpMax, bDown, bWholeWord, bCase, TRUE, anys);

	SetCursor(hcur);
	if (!bRes)
		MessageBox(hdlgFind ? hdlgFind : client, GetString(IDS_ERROR_SEARCH), STR_METAPAD, MB_OK|MB_ICONINFORMATION);

	return bRes;
}





DWORD StrReplaceAll(LPCTSTR szBuf, LPTSTR* szOut, DWORD* bufLen, LPCTSTR szFind, LPCTSTR szRepl, BOOL bCase, BOOL bWholeWord, LPBYTE anys){
	LONG ld;
	DWORD len, alen, lf, lr, ct = 0, cf, f = 1;
	LPTSTR src, dst, odst, pd;
	if (!szBuf || !szFind || !*szFind) return ct;
	len = alen = (bufLen && *bufLen >= 0 ? *bufLen : lstrlen(*szBuf));
	if (len < 1) return ct;
	src = szBuf;
	dst = odst = (LPTSTR)HeapAlloc(globalHeap, 0, (alen+1) * sizeof(TCHAR));
	szRepl = SCNUL(szRepl);
	lf = lstrlen(szFind);
	lr = lstrlen(szRepl);
	ld = lr - lf;
	src[len] = _T('\0');
	for (cf = 0, pd = NULL; *src; f = 0) {
		if ((anys && *anys++) || *src == szFind[cf] || (!bCase && (TCHAR)(DWORD_PTR)CharLower((LPTSTR)(DWORD_PTR)(BYTE)*src) == (TCHAR)(DWORD_PTR)CharLower((LPTSTR)(DWORD_PTR)(BYTE)szFind[cf]))) {
			if (!pd) pd = dst;
			if (++cf == lf && (!bWholeWord || ( (f || !(_istalnum(*(pd-1)) || *(pd-1) == _T('_'))) && !(*(src+1) && (_istalnum(*(src+1)) || *(src+1) == _T('_'))) ))) {
				len += ld;
				if (len > alen) {
					alen = ((len + 1) / 2) * 3;
					dst = (LPTSTR)HeapAlloc(globalHeap, 0, (alen+1) * sizeof(TCHAR));
					lstrcpyn(dst, odst, pd - odst + 1);
					pd = dst + (pd - odst);
					FREE(odst);
					odst = dst;
				}
				lstrcpyn(pd, szRepl, lr + 1);
				dst = pd + lr;
				pd = NULL;
				cf = 0;
				src++;
				ct++;
				continue;
			}
		} else if (pd) {
			pd = NULL;
			cf = 0;
			if (anys) anys--;
			continue;
		}
		*dst++ = *src++;
	}
	*dst = _T('\0');
	if (bufLen) *bufLen = len;
	if (szOut) {
		FREE(*szOut);
		*szOut = odst;
	}
	return ct;
}

DWORD ReplaceAll(HWND owner, LPTSTR szFind, LPTSTR szRepl, BOOL selection, BOOL escape, BOOL bCase, BOOL bWholeWord, LPBYTE anys, LPCTSTR header, LPCTSTR footer){
	HCURSOR hCur;
	LPCTSTR szIn;
	LPTSTR szBuf = NULL, szTmp = NULL;
	CHARRANGE cr;
	DWORD l, r, lh = 0, lf = 0;

	if (!szFind || !*szFind) return 0;
	if (!owner) owner = hwnd;
	if (header) lh = lstrlen(header);
	if (footer) lf = lstrlen(footer);
	hCur = SetCursor(LoadCursor(NULL, IDC_WAIT));
	if (selection){
		szIn = GetShadowSelection(&l);
		if (!l) {
			MessageBox(owner, GetString(IDS_NO_SELECTED_TEXT), STR_METAPAD, MB_OK|MB_ICONINFORMATION);
			return 0;
		}
	} else
		szIn = GetShadowBuffer(&l);
	if (lh || lf) {
		szTmp = (LPTSTR)HeapAlloc(globalHeap, 0, (l + lh + lf) * sizeof(TCHAR));
		if (lh) lstrcpy(szTmp, header);
		lstrcpy(szTmp + lh, szIn);
		if (lf) lstrcpy(szTmp+lh+l, footer);
		szIn = (LPCTSTR)szTmp;
	}
	l += lh+lf;
	if (escape){
		FREE(anys);
		anys = (LPBYTE)HeapAlloc(globalHeap, HEAP_ZERO_MEMORY, lstrlen(szFind)+1);
		ParseForEscapeSeqs(szFind, anys, NULL);
		ParseForEscapeSeqs(szRepl, NULL, NULL);
	}
	r = StrReplaceAll(szIn, &szBuf, &l, szFind, szRepl, bMatchCase, bWholeWord, anys);
	SendMessage(client, WM_SETREDRAW, (WPARAM)FALSE, 0);
	if (!selection) {
		cr.cpMin = 0; cr.cpMax = -1;
#ifdef USE_RICH_EDIT
		SendMessage(client, EM_EXSETSEL, 0, (LPARAM)cr);
#else
		SendMessage(client, EM_SETSEL, (WPARAM)cr.cpMin, (LPARAM)cr.cpMax);
#endif
	}
	szBuf[l - lf] = _T('\0');
	SendMessage(client, EM_REPLACESEL, (WPARAM)TRUE, (LPARAM)(szBuf+lh));
	FREE(szTmp);
	FREE(szBuf);
	FREE(anys);
	SendMessage(client, WM_SETREDRAW, (WPARAM)TRUE, 0);
	InvalidateRect(client, NULL, TRUE);
	SetCursor(hCur);
	return r;
}

