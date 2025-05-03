// j2534-elm327.h : main header file for the j2534-arduin DLL
//

#pragma once

#ifndef __AFXWIN_H__
	#error "include 'stdafx.h' before including this file for PCH"
#endif

#include "resource.h"		// main symbols


// CupxcanApp
// See upxcan.cpp for the implementation of this class
//

class Celm327App : public CWinApp
{
public:
	Celm327App();
	~Celm327App();
// Overrides
public:
	virtual BOOL InitInstance();
	DECLARE_MESSAGE_MAP()
};
