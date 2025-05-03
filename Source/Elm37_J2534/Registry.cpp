// RegTest2.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include "stdafx.h"

/*
Original non-working code posted by 8bitcartridge
here > https://stackoverflow.com/questions/6825555/enumerating-all-subkeys-and-values-in-a-windows-registry-key
Code fixed by Vladivarius
elamon.vlad@gmail.com
27.12.2020.
*/
#define _CRT_SECURE_NO_WARNINGS

#include <Windows.h> //not sure if this is the only required thing to include
#include <stdio.h>
#include <iostream>
#include <tchar.h>
#include <vector>

using namespace std;
LSTATUS result;
struct JDevice
{
	CString Name;
	CString Dll;
};

std::vector<JDevice> DevList;

std::string thisDllFileName()
{
	CString thisPath = L"";
	WCHAR path[MAX_PATH];
	HMODULE hm;
	if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
		GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		(LPWSTR)&thisDllFileName, &hm))
	{
		GetModuleFileNameW(hm, path, MAX_PATH);
		thisPath = CString(path);
	}
	//	else if (_DEBUG) std::wcout << L"GetModuleHandle Error: " << GetLastError() << std::endl;

	//	if (_DEBUG) std::wcout << L"thisDllDirPath: [" << CStringW::PCXSTR(thisPath) << L"]" << std::endl;
	//return thisPath;
	return { thisPath.GetString(), thisPath.GetString() + thisPath.GetLength() };
}

//Not used
void EnumerateValues(HKEY hKey, DWORD numValues)
{
	//cout << "\nKeys found: " << numValues;
	DWORD dwIndex = 0;
	LPSTR valueName = new CHAR[256];
	DWORD valNameLen;
	DWORD dataType;
	BYTE databuffer[256];
	BYTE* data = databuffer;
	DWORD dataSize = 1024;
	CString name;
	CString dll;

	name.Empty();
	dll.Empty();
	for (int i = 0; i < numValues; i++)
	{
		DWORD valNameLen = 256;
		dataSize = 256;
		dataType = 0;
		result =
			RegEnumValueA(hKey,
				dwIndex,
				valueName,
				&valNameLen,
				NULL,
				&dataType,
				data,
				&dataSize);

		if (result != ERROR_SUCCESS) {
			//cout << "\nError RegEnumValue > " << result;
			return;
		}
		if (strncmp(valueName, "Name", valNameLen) == 0)
		{
			CString strTmp(data);
			name = strTmp;
		}
		if (strncmp(valueName, "FunctionLibrary", valNameLen) == 0)
		{
			CString strTmp(data);
			dll = strTmp;
		}
		dwIndex++;
	}
}

LONG GetStringRegKey(HKEY hKey, const std::string& strValueName, std::string& strValue, const std::string& strDefaultValue)
{
	strValue = strDefaultValue;
	CHAR szBuffer[512];
	DWORD dwBufferSize = sizeof(szBuffer);
	ULONG nError;
	nError = RegQueryValueExA(hKey, strValueName.c_str(), 0, NULL, (LPBYTE)szBuffer, &dwBufferSize);
	if (ERROR_SUCCESS == nError)
	{
		//strValue = szBuffer;
		std::string tmp(szBuffer);
		strValue = tmp;
	}
	return nError;
}

void ReadDeviceInfo(HKEY hKey)
{
	std::string dll;
	std::string name;
	if (GetStringRegKey(hKey, "FunctionLibrary", dll, "") == 0) 
	{		
		if (dll != thisDllFileName())
		{
			if (GetStringRegKey(hKey, "Name", name, "") != 0 || name.empty())
			{
				if (GetStringRegKey(hKey, "Vendor", name, "") != 0 || name.empty())
				{
					//Name empty and Vendor empty
					return;
				}
			}
			JDevice jd;
			jd.Name.Format(L"%S", name.c_str());
			jd.Dll.Format(L"%S", dll.c_str());
			DevList.push_back(jd);
		}
	}
}

void EnumerateSubKeys(HKEY RootKey, std::string subKey)
{
	HKEY hKey;
	DWORD cSubKeys;        //Used to store the number of Subkeys
	DWORD maxSubkeyLen;    //Longest Subkey name length
	DWORD cValues;        //Used to store the number of Subkeys
	DWORD maxValueLen;    //Longest Subkey name length
	DWORD retCode;        //Return values of calls
	
	result =
		RegOpenKeyExA(RootKey, subKey.c_str(), 0, KEY_QUERY_VALUE | KEY_ENUMERATE_SUB_KEYS | KEY_READ /*KEY_ALL_ACCESS*/, &hKey);
	if (result != ERROR_SUCCESS) {
		cout << "\nError RegOpenKeyEx > " << result;
		return;
	}

	result =
		RegQueryInfoKey(hKey,          // key handle
			NULL,            // buffer for class name
			NULL,            // size of class string
			NULL,            // reserved
			&cSubKeys,        // number of subkeys
			&maxSubkeyLen,    // longest subkey length
			NULL,            // longest class string 
			&cValues,        // number of values for this key 
			&maxValueLen,    // longest value name 
			NULL,            // longest value data 
			NULL,            // security descriptor 
			NULL);            // last write time

	if (result != ERROR_SUCCESS) {
		cout << "\nError RegQueryInfoKey > " << result;
		return;
	}

	if (cSubKeys > 0)
	{
		char currentSubkey[MAX_PATH];

		for (int i = 0; i < cSubKeys; i++) {
			DWORD currentSubLen = MAX_PATH;

			retCode = RegEnumKeyExA(hKey,    // Handle to an open/predefined key
				i,                // Index of the subkey to retrieve.
				currentSubkey,            // buffer to receives the name of the subkey
				&currentSubLen,            // size of that buffer
				NULL,                // Reserved
				NULL,                // buffer for class string 
				NULL,                // size of that buffer
				NULL);                // last write time

			if (retCode == ERROR_SUCCESS)
			{

				//char* subKeyPath = new char[currentSubLen + strlen(subKey)];
				//snprintf(subKeyPath, MAX_PATH, "%s\\%s", subKey, currentSubkey);
				std::string subKeyPath;
				subKeyPath = subKey + "\\" + currentSubkey;
				EnumerateSubKeys(RootKey, subKeyPath);
			}
		}
	}
	else
	{
		ReadDeviceInfo(hKey);
		//EnumerateValues(hKey, cValues);
	}
	RegCloseKey(hKey);
}

std::vector<JDevice> GetJDeviceList()
{
	EnumerateSubKeys(HKEY_LOCAL_MACHINE, "SOFTWARE\\PassThruSupport.04.04");
	return DevList;
}