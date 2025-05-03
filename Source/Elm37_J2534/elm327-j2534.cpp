// upxcan.cpp : Defines the initialization routines for the DLL.
//

#include "stdafx.h"
#include "j2534-elm327.h"
#include "elm327-comm.h"
#ifdef _DEBUG
#define new DEBUG_NEW
#endif

//
//TODO: If this DLL is dynamically linked against the MFC DLLs,
//		any functions exported from this DLL which call into
//		MFC must have the AFX_MANAGE_STATE macro added at the
//		very beginning of the function.
//
//		For example:
//
//		extern "C" BOOL PASCAL EXPORT ExportedFunction()
//		{
//			AFX_MANAGE_STATE(AfxGetStaticModuleState());
//			// normal function body here
//		}
//
//		It is very important that this macro appear in each
//		function, prior to any calls into MFC.  This means that
//		it must appear as the first statement within the 
//		function, even before any object variable declarations
//		as their constructors may generate calls into the MFC
//		DLL.
//
//		Please see MFC Technical Notes 33 and 58 for additional
//		details.
//

// CupxcanApp
bool loopBack = false;
BEGIN_MESSAGE_MAP(Celm327App, CWinApp)
END_MESSAGE_MAP()

void CloseLog();
void CloseDebugFile();
extern BOOL endApp;
extern HANDLE bgHandle;
extern elm327Comm elm327;
// CupxcanApp construction

Celm327App::Celm327App()
{
	// TODO: add construction code here,
	// Place all significant initialization in InitInstance
}

Celm327App::~Celm327App()
{
	// TODO: add destruction code here
	DWORD dwExitCode;
	OutputDebugString(L"Closing DLL\n");
	endApp = true;	//Ask background task to close
	OutputDebugString(L"Passthru library unloaded\n");
	CloseLog();
	CloseDebugFile();
	elm327.Stopelm327Comm();
	OutputDebugString(L"Log files closed\n");
	for (int i = 0;i < 300;i++)
	{
		//Wait until background task is terminated (max 3 secs)
		GetExitCodeThread(bgHandle, &dwExitCode);
		if (dwExitCode == STILL_ACTIVE) 
		{
			Sleep(10);
		}
		else
		{
			break;
		}
	}
}


// The one and only CupxcanApp object

Celm327App theApp;


// CupxcanApp initialization

BOOL Celm327App::InitInstance()
{
	CWinApp::InitInstance();
	return TRUE;
}

