// SelectionBox.cpp : implementation file
//

#include "stdafx.h"

#include <set>
#include <fstream>
#include <sstream>
#include <string>
#include <tchar.h>
#include <iostream>
#include <vector>
#include <stdio.h>
#include "j2534-elm327.h"
#include "SelectionBox.h"
//using namespace std;
extern std::string PathOfDll;
extern std::string ComPort;
extern int Baudrate;
std::string thisDllDirPath();

#pragma comment(lib, "setupapi.lib")

// SelectionBox dialog

//DWORD WINAPI FileMonitor(_In_  LPVOID lpParameter);

IMPLEMENT_DYNAMIC(CSelectionBox, CDialog)

//std::vector<CString> devList;

//CSelectionBox::CSelectionBox(std::set<cPassThruInfo>& connectedList, CWnd* pParent /*=NULL*/)

CSelectionBox::CSelectionBox(std::set<int>& connectedList, CWnd* pParent /*=NULL*/)
: CDialog(CSelectionBox::IDD, pParent), sel(NULL)
{

}

CSelectionBox::~CSelectionBox()
{
}

void CSelectionBox::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	DDX_Control(pDX, IDOK, m_button_ok);
	//DDX_Control(pDX, IDC_LIST1, m_listview);
	//	DDX_Control(pDX, IDC_J2534REGINFO, m_detailtext);
	//	DDX_Control(pDX, IDC_BUTTON1, m_button_config);
	DDX_Control(pDX, IDC_EDIT1, m_logfolder);
	DDX_Control(pDX, IDC_COMBO_DEVICE, m_jDevice);
	DDX_Control(pDX, IDC_COMBO_BAUDRATE, m_baudrate);
}


BEGIN_MESSAGE_MAP(CSelectionBox, CDialog)
	ON_BN_CLICKED(IDOK, &CSelectionBox::OnBnClickedOk)
//	ON_NOTIFY(LVN_ITEMCHANGED, IDC_LIST1, &CSelectionBox::OnLvnItemchangedList1)
	ON_NOTIFY(HDN_ITEMDBLCLICK, 0, &CSelectionBox::OnHdnItemdblclickList1)
//	ON_BN_CLICKED(IDC_BUTTON1, &CSelectionBox::OnBnClickedConfig)
	ON_BN_CLICKED(IDC_BUTTON2, &CSelectionBox::OnBnClickedBrowse)
	ON_WM_SYSCOMMAND()
	ON_CBN_SELCHANGE(IDC_COMBO_DEVICE, &CSelectionBox::OnCbnSelchangeComboDevice)
	ON_CBN_SELCHANGE(IDC_COMBO_BAUDRATE, &CSelectionBox::OnCbnSelchangeComboBaudrate)
END_MESSAGE_MAP()

