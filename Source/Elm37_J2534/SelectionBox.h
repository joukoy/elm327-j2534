#pragma once
#include "afxwin.h"
#include "afxcmn.h"

#include "resource.h"
#include "elm327_loader.h"
#include <set>
#include <string>

#include <windowsx.h>
#include <setupapi.h>
#include <devguid.h>
#include <cfgmgr32.h>   //CM_Get_Parent
#include <vector>

// SelectionBox dialog

class CSelectionBox : public CDialog
{
	DECLARE_DYNAMIC(CSelectionBox)

public:
	//CSelectionBox(std::set<cPassThruInfo>& connectedList, CWnd* pParent = NULL);   // standard constructor
	CSelectionBox(std::set<int>& connectedList, CWnd* pParent = NULL);   // standard constructor
	virtual ~CSelectionBox();

// Dialog Data
	enum { IDD = IDD_DIALOG1 };
protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support

	DECLARE_MESSAGE_MAP()
	CString bauds[16] =
	{
		CString("300"),
		CString("600"),
		CString("1200"),
		CString("2400"),
		CString("4800"),
		CString("9600"),
		CString("19200"),
		CString("38400"),
		CString("57600"),
		CString("115200"),
		CString("230400"),
		CString("460800"),
		CString("500000"),
		CString("921600"),
		CString("1000000")
	};

private:
	// Reference to the connectedList. The caller has already scanned for devices and
	// determined that it is worth popping-up a dialog box to select from these
//	std::set<cPassThruInfo>& connectedList;
	struct SerialPortInfo {
		std::string portName;      // e.g., "COM3"
		std::string friendlyName;  // e.g., "USB-SERIAL CH340 (COM3)"
	};
	std::vector<SerialPortInfo> ListSerialPorts();
	cPassThruInfo * sel; //??
	CString cstrDebugFile;
	CString cstrLogFolder;
	CString cstrWriteModifierFile;
	//CString cstrDeviceName;
	//CListCtrl m_listview;
	CEdit m_logfolder;
	CButton m_button_ok;
	CComboBox m_jDevice;
	CComboBox m_baudrate;
//	CButton m_button_config;

//	void DoPopulateRegistryListbox();

public:

	virtual BOOL OnInitDialog();
	void OnBnClickedOk();
	afx_msg void OnLvnItemchangedList1(NMHDR *pNMHDR, LRESULT *pResult);
	afx_msg void OnHdnItemdblclickList1(NMHDR *pNMHDR, LRESULT *pResult);
	afx_msg void OnBnClickedBrowse();

//	cPassThruInfo * GetSelectedPassThru();
	CString GetDebugFilename();
	//CString GetDeviceName();
	//int GetComPort();
	CEdit m_msglogfile;
	CButton m_AppendMsgLog;
	afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
	afx_msg void OnCbnSelchangeComboDevice();
	afx_msg void OnCbnSelchangeComboBaudrate();
	std::vector<SerialPortInfo> SerialPorts;
};
