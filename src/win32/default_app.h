/**
 * Copyright (C) 2010 syndicode
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 **/

#ifndef _DEFAULT_APP_H
#define _DEFAULT_APP_H

#include <memory>
#include <Windows.h>

 // abstract base class

class CWinDefaultApp
{
public:
	static std::unique_ptr<CWinDefaultApp> Factory();

	void CheckDefaultNfoViewer(HWND a_hwnd);
	void CheckDefaultNfoViewerInteractive(HWND a_hwnd);
	bool CanCheckDefaultNfoViewer();

	CWinDefaultApp() = delete;
	virtual ~CWinDefaultApp() {}
protected:
	CWinDefaultApp(const std::wstring& a_appRegistryName, const std::wstring& a_fileExtension) :
		m_appRegistryName(a_appRegistryName), m_extension(a_fileExtension), m_noSuchProgName(false)
	{ }

	enum class MakeDefaultResult {
		SUCCEEDED,
		FAILED,
		NOT_SUPPORTED
	};

	bool GotNoSuchProgramName() const { return m_noSuchProgName; }

	virtual bool IsDefault() = 0;
	virtual MakeDefaultResult MakeDefault() = 0;

	const std::wstring m_appRegistryName;
	const std::wstring m_extension;

	bool m_noSuchProgName;
};

// class for Windows 8 and later because wtf Microsoft

class CWin8DefaultApp : public CWinDefaultApp
{
public:
	CWin8DefaultApp(const std::wstring&, const std::wstring&);

protected:
	bool IsDefault() override;
	MakeDefaultResult MakeDefault() override;
};

#endif /* !_DEFAULT_APP_H */