// SelectionBox message handlers
BOOL CSelectionBox::OnInitDialog()
{
	FILE* inf;
	std::string line;
	CDialog::OnInitDialog();
	ShowWindow(SW_HIDE);
	PathOfDll = thisDllDirPath();

	SYSTEMTIME LocalTime;
	GetLocalTime(&LocalTime);

	CreateDirectoryA((PathOfDll + "Logs").c_str(), NULL);
	CString cstrPath;
	cstrPath.Format(_T("%SLogs"), PathOfDll.c_str());
	m_logfolder.SetWindowText(cstrPath);


	char iniPath[MAX_PATH];
	sprintf(iniPath, "%sSettings.ini", PathOfDll.c_str());
	fopen_s(&inf, iniPath, "rt");
	if (inf != NULL)
	{
#define SEPARATORS "=\n"
		while (!feof(inf))
		{
			char line[512];
			char* context;
			line[0] = 0;
			fgets(line, sizeof(line), inf);
			char* token = strtok_s(line, SEPARATORS, &context);
			if (token == NULL) continue;

			if (0 == _stricmp(token, "COMPORT"))
			{
				token = strtok_s(NULL, SEPARATORS, &context);
				if (token == NULL) continue;
				ComPort = token;
			}
			if (0 == _stricmp(token, "BAUDRATE"))
			{
				token = strtok_s(NULL, SEPARATORS, &context);
				if (token == NULL) continue;
				Baudrate = atoi(token);
			}
			if (0 == _stricmp(token, "LOGFOLDER"))
			{
				token = strtok_s(NULL, SEPARATORS, &context);
				if (token == NULL) continue;
				CString tmp(token);
				m_logfolder.SetWindowText(tmp);

			}
		}
		fclose(inf);

	}

	int sel = 0;

	SerialPorts = ListSerialPorts();

	for (int a = 0; a < SerialPorts.size(); a++)
	{
		char s[100];
		SerialPortInfo spi = SerialPorts.at(a);
		sprintf(s, "%s", spi.friendlyName.c_str());
		CString tmp(s);
		m_jDevice.AddString(tmp);
		if (ComPort == spi.portName)
		{
			sel = a;
		}
	}
	m_jDevice.SetCurSel(sel);

	sel = 0;
	for (int b = 0;b < 15;b++)
	{
		m_baudrate.AddString(bauds[b]);
		if (Baudrate == _ttoi(bauds[b]))
		{
			sel = b;
		}
	}
	m_baudrate.SetCurSel(sel);
	ShowWindow(SW_SHOW);
	BringWindowToTop();
	m_button_ok.EnableWindow(true);
	// Return TRUE unless you set focus to a control
	return TRUE;
}

// Callback for sorting the omega ListCtrl. Use the data item to get the corresponding
// class and sort by name. This will make more sense later if it's modified to sort by
// serial number
static int CALLBACK CompareByName(LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort)
{
	cPassThruInfo * item1 = (cPassThruInfo *) lParam1;
	cPassThruInfo * item2 = (cPassThruInfo *) lParam2;

	if (item1->Name < item2->Name)
	{
		return -1;
	}
	else if (item1->Name > item2->Name)
	{
		return 1;
	}
	else
	{
		return 0;
	}
}


void CSelectionBox::OnLvnItemchangedList1(NMHDR *pNMHDR, LRESULT *pResult)
{
}



void CSelectionBox::OnBnClickedOk()
{
	FILE* outf;
	SYSTEMTIME LocalTime;

	m_logfolder.GetWindowText(cstrLogFolder);
	int selectedDevice = m_jDevice.GetCurSel();
	int sel = m_jDevice.GetCurSel();
	SerialPortInfo spi = SerialPorts.at(sel);
	ComPort = spi.portName;

	sel = m_baudrate.GetCurSel();
	Baudrate = _ttoi(bauds[sel]);

	GetLocalTime(&LocalTime);
	cstrDebugFile.Format(_T("%s\\%s_%04d-%02d-%02d_%02d-%02d-%02d_%04d.txt"), cstrLogFolder, _T("UPX-CAN"), LocalTime.wYear,
		LocalTime.wMonth, LocalTime.wDay, LocalTime.wHour, LocalTime.wMinute, LocalTime.wSecond,
		LocalTime.wMilliseconds);

	char iniPath[MAX_PATH];
	sprintf(iniPath, "%sSettings.ini", PathOfDll.c_str());
	fopen_s(&outf, iniPath, "wt");
	if (outf == NULL)
	{
		//strcpy_s(mLastError, sizeof(mLastError), "Cannot open config file for writing");
		MessageBoxA(NULL, "Cannot open config file for writing", "UPX-CAN", MB_ICONERROR);
		return;
	}

	fprintf(outf, "COMPORT=%s\n", ComPort.c_str());
	fprintf(outf, "LOGFOLDER=%S\n", (LPCTSTR)cstrLogFolder);
	fprintf(outf, "BAUDRATE=%d\n", Baudrate);
	fclose(outf);

	//HANDLE bgHandle = CreateThread(0, 0, &FileMonitor, 0, 0, 0);

	//SetupDiDestroyClassImageList(&ild);

	OnOK();
}


