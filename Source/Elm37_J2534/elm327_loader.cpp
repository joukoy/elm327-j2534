/*
**
** Copyright (C) 2009 Drew Technologies Inc.
** Author: Joey Oravec <joravec@drewtech.com>
**
** This library is free software; you can redistribute it and/or modify
** it under the terms of the GNU Lesser General Public License as published
** by the Free Software Foundation, either version 3 of the License, or (at
** your option) any later version.
**
** This library is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
** Lesser General Public License for more details.
**
** You should have received a copy of the GNU Lesser General Public
** License along with this library; if not, <http://www.gnu.org/licenses/>.
**
*/


#include "stdafx.h"
#include <afxmt.h>

// #include "j2534_v0404.h"
#include "SelectionBox.h"
#include "elm327_debug.h"
#include "elm327_loader.h"
#include "elm327_output.h"
#include "CMessages.h"

static HINSTANCE hDLL = NULL;

static bool fLibLoaded = false;
static LARGE_INTEGER ticksPerSecond;
static LARGE_INTEGER tick;
static CRITICAL_SECTION mAutoLock;

// Vista-forward has a great function InitOnceExecuteOnce() to thread-safe execute
// a callback exactly once, but we want to support Windows XP. Instead we'll guard
// with a globally initialized CCriticalSection.
static CCriticalSection CritSectionPerformanceCounter;
static bool fPerformanceCounterInitialized = false;
static CCriticalSection CritSectionAutoLock;
static bool fAutoLockInitialized = false;

CString ReadFilterFile;
CString WriteFilterFile;
CString IoctlFile;
CString FastInitFile;
CString FiveBaudInitFile;
CString msgLofGile;
void CreateLogFile(LPCTSTR FileName, BOOL append);
HANDLE bgHandle;

// static void EnumPassThruInterfaces(std::set<cPassThruInfo> &registryList);

auto_lock::auto_lock()
{
	// ONCE -- the first time somebody creates an autolock we need to initialize the mutex
	CritSectionAutoLock.Lock();
	if (! fAutoLockInitialized)
	{
		InitializeCriticalSection(&mAutoLock);
		fAutoLockInitialized = true;
	}
	CritSectionAutoLock.Unlock();

	if (! TryEnterCriticalSection(&mAutoLock))
	{
		dtDebug(_T("Multi-threading error"));
		EnterCriticalSection(&mAutoLock);
	}
}

auto_lock::~auto_lock()
{
	LeaveCriticalSection(&mAutoLock);
}



double GetTimeSinceInit()
{
	LARGE_INTEGER tock;
    double time;

	// ONCE -- the first time somebody gets a timestamp set the timer to 0.000s
	CritSectionPerformanceCounter.Lock();
	if (! fPerformanceCounterInitialized)
	{
		QueryPerformanceFrequency(&ticksPerSecond);
		QueryPerformanceCounter(&tick);
		fPerformanceCounterInitialized = true;
	}
	CritSectionPerformanceCounter.Unlock();

	QueryPerformanceCounter(&tock);
	time = (double)(tock.QuadPart-tick.QuadPart)/(double)ticksPerSecond.QuadPart;
	return time;
}

bool shim_checkAndAutoload(void)
{
	// We're OK if a library is loaded
	if (fLibLoaded)	return true;

	
	// Define ALLOW_POPUP if you want this function to continue by scaning the registry, presenting
	// a dialog, and allowing the user to pick a J2534 DLL. Leave it undefined if you want to force
	// the app to call PassThruLoadLibrary

	// Check the registry for J2534 interfaces (Not used, should clean up)
	//std::set<cPassThruInfo> interfaceList;
	std::set<int> interfaceList;
//	EnumPassThruInterfaces(interfaceList);
	
		// Multiple interfaces? Popup a selection box!
		INT_PTR retval;
		CSelectionBox Dlg(interfaceList);

		retval = Dlg.DoModal();
		if (retval != 1)
		{
			return false;
		}
		shim_writeLogfile(Dlg.GetDebugFilename(), true);
		return true;
}