void CSelectionBox::OnHdnItemdblclickList1(NMHDR *pNMHDR, LRESULT *pResult)
{

}


void CSelectionBox::OnBnClickedBrowse()
{
	CString cstrFileName(_T("Select folder"));
	CString cstrFilter(_T("All Files (*.*)|*.*||"));

	m_logfolder.GetWindowText(cstrFileName);
	cstrFileName.Format(L"%s\\Select folder", cstrFileName);
	INT_PTR retval;
	CFileDialog Dlg(FALSE, _T(""), cstrFileName, 0, cstrFilter, 0, 0, 1);
	retval = Dlg.DoModal();
	if (retval != IDCANCEL)
	{
	//	m_logfilename.SetWindowText(Dlg.GetFileName());
	//	string Logfile = Dlg.GetFolderPath(); //  +'\' + Dlg.GetFileName();
		CString thisPath = Dlg.GetPathName();
		PathRemoveFileSpecW((LPWSTR)thisPath.GetBuffer());
		if (!thisPath.IsEmpty() &&
			thisPath.GetAt(thisPath.GetLength() - 1) != '\\')
			thisPath += L"\\";
		m_logfolder.SetWindowText(thisPath);
	}
}
 /*
cPassThruInfo * CSelectionBox::GetSelectedPassThru()
{
	return sel;
}
*/
CString CSelectionBox::GetDebugFilename()
{
	return cstrDebugFile;
}


void CSelectionBox::OnSysCommand(UINT nID, LPARAM lParam)
{
	// TODO: Add your message handler code here and/or call default
	if (nID == SC_CLOSE)
	{
		OnCancel();
	}
	else
	{
		CDialog::OnSysCommand(nID, lParam);
	}
}


void CSelectionBox::OnCbnSelchangeComboDevice()
{
	// TODO: Add your control notification handler code here
}

void CSelectionBox::OnCbnSelchangeComboBaudrate()
{
	// TODO: Add your control notification handler code here
}


std::vector<CSelectionBox::SerialPortInfo> CSelectionBox::ListSerialPorts() 
{
	std::vector<CSelectionBox::SerialPortInfo> ports;

	// Get a handle to the device information set for all present devices
	HDEVINFO hDevInfo = SetupDiGetClassDevs(
		&GUID_DEVCLASS_PORTS,
		NULL,
		NULL,
		DIGCF_PRESENT
	);

	if (hDevInfo == INVALID_HANDLE_VALUE) {
		return ports; // Empty
	}

	SP_DEVINFO_DATA devInfoData;
	devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

	for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &devInfoData); ++i) {
		char friendlyName[256];
		char portName[256];

		// Get the friendly name
		if (SetupDiGetDeviceRegistryPropertyA(
			hDevInfo,
			&devInfoData,
			SPDRP_FRIENDLYNAME,
			NULL,
			(PBYTE)friendlyName,
			sizeof(friendlyName),
			NULL
		)) {
			// Now, open the registry key to get the port name
			HKEY hDeviceRegistryKey = SetupDiOpenDevRegKey(
				hDevInfo,
				&devInfoData,
				DICS_FLAG_GLOBAL,
				0,
				DIREG_DEV,
				KEY_READ
			);

			if (hDeviceRegistryKey != INVALID_HANDLE_VALUE) {
				DWORD type = 0;
				DWORD size = sizeof(portName);
				if (RegQueryValueExA(hDeviceRegistryKey, "PortName", NULL, &type, (LPBYTE)portName, &size) == ERROR_SUCCESS) {
					if (type == REG_SZ) {
						SerialPortInfo info;
						info.portName = portName;
						info.friendlyName = friendlyName;
						ports.push_back(info);
					}
				}
				RegCloseKey(hDeviceRegistryKey);
			}
		}
	}

	SetupDiDestroyDeviceInfoList(hDevInfo);
	return ports;
}


