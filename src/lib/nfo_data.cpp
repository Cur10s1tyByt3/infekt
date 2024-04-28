/**
 * Copyright (C) 2010-2014 syndicode
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

#include "stdafx.h"
#include "nfo_data.h"
#include "util.h"
#include "sauce.h"
#include "ansi_art.h"
#include <numeric>

typedef std::list<std::wstring> TLineContainer;

CNFOData::CNFOData()
	: m_lastErrorCode(NDE_NO_ERROR)
	, m_lastErrorDescr()
	, m_textContent()
	, m_utf8Content()
	, m_grid()
	, m_utf8Map()
	, m_loaded(false)
	, m_hyperLinks()
	, m_filePath()
	, m_vFileName()
	, m_sourceCharset(NFOC_AUTO)
	, m_lineWrap(false)
	, m_isAnsi(false)
	, m_ansiHintWidth(0)
	, m_ansiHintHeight(0)
	, m_colorMap()
{
}


bool CNFOData::LoadFromFile(const std::_tstring& a_filePath)
{
	FILE* l_file = nullptr;
	size_t l_fileBytes;

#ifdef _WIN32
	if (_tfopen_s(&l_file, a_filePath.c_str(), _T("rb")) != 0 || !l_file)
#else
	if (!(l_file = fopen(a_filePath.c_str(), "rb")))
#endif
	{
#ifdef _WIN32
		std::wstringstream l_errmsg;
		l_errmsg << L"Unable to open NFO file '" << a_filePath << L"' (error " << errno << L")";
#else
		std::stringstream l_errmsg;
		l_errmsg << "Unable to open NFO file '" << a_filePath << "' (error " << errno << ")";
#endif
		SetLastError(NDE_UNABLE_TO_OPEN_PHYSICAL, l_errmsg.str());

		return false;
	}

#ifdef _WIN32
	l_fileBytes = _filelength(_fileno(l_file));

	if (l_fileBytes < 0)
	{
		SetLastError(NDE_FAILED_TO_DETERMINE_SIZE, "Unable to get NFO file size.");

		fclose(l_file);
		return false;
	}
#else
	struct stat l_fst {};
	if (stat(a_filePath.c_str(), &l_fst) == 0 && S_ISREG(l_fst.st_mode))
	{
		l_fileBytes = l_fst.st_size;
	}
	else
	{
		SetLastError(NDE_FAILED_TO_DETERMINE_SIZE, "stat() on NFO file failed.");

		fclose(l_file);
		return false;
	}
#endif

	if (l_fileBytes > 1024 * 1024 * 3)
	{
		SetLastError(NDE_SIZE_EXCEEDS_LIMIT, "NFO file is too large (> 3 MB)");

		fclose(l_file);
		return false;
	}

	CAutoFreeBuffer<unsigned char> l_buf(l_fileBytes + 1);

	// copy file contents into memory buffer:
	unsigned char* l_ptr = l_buf.get();
	size_t l_totalBytesRead = 0;
	bool l_error = false;

	while (!feof(l_file))
	{
		unsigned char l_chunkBuf[8192];
		size_t l_bytesRead;

		l_bytesRead = fread_s(&l_chunkBuf, sizeof(l_chunkBuf), sizeof(unsigned char), 8192, l_file);
		if (l_bytesRead > 0)
		{
			l_totalBytesRead += l_bytesRead;

			if (l_totalBytesRead > l_fileBytes)
			{
				l_error = true;
				break;
			}

			memmove_s(l_ptr, l_buf.get() + l_fileBytes - l_ptr, l_chunkBuf, l_bytesRead);

			l_ptr += l_bytesRead;
		}
		else if (ferror(l_file))
		{
			l_error = true;
			break;
		}
	}

	// it's not defined what exactly happens if Load... is used a second time
	// on the same instance but the second load fails.

	m_filePath = a_filePath;
	m_vFileName = _T("");

	if (!l_error)
	{
		m_loaded = LoadFromMemoryInternal(l_buf.get(), l_fileBytes);
	}
	else
	{
		SetLastError(NDE_FERROR, "An error occured while reading from the NFO file.");

		m_loaded = false;
	}

	fclose(l_file);

	if (!m_loaded)
	{
		m_filePath = _T("");
	}

	return m_loaded;
}


bool CNFOData::LoadFromMemory(const unsigned char* a_data, size_t a_dataLen)
{
	m_filePath = _T("");

	m_loaded = LoadFromMemoryInternal(a_data, a_dataLen);

	return m_loaded;
}


bool CNFOData::LoadStripped(const CNFOData& a_source)
{
	if (!a_source.HasData())
	{
		return false;
	}

	ClearLastError();

	m_filePath = a_source.m_filePath;
	m_vFileName = a_source.m_vFileName;

	m_textContent = a_source.GetStrippedText();

	m_loaded = PostProcessLoadedContent();

	return m_loaded;
}


// useful in conjunction with LoadFromMemory...
void CNFOData::SetVirtualFileName(const std::_tstring& a_filePath, const std::_tstring& a_fileName)
{
	m_filePath = a_filePath;
	m_vFileName = a_fileName;
}


static void _InternalLoad_NormalizeWhitespace(std::wstring& a_text)
{
	std::wstring l_text;
	std::wstring::size_type l_prevPos = 0, l_pos;

	CUtil::StrTrimRight(a_text);

	l_text.reserve(a_text.size());

	l_pos = a_text.find_first_of(L"\r\t\xA0");

	while (l_pos != std::wstring::npos)
	{
		l_text.append(a_text, l_prevPos, l_pos - l_prevPos);

		switch (a_text[l_pos])
		{
		case L'\t':
			l_text.append(8, L' ');
			break;
		case 0xA0:
			l_text += ' ';
			break;
		}

		l_prevPos = l_pos + 1;
		l_pos = a_text.find_first_of(L"\r\t\xA0", l_prevPos);
	}

	if (l_prevPos != 0)
	{
		l_text.append(a_text.substr(l_prevPos));
		a_text.swap(l_text);
	}

	_ASSERT(a_text.find_first_of(L"\r\t\xA0") == std::wstring::npos);

	// we should only have \ns and no tabs now.

	a_text += L'\n';
}


static void _InternalLoad_SplitIntoLines(const std::wstring& a_text, size_t& a_maxLineLen, TLineContainer& a_lines)
{
	size_t l_prevPos = 0, l_pos = a_text.find(L'\n');

	a_maxLineLen = 1;

	// read lines:
	while (l_pos != std::wstring::npos)
	{
		std::wstring l_line = a_text.substr(l_prevPos, l_pos - l_prevPos);

		// trim trailing whitespace:
		CUtil::StrTrimRight(l_line);

		if (l_line.size() > a_maxLineLen)
		{
			a_maxLineLen = l_line.size();
		}

		a_lines.push_back(std::move(l_line));

		l_prevPos = l_pos + 1;
		l_pos = a_text.find(L'\n', l_prevPos);
	}

	if (l_prevPos < a_text.size() - 1)
	{
		std::wstring l_line = a_text.substr(l_prevPos);
		CUtil::StrTrimRight(l_line);
		if (l_line.size() > a_maxLineLen) a_maxLineLen = l_line.size();
		a_lines.push_back(std::move(l_line));
	}
}


static void _InternalLoad_FixLfLf(std::wstring& a_text, TLineContainer& a_lines)
{
	// fix NFOs like Crime.is.King.German.SUB5.5.DVDRiP.DivX-GWL
	// they use \n\n instead of \r\n

	int l_evenEmpty = 0, l_oddEmpty = 0;

	int i = 0;
	for (auto it = a_lines.cbegin(); it != a_lines.cend(); it++, i++)
	{
		if (it->empty())
		{
			if (i % 2) ++l_oddEmpty; else ++l_evenEmpty;
		}
	}

	int l_kill = -1;
	if (l_evenEmpty <= 0.1 * a_lines.size() && l_oddEmpty > 0.4 * a_lines.size() && l_oddEmpty < 0.6 * a_lines.size())
	{
		l_kill = 1;
	}
	else if (l_oddEmpty <= 0.1 * a_lines.size() && l_evenEmpty > 0.4 * a_lines.size() && l_evenEmpty < 0.6 * a_lines.size())
	{
		l_kill = 0;
	}

	if (l_kill >= 0)
	{
		std::wstring l_newContent; l_newContent.reserve(a_text.size());
		TLineContainer l_newLines;
		i = 0;
		for (auto it = a_lines.cbegin(); it != a_lines.cend(); it++, i++)
		{
			if (!it->empty() || i % 2 != l_kill)
			{
				l_newLines.push_back(*it);
				l_newContent += *it;
				l_newContent += L'\n';
			}
		}
		a_lines = l_newLines;
		a_text = l_newContent;
	}
}


static void _InternalLoad_FixAnsiEscapeCodes(std::wstring& a_text)
{
	// http://en.wikipedia.org/wiki/ANSI_escape_code
	// ~(?:\x1B\[|\x9B)((?:\d+;)*\d+|)([\@-\~])~

	std::wstring::size_type l_pos = a_text.find_first_of(L"\xA2\x2190"), l_prevPos = 0;
	std::wstring l_newText;

	while (l_pos != std::wstring::npos)
	{
		bool l_go = false;

		l_newText += a_text.substr(l_prevPos, l_pos - l_prevPos);

		if (a_text[l_pos] == 0xA2)
			l_go = true; // single byte CIS
		else if (a_text[l_pos] == 0x2190 && l_pos + 1 < a_text.size() && a_text[l_pos + 1] == L'[')
		{
			l_go = true;
			++l_pos;
		}

		if (l_go)
		{
			std::wstring::size_type p = l_pos + 1;
			std::wstring l_numBuf;
			wchar_t l_finalChar = 0;

			while (p < a_text.size() && ((a_text[p] >= L'0' && a_text[p] <= L'9') || a_text[p] == L';'))
			{
				l_numBuf += a_text[p];
				++p;
			}

			if (p < a_text.size()) { l_finalChar = a_text[p]; }

			if (!l_numBuf.empty() && l_finalChar > 0)
			{
				// we only honor the first number:
				std::wstring::size_type l_tmp_pos = l_numBuf.find(L';');
				if (l_tmp_pos != std::wstring::npos)
				{
					l_numBuf.erase(l_tmp_pos);
				}

				long l_number = CUtil::StringToLong(l_numBuf);

				switch (l_finalChar)
				{
				case L'C': // Cursor Forward
					if (l_number < 1) l_number = 1;
					else if (l_number > 1024) l_number = 1024;

					for (long i = 0; i < l_number; i++) l_newText += L' ';
					break;
				}

				l_pos = p;
			}
			else if (l_numBuf.empty() && ((l_finalChar >= L'A' && l_finalChar <= L'G') || l_finalChar == L'J'
				|| l_finalChar == L'K' || l_finalChar == L'S' || l_finalChar == L'T' || l_finalChar == L's' || l_finalChar == L'u'))
			{
				// skip some known, but unsupported codes
				l_pos = p;
			}
			else if (a_text[l_pos] == 0xA2)
			{
				// dont' strip \xA2 if it's not actually an escape sequence indicator
				l_newText += a_text[l_pos];
			}
		}
		else
			l_newText += a_text[l_pos];

		l_prevPos = l_pos + 1;
		l_pos = a_text.find_first_of(L"\xA2\x2190", l_prevPos);
	}

	if (l_prevPos > 0)
	{
		if (l_prevPos < a_text.size() - 1)
		{
			l_newText += a_text.substr(l_prevPos);
		}

		a_text = l_newText;
	}
}


static void _InternalLoad_WrapLongLines(TLineContainer& a_lines, size_t& a_newMaxLineLen)
{
	constexpr size_t MAX_LEN_SOFT = 100;
	constexpr size_t MAX_LEN_HARD = 2 * 80;
	constexpr size_t EQUAL_CONSECUTIVE_CHARACTERS_MAX = 3;

	constexpr auto is_line_wrapping_candidate = [](const std::wstring& line)
	{
		if (line.size() <= MAX_LEN_SOFT)
		{
			return false;
		}

		// don't touch lines with blockchars:
		if (line.find_first_of(L"\x2580\x2584\x2588\x258C\x2590\x2591\x2592\x2593") != std::wstring::npos)
		{
			return false;
		}

		return true;
	};

	// quick exit for nice & compliant NFOs
	if (std::find_if(a_lines.begin(), a_lines.end(), is_line_wrapping_candidate) == a_lines.end())
	{
		return;
	}

	constexpr auto count_leading_spaces = [](const std::wstring& line)
	{
		auto leading_spaces = line.find_first_not_of(L' ');

		if (leading_spaces == std::wstring::npos)
		{
			leading_spaces = 0;
		}

		return leading_spaces;
	};

	constexpr auto wrap_line = [](std::wstring line, size_t num_leading_spaces, auto push_backer)
	{
		bool first_run = true;

		while (line.size() > 0)
		{
			std::wstring::size_type cut_position = line.rfind(' ', MAX_LEN_SOFT);

			if (cut_position == std::wstring::npos
				|| cut_position < num_leading_spaces
				|| cut_position == 0
				|| line.size() < MAX_LEN_SOFT)
			{
				cut_position = MAX_LEN_SOFT;
			}

			std::wstring new_line = line.substr(0, cut_position);

			if (!first_run)
			{
				CUtil::StrTrimLeft(new_line);

				new_line.insert(0,
					num_leading_spaces // whitespace level of line being split
					+ 2 // some indentation to denote what happened
					, ' ');
			}

			push_backer(std::move(new_line));

			if (cut_position != MAX_LEN_SOFT)
			{
				// also erase space character
				line.erase(0, cut_position + 1);
			}
			else
			{
				// there was no space character
				line.erase(0, cut_position);
			}

			first_run = false;
		}
	};

	TLineContainer new_lines;

	for (const std::wstring& line : a_lines)
	{
		if (!is_line_wrapping_candidate(line))
		{
			new_lines.emplace_back(line);

			continue;
		}

		const auto num_leading_spaces = count_leading_spaces(line);

		if (line.size() > MAX_LEN_HARD)
		{
			goto force_wrap;
		}

		// If a line contains repeating characters (except for the initial spaces),
		// it's most likely art, not text.
		{
			wchar_t current_char = 0;
			size_t equal_consecutive_count = 0;

			for (auto it = std::next(line.begin(), num_leading_spaces), _end = line.end(); it != _end; ++it)
			{
				[[unlikely]] if (*it == current_char)
				{
					++equal_consecutive_count;

					if (equal_consecutive_count > EQUAL_CONSECUTIVE_CHARACTERS_MAX)
					{
						break;
					}
				}
				else
				{
					current_char = *it;
					equal_consecutive_count = 1;
				}
			}

			if (equal_consecutive_count > EQUAL_CONSECUTIVE_CHARACTERS_MAX)
			{
				new_lines.emplace_back(line);

				continue;
			}
		}

	force_wrap:

		wrap_line(line, num_leading_spaces, [&](auto&& new_line) {new_lines.emplace_back(new_line); });
	}

	a_lines = new_lines;
	a_newMaxLineLen = std::accumulate(new_lines.begin(), new_lines.end(), size_t(0),
		[](size_t carry, auto& line) {
			return std::max(line.size(), carry);
		});
}


bool CNFOData::LoadFromMemoryInternal(const unsigned char* a_data, size_t a_dataLen)
{
	bool l_loaded = false;
	size_t l_dataLen = a_dataLen;

	ClearLastError();

	m_isAnsi = false; // modifying this state here (and in ReadSAUCE) is not nice

	if (!ReadSAUCE(a_data, l_dataLen))
	{
		return false;
	}

	switch (m_sourceCharset)
	{
	case NFOC_AUTO:
		l_loaded = TryLoad_UTF8Signature(a_data, l_dataLen);
		if (!l_loaded) l_loaded = TryLoad_UTF16LE(a_data, l_dataLen, EApproach::EA_TRY);
		if (!l_loaded) l_loaded = TryLoad_UTF16BE(a_data, l_dataLen);
		if (HasFileExtension(_T(".nfo")) || HasFileExtension(_T(".diz")))
		{
			// other files are likely ANSI art, so only try non-BOM-UTF-8 for .nfo and .diz
			if (!l_loaded) l_loaded = TryLoad_UTF8(a_data, l_dataLen, EApproach::EA_TRY);
		}
		if (!l_loaded) l_loaded = TryLoad_CP437(a_data, l_dataLen, EApproach::EA_TRY);
		break;
	case NFOC_UTF16:
		l_loaded = TryLoad_UTF16LE(a_data, l_dataLen, EApproach::EA_FALSE);
		if (!l_loaded) l_loaded = TryLoad_UTF16BE(a_data, l_dataLen);
		break;
	case NFOC_UTF8_SIG:
		l_loaded = TryLoad_UTF8Signature(a_data, l_dataLen);
		break;
	case NFOC_UTF8:
		l_loaded = TryLoad_UTF8(a_data, l_dataLen, EApproach::EA_FALSE);
		break;
	case NFOC_CP437:
		l_loaded = TryLoad_CP437(a_data, l_dataLen, EApproach::EA_FALSE);
		break;
	case NFOC_WINDOWS_1252:
		l_loaded = TryLoad_CP252(a_data, l_dataLen);
		break;
	case NFOC_CP437_IN_UTF8:
		l_loaded = TryLoad_UTF8(a_data, l_dataLen, EApproach::EA_FORCE);
		break;
	case NFOC_CP437_IN_UTF16:
		l_loaded = TryLoad_UTF16LE(a_data, l_dataLen, EApproach::EA_FORCE);
		break;
	case NFOC_CP437_IN_CP437:
		l_loaded = TryLoad_CP437(a_data, l_dataLen, EApproach::EA_FORCE);
		break;
	case NFOC_CP437_STRICT:
		l_loaded = TryLoad_CP437_Strict(a_data, l_dataLen);
		break;
	default:
		break;
	}

	if (l_loaded)
	{
		return PostProcessLoadedContent();
	}
	else
	{
		if (!IsInError())
		{
			SetLastError(NDE_ENCODING_PROBLEM, "There appears to be a charset/encoding problem.");
		}

		m_textContent.clear();

		return false;
	}
}


bool CNFOData::PostProcessLoadedContent()
{
	size_t l_maxLineLen;
	TLineContainer l_lines;
	bool l_ansiError = false;

	m_colorMap.reset();

	if (!m_isAnsi)
	{
		if (m_sourceCharset != NFOC_CP437_STRICT)
		{
			_InternalLoad_NormalizeWhitespace(m_textContent);
			_InternalLoad_FixAnsiEscapeCodes(m_textContent);
		}

		_InternalLoad_SplitIntoLines(m_textContent, l_maxLineLen, l_lines);

		if (m_sourceCharset != NFOC_CP437_STRICT)
		{
			_InternalLoad_FixLfLf(m_textContent, l_lines);
		}

		if (m_lineWrap)
		{
			_InternalLoad_WrapLongLines(l_lines, l_maxLineLen);
		}
	}
	else
	{
		if (m_ansiHintWidth == 0)
		{
			m_ansiHintWidth = 80;
		}

		try
		{
			CAnsiArt l_ansiArtProcessor(WIDTH_LIMIT, LINES_LIMIT, m_ansiHintWidth, m_ansiHintHeight);

			l_ansiError = !l_ansiArtProcessor.Parse(m_textContent) || !l_ansiArtProcessor.Process();

			if (!l_ansiError)
			{
				l_lines = l_ansiArtProcessor.GetLines();
				l_maxLineLen = l_ansiArtProcessor.GetMaxLineLength();
				m_textContent = l_ansiArtProcessor.GetAsClassicText();
				m_colorMap = l_ansiArtProcessor.GetColorMap();
			}
		}
		catch (const std::exception& ex)
		{
			l_ansiError = true;
			(void)ex;
		}
	}

	// copy lines to grid:
	m_grid.reset();
	m_utf8Map.clear();
	m_hyperLinks.clear();
	m_utf8Content.clear();

	if (l_ansiError)
	{
		SetLastError(NDE_ANSI_INTERNAL,
			"Internal problem during ANSI processing. This could be a bug, please file a report and attach the file you were trying to open.");

		return false;
	}

	if (l_lines.size() == 0 || l_maxLineLen == 0)
	{
		SetLastError(NDE_EMPTY_FILE, "Unable to find any lines in this file.");

		return false;
	}

	if (l_maxLineLen > WIDTH_LIMIT)
	{
		std::stringstream l_errmsg;
		l_errmsg << "This file contains a line longer than " << WIDTH_LIMIT << " chars. To prevent damage and lock-ups, we do not load it.";

		SetLastError(NDE_MAXIMUM_LINE_LENGTH_EXCEEDED, l_errmsg.str());

		return false;
	}

	if (l_lines.size() > LINES_LIMIT)
	{
		std::stringstream l_errmsg;
		l_errmsg << "This file contains more than " << LINES_LIMIT << " lines. To prevent damage and lock-ups, we do not load it.";

		SetLastError(NDE_MAXIMUM_NUMBER_OF_LINES_EXCEEDED, l_errmsg.str());

		return false;
	}

	// allocate mem:
	m_grid = std::make_unique<TwoDimVector<wchar_t>>(l_lines.size(), l_maxLineLen, 0);

	// vars for hyperlink detection:
	std::wstring l_prevLinkUrl;
	int l_maxLinkId = 1;
	std::multimap<size_t, CNFOHyperLink>::iterator l_prevLinkIt = m_hyperLinks.end();

	// go through line by line:
	size_t i = 0; // line (row) index
	for (TLineContainer::const_iterator it = l_lines.cbegin(); it != l_lines.cend(); it++, i++)
	{
		int l_lineLen = static_cast<int>(it->length());

#pragma omp parallel for
		for (int j = 0; j < l_lineLen; j++)
		{
			(*m_grid)[i][j] = (*it)[j];
		}

		const std::string l_utf8Line = CUtil::FromWideStr(*it, CP_UTF8);

		const char* const p_start = l_utf8Line.c_str();
		const char* p = p_start;
		size_t char_index = 0;
		while (p != nullptr && *p)
		{
			wchar_t w_at = (*m_grid)[i][char_index++];
			const char* p_char = p;
			const char* p_next = utf8_find_next_char(p);

			if (m_utf8Map.find(w_at) == m_utf8Map.end())
			{
				m_utf8Map[w_at] = (p_next != nullptr ? std::string(p_char, static_cast<size_t>(p_next - p)) : std::string(p_char));
			}

			p = p_next;
		}

		// find hyperlinks:
		if (/* m_bFindHyperlinks == */true)
		{
			size_t l_linkPos = (size_t)-1, l_linkLen;
			bool l_linkContinued;
			std::wstring l_url, l_prevUrlCopy = l_prevLinkUrl;
			size_t l_offset = 0;

			while (CNFOHyperLink::FindLink(*it, l_offset, l_linkPos, l_linkLen, l_url, l_prevUrlCopy, l_linkContinued))
			{
				int l_linkID = (l_linkContinued ? l_maxLinkId - 1 : l_maxLinkId);

				std::multimap<size_t, CNFOHyperLink>::iterator l_newItem =
					m_hyperLinks.emplace(i, CNFOHyperLink(l_linkID, l_url, i, l_linkPos, l_linkLen));

				if (!l_linkContinued)
				{
					l_maxLinkId++;
					l_prevLinkUrl = l_url;
					l_prevLinkIt = l_newItem;
				}
				else
				{
					(*l_newItem).second.SetHref(l_url);

					if (l_prevLinkIt != m_hyperLinks.end())
					{
						_ASSERT((*l_prevLinkIt).second.GetLinkID() == l_linkID);
						// update href of link's first line:
						(*l_prevLinkIt).second.SetHref(l_url);
					}

					l_prevLinkUrl.clear();
				}

				l_prevUrlCopy.clear();
			}

			if (l_linkPos == (size_t)-1)
			{
				// do not try to continue links when a line without any link on it is met.
				l_prevLinkUrl.clear();
			}
		}
	} // end of foreach line loop.

	return true;
}


bool CNFOData::TryLoad_UTF8Signature(const unsigned char* a_data, size_t a_dataLen)
{
	if (a_dataLen < 3 || a_data[0] != 0xEF || a_data[1] != 0xBB || a_data[2] != 0xBF)
	{
		// no UTF-8 signature found.
		return false;
	}

	a_data += 3;
	a_dataLen -= 3;

	if (TryLoad_UTF8(a_data, a_dataLen, EApproach::EA_TRY))
	{
		if (m_sourceCharset == NFOC_UTF8)
		{
			m_sourceCharset = NFOC_UTF8_SIG;
		}

		return true;
	}

	return false;
}


bool CNFOData::TryLoad_UTF8(const unsigned char* a_data, size_t a_dataLen, EApproach a_fix)
{
	if (utf8_validate((const char*)a_data, a_dataLen, nullptr))
	{
		const std::string l_utf((const char*)a_data, a_dataLen);

		// the following is a typical collection of characters that indicate
		// a CP437 representation that has been (very badly) UTF-8 encoded
		// using an "ISO-8559-1 to UTF-8" or similar routine.
		if (a_fix == EApproach::EA_FORCE || (a_fix == EApproach::EA_TRY && (l_utf.find("\xC3\x9F") != std::string::npos || l_utf.find("\xC3\x8D") != std::string::npos)
			/* one "Eszett" or LATIN CAPITAL LETTER I WITH ACUTE (horizontal double line in 437) */ &&
			(l_utf.find("\xC3\x9C\xC3\x9C") != std::string::npos || l_utf.find("\xC3\x9B\xC3\x9B") != std::string::npos)
			/* two consecutive 'LATIN CAPITAL LETTER U WITH DIAERESIS' or 'LATIN CAPITAL LETTER U WITH CIRCUMFLEX' */ &&
			(l_utf.find("\xC2\xB1") != std::string::npos || l_utf.find("\xC2\xB2") != std::string::npos)
			/* 'PLUS-MINUS SIGN' or 'SUPERSCRIPT TWO' */)
			/* following is more detection stuff for double-encoded CP437 NFOs that were converted to UTF-8 */
			|| (a_fix == EApproach::EA_TRY && (l_utf.find("\xC2\x9A\xC2\x9A") != std::string::npos && l_utf.find("\xC3\xA1\xC3\xA1") != std::string::npos))
			)
		{
			std::vector<char> l_cp437(a_dataLen + 1);
			size_t l_newLength = utf8_to_latin9(l_cp437.data(), (const char*)a_data, a_dataLen);

			if (l_newLength > 0 && TryLoad_CP437((unsigned char*)l_cp437.data(), l_newLength, EApproach::EA_TRY))
			{
				m_sourceCharset = (m_sourceCharset == NFOC_CP437_IN_CP437 ? NFOC_CP437_IN_CP437_IN_UTF8 : NFOC_CP437_IN_UTF8);

				return true;
			}

			return false;
		}
		else
		{
			m_textContent = CUtil::ToWideStr(l_utf, CP_UTF8);
		}

		m_sourceCharset = NFOC_UTF8;

		return true;
	}

	return false;
}


#define CP437_MAP_LOW 0x7F

#include "nfo_data_cp437.inc"
#include "nfo_data_cp437_strict.inc"


bool CNFOData::TryLoad_CP437(const unsigned char* a_data, size_t a_dataLen, EApproach a_fix)
{
	bool l_containsLF = false;
	bool l_containsCRLF = false;

	// perform a detection run first:
	for (size_t i = 0; i < a_dataLen; ++i)
	{
		if (a_data[i] == '\r')
		{
		}
		else if (a_data[i] == '\n')
		{
			l_containsLF = true;

			if (i > 0 && a_data[i - 1] == '\r')
			{
				l_containsCRLF = true;
			}
		}
		else if (a_fix == EApproach::EA_TRY && i > 0
			&& a_data[0] != 0x1B // assume that ANSI art files start with ESC and that they never are double-encoded...
			)
		{
			// look for bad full blocks and shadowed full blocks or black half blocks:

			if (
				(a_data[i] == 0x9A && a_data[i - 1] == 0x9A) ||
				(a_data[i] == 0xFD && a_data[i - 1] == 0xFD) ||
				(a_data[i] == 0xE1 && a_data[i - 1] == 0xE1)
				)
			{
				a_fix = EApproach::EA_FORCE;
			}
		}

		if (l_containsCRLF && a_fix != EApproach::EA_TRY)
		{
			break;
		}
	}

	// kill trailing nullptr chars that some NFOs have so our
	// binary file check doesn't trigger.
	while (a_dataLen > 0 && a_data[a_dataLen - 1] == 0) a_dataLen--;

	// kill UTF-8 signature, if we got here, the NFO was not valid UTF-8:
	if (a_dataLen >= 3 && a_fix == EApproach::EA_TRY && a_data[0] == 0xEF && a_data[1] == 0xBB && a_data[2] == 0xBF)
	{
		a_data += 3;
		a_dataLen -= 3;
	}

	bool l_foundBinary = false;

	m_textContent.resize(a_dataLen);

#pragma omp parallel for
	for (int i = 0; i < static_cast<int>(a_dataLen); i++)
	{
		unsigned char p = a_data[i];

		if (p >= CP437_MAP_LOW)
		{
			if (a_fix != EApproach::EA_FORCE)
			{
				m_textContent[i] = map_cp437_to_unicode_high_bit[p - CP437_MAP_LOW];
			}
			else
			{
				wchar_t l_temp = map_cp437_to_unicode_high_bit[p - CP437_MAP_LOW];

				m_textContent[i] = (l_temp >= CP437_MAP_LOW ?
					map_cp437_to_unicode_high_bit[(l_temp & 0xFF) - CP437_MAP_LOW] : l_temp);
			}
		}
		else if (p <= 0x1F)
		{
			if (p == 0)
			{
				// "allow" \0 chars, e.g. for ANSI files with SAUCE record ...
				// ... also allow them for regular files, but trigger some more checks.
				m_textContent[i] = L' ';
				l_foundBinary = true;
			}
			else if (p == 0x0D && i < static_cast<int>(a_dataLen) - 1 && a_data[i + 1] == 0x0A)
			{
				m_textContent[i] = L'\r';
			}
			else if (p == 0x0D && i < static_cast<int>(a_dataLen) - 2 && a_data[i + 1] == 0x0D && a_data[i + 2] == 0x0A)
			{
				// https://github.com/syndicodefront/infekt/issues/92
				// http://stackoverflow.com/questions/6998506/text-file-with-0d-0d-0a-line-breaks
				m_textContent[i] = L' ';
			}
			else if (p == 0x0D && (!l_containsLF || l_containsCRLF))
			{
				// https://github.com/syndicodefront/infekt/issues/103
				m_textContent[i] = L'\n';
			}
			else
			{
				m_textContent[i] = map_cp437_to_unicode_control_range[p];
			}
		}
		else
		{
			_ASSERT(p > 0x1F && p < CP437_MAP_LOW);

			m_textContent[i] = (wchar_t)p;

			if (a_fix == EApproach::EA_FORCE && (p == 0x55 || p == 0x59 || p == 0x5F))
			{
				// untransliterated CAPITAL U WITH CIRCUMFLEX
				// => regular U (0x55) -- was full block (0x2588)
				// same for Y (0x59) and _ (0x5F)
				unsigned char l_next = (i < static_cast<int>(a_dataLen) - 1 ? a_data[i + 1] : 0),
					l_prev = (i > 0 ? a_data[i - 1] : 0);

				if ((l_next >= 'a' && l_next <= 'z') || (l_prev >= 'a' && l_prev <= 'z') ||
					(l_next >= 'A' && l_next <= 'Z' && l_next != 'U' && l_next != 'Y' && l_next != '_') || (l_prev >= 'A' && l_prev <= 'Z' && l_prev != 'U' && l_prev != 'Y' && l_prev != '_') ||
					(l_next >= '0' && l_next <= '9') || (l_prev >= '0' && l_prev <= '9'))
				{
					// most probably a regular 'U'/'Y'/'_'
				}
				else if (p == 0x55)
					m_textContent[i] = 0x2588;
				else if (p == 0x59)
					m_textContent[i] = 0x258C;
				else if (p == 0x5F)
					m_textContent[i] = 0x2590;
			}
		}
	}

	bool l_ansi = m_isAnsi || DetectAnsi();

	if (l_foundBinary && !l_ansi
		&& std::regex_match(m_textContent, std::wregex(L"\\s+[A-Z][a-z]+\\s+"))
		// :TODO: improve detection/discrimination of binary files (images, PDFs, PE files...) and NFO files
		)
	{
		SetLastError(NDE_UNRECOGNIZED_FILE_FORMAT, "Unrecognized file format or broken file.");

		return false;
	}
	else
	{
		m_sourceCharset = (a_fix == EApproach::EA_FORCE ? NFOC_CP437_IN_CP437 : NFOC_CP437);

		m_isAnsi = l_ansi;

		return true;
	}
}


bool CNFOData::TryLoad_CP437_Strict(const unsigned char* a_data, size_t a_dataLen)
{
	// no fuzz here, be compliant!
	// https://github.com/syndicodefront/infekt/issues/83

	bool l_error = false;

	m_textContent.resize(a_dataLen);

#pragma omp parallel for
	for (int i = 0; i < static_cast<int>(a_dataLen); i++)
	{
		unsigned char p = a_data[i];

		if (p >= CP437_MAP_LOW)
		{
			m_textContent[i] = map_cp437_strict_to_unicode_high_bit[p - CP437_MAP_LOW];
		}
		else if (p <= 0x1F)
		{
			if (p == 0)
			{
				l_error = true;
			}
			else if (p == 0x0D && i < static_cast<int>(a_dataLen) - 1 && a_data[i + 1] == 0x0A)
			{
				m_textContent[i] = L'\r';
			}
			else if (p == 0x0A && i > 0 && a_data[i - 1] == 0x0D)
			{
				m_textContent[i] = L'\n';
			}
			else
			{
				m_textContent[i] = map_cp437_strict_to_unicode_control_range[p];
			}
		}
		else
		{
			_ASSERT(p > 0x1F && p < CP437_MAP_LOW);

			m_textContent[i] = (wchar_t)p;
		}
	}

	if (l_error)
	{
		SetLastError(NDE_UNRECOGNIZED_FILE_FORMAT, "Unrecognized file format or broken file.");

		return false;
	}
	else
	{
		m_sourceCharset = NFOC_CP437_STRICT;

		m_isAnsi = m_isAnsi || DetectAnsi();

		return true;
	}
}


bool CNFOData::HasFileExtension(const TCHAR* a_extension) const
{
	const std::_tstring l_extension = (m_filePath.length() > 4 ? m_filePath.substr(m_filePath.length() - 4) : _T(""));

	return _tcsicmp(l_extension.c_str(), a_extension) == 0;
}


bool CNFOData::DetectAnsi() const
{
	// try to detect ANSI art files without SAUCE records:

	if (!m_isAnsi && HasFileExtension(_T(".ans")) && m_textContent.find(L"\u2190[") != std::wstring::npos)
	{
		return true;
	}

	if (!m_isAnsi && !HasFileExtension(_T(".nfo")) && m_textContent.find(L"\u2190[") != std::wstring::npos)
	{
		return std::regex_search(m_textContent, std::wregex(L"\u2190\\[[0-9;]+m"));
	}

	return false;
}


bool CNFOData::TryLoad_UTF16LE(const unsigned char* a_data, size_t a_dataLen, EApproach a_fix)
{
	if (a_dataLen < 2 || a_data[0] != 0xFF || a_data[1] != 0xFE)
	{
		// no BOM!
		return false;
	}

	// skip BOM...
	a_data += 2;

	// ...and load
	m_textContent.assign((wchar_t*)a_data, (a_dataLen - 2) / sizeof(wchar_t));

	if (m_textContent.find_first_of(L"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ") == std::wstring::npos
		&& std::string((char*)a_data, a_dataLen).find_first_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ") != std::string::npos
		) {
		// probably an invalid BOM...
		// (ex: Jimmy.Kimmel.2014.01.27.Chris.O.Donnell.720p.HDTV.x264-CROOKS)

		return false;
	}
	else if (m_textContent.find(L'\0') != std::wstring::npos)
	{
		SetLastError(NDE_UNRECOGNIZED_FILE_FORMAT, "Unrecognized file format or broken file.");

		return false;
	}

	// see comments in TryLoad_UTF8...
	if (a_fix == EApproach::EA_FORCE || (a_fix == EApproach::EA_TRY && (m_textContent.find(L'\u00DF') != std::wstring::npos || m_textContent.find(L'\u00CD') != std::wstring::npos) &&
		(m_textContent.find(L"\u00DC\u00DC") != std::wstring::npos || m_textContent.find(L"\u00DB\u00DB") != std::wstring::npos) &&
		(m_textContent.find(L"\u00B1") != std::wstring::npos || m_textContent.find(L"\u00B2") != std::wstring::npos)))
	{
		const std::string l_cp437 = CUtil::FromWideStr(m_textContent, CP_ACP);

		if (!l_cp437.empty() && TryLoad_CP437((unsigned char*)l_cp437.c_str(), l_cp437.size(), EApproach::EA_FALSE))
		{
			m_sourceCharset = NFOC_CP437_IN_UTF16;

			return true;
		}

		return false;
	}

	m_sourceCharset = NFOC_UTF16;

	return true;
}

#if !defined(_WIN32)
static inline unsigned short _byteswap_ushort(unsigned short val)
{
	return (val >> CHAR_BIT) | (val << CHAR_BIT);
}
#endif

bool CNFOData::TryLoad_UTF16BE(const unsigned char* a_data, size_t a_dataLen)
{
	if (sizeof(wchar_t) != sizeof(unsigned short))
	{
		return false;
	}

	if (a_dataLen < 2 || a_data[0] != 0xFE || a_data[1] != 0xFF)
	{
		// no BOM!
		return false;
	}

	a_dataLen -= 2;

	wchar_t* l_bufStart = (wchar_t*)(a_data + 2);
	const size_t l_numWChars = a_dataLen / sizeof(wchar_t);

	CAutoFreeBuffer<wchar_t> l_newBuf(l_numWChars + 1);

	for (size_t p = 0; p < l_numWChars; p++)
	{
		l_newBuf[p] = _byteswap_ushort(l_bufStart[p]);

		if (l_newBuf[p] == 0)
		{
			SetLastError(NDE_UNRECOGNIZED_FILE_FORMAT, "Unrecognized file format or broken file.");

			return false;
		}
	}

	if (m_textContent.find_first_of(L"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ") == std::wstring::npos
		&& std::string((char*)a_data, a_dataLen).find_first_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ") != std::string::npos
		) {
		// probably an invalid BOM...

		return false;
	}

	m_textContent.assign(l_newBuf.get(), l_numWChars);

	m_sourceCharset = NFOC_UTF16;

	return true;
}


bool CNFOData::TryLoad_CP252(const unsigned char* a_data, size_t a_dataLen)
{
	m_textContent = CUtil::ToWideStr(std::string((const char*)a_data, a_dataLen), CP_ACP);

	return (!m_textContent.empty());
}


bool CNFOData::ReadSAUCE(const unsigned char* a_data, size_t& ar_dataLen)
{
	if (ar_dataLen <= SAUCE_RECORD_SIZE)
	{
		// no SAUCE record, no error.
		return true;
	}

	SAUCE l_record{};
	bool l_incompleteRecord = false;
	size_t l_recordLength = SAUCE_RECORD_SIZE;

	memcpy(&l_record, a_data + ar_dataLen - SAUCE_RECORD_SIZE, SAUCE_RECORD_SIZE);

	// validate SAUCE header + supported features:

	if (memcmp(l_record.ID, "SAUCE", 5) != 0)
	{
		// no complete SAUCE record, look for incomplete one:

		if (ar_dataLen > SAUCE_RECORD_SIZE)
		{
			for (size_t i = 0; i < SAUCE_RECORD_SIZE - strlen("SAUCE00"); ++i)
			{
				if (0 == memcmp("SAUCE00", a_data + ar_dataLen - SAUCE_RECORD_SIZE + i, strlen("SAUCE00")))
				{
					l_incompleteRecord = true;
					l_recordLength = SAUCE_RECORD_SIZE - i;

					memset(&l_record, 0, sizeof(l_record));
					memcpy(&l_record, a_data + ar_dataLen - SAUCE_RECORD_SIZE + i, l_recordLength);

					break;
				}
			}
		}

		if (!l_incompleteRecord)
		{
			// REALLY no SAUCE record, okay_sad_face.png

			return true;
		}
	}

	if (memcmp(l_record.Version, "00", 2) != 0)
	{
		SetLastError(NDE_SAUCE_INTERNAL, "SAUCE: Unsupported file version.");

		return false;
	}

	if (l_record.DataType != SAUCEDT_CHARACTER || (l_record.FileType != SAUCEFT_CHAR_ANSI && l_record.FileType != SAUCEFT_CHAR_ASCII))
	{
		if (l_incompleteRecord && l_record.DataType == 0 && l_record.FileType == 0)
		{
			m_isAnsi = true; // wild guess!
			ar_dataLen -= l_recordLength;

			if (a_data[ar_dataLen - 1] == SAUCE_EOF)
			{
				--ar_dataLen;
			}

			return true;
		}

		if (l_record.DataType == SAUCEDT_CHARACTER && l_record.FileType == ' ' && l_record.Comments == ' ')
		{
			m_isAnsi = false;
			ar_dataLen -= l_recordLength;

			if (a_data[ar_dataLen - 1] == SAUCE_EOF)
			{
				--ar_dataLen;
			}

			return true;
		}

		m_lastErrorDescr = "SAUCE: Unsupported file format type.";

		return false;
	}

	// skip record + comments:

	size_t l_bytesToTrim = l_recordLength + (l_record.Comments > 0
		? l_record.Comments * SAUCE_COMMENT_LINE_SIZE + SAUCE_HEADER_ID_SIZE : 0);

	if (l_record.Comments > SAUCE_MAX_COMMENTS || ar_dataLen < l_bytesToTrim)
	{
		SetLastError(NDE_SAUCE_INTERNAL, "SAUCE: Bad comments definition.");

		return false;
	}

	ar_dataLen = ar_dataLen - l_bytesToTrim;

	while (ar_dataLen > 0 && a_data[ar_dataLen - 1] == SAUCE_EOF)
	{
		--ar_dataLen;
	}

	m_isAnsi = (l_record.FileType == SAUCEFT_CHAR_ANSI)
		|| (l_incompleteRecord && l_record.FileType == 0);

	if (l_record.TInfo1 > 0 && l_record.TInfo1 < WIDTH_LIMIT * 2) // width in characters
	{
		m_ansiHintWidth = l_record.TInfo1;
	}

	if (l_record.TInfo2 > 0 && l_record.TInfo2 < LINES_LIMIT * 2) // height in lines
	{
		m_ansiHintHeight = l_record.TInfo2;
	}

	return true;
}


const std::_tstring CNFOData::GetFileName() const
{
	if (!m_vFileName.empty())
	{
		return m_vFileName;
	}

#ifdef _WIN32
	const wchar_t* l_name = ::PathFindFileName(m_filePath.c_str());
	return l_name;
#else
	char* l_tmp = strdup(m_filePath.c_str());
	std::string l_result = basename(l_tmp);
	free(l_tmp);
	return l_result;
#endif
}


FILE* CNFOData::OpenFileForWritingWithErrorMessage(const std::_tstring& a_filePath)
{
	FILE* l_file = nullptr;

#ifdef _WIN32
	if (_tfopen_s(&l_file, a_filePath.c_str(), _T("wb")) != 0 || !l_file)
#else
	if (!(l_file = fopen(a_filePath.c_str(), "wb")))
#endif
	{
#ifdef _WIN32
		std::wstringstream l_errmsg;
		l_errmsg << L"Unable to NFO file '" << a_filePath << L"' for writing (error " << errno << L")";
#else
		std::stringstream l_errmsg;
		l_errmsg << "Unable to NFO file '" << a_filePath << "' for writing (error " << errno << ")";
#endif
		SetLastError(NDE_UNABLE_TO_OPEN_PHYSICAL, l_errmsg.str());

		return nullptr;
	}

	return l_file;
}


bool CNFOData::SaveToUnicodeFile(const std::_tstring& a_filePath, bool a_utf8, bool a_compoundWhitespace)
{
	FILE* l_file = OpenFileForWritingWithErrorMessage(a_filePath);

	if (!l_file)
	{
		return false;
	}

	size_t l_written = 0;
	bool l_success;

	if (a_utf8)
	{
		unsigned char l_signature[3] = { 0xEF, 0xBB, 0xBF };
		l_written += fwrite(l_signature, 1, sizeof(l_signature), l_file);

		const std::string& l_contents = (a_compoundWhitespace
			? CUtil::FromWideStr(GetWithBoxedWhitespace(), CP_UTF8)
			: GetTextUtf8());

		l_written += fwrite(l_contents.c_str(), l_contents.size(), 1, l_file);

		l_success = (l_written == 4);
	}
	else
	{
		unsigned char l_bom[2] = { 0xFF, 0xFE };
		l_written += fwrite(l_bom, 1, sizeof(l_bom), l_file);

		const std::wstring& l_contents = (a_compoundWhitespace
			? GetWithBoxedWhitespace()
			: m_textContent);

		l_written += fwrite(l_contents.c_str(), l_contents.size(), sizeof(wchar_t), l_file);

		l_success = (l_written == 4);
	}

	fclose(l_file);

	return l_success;
}


const std::string& CNFOData::GetTextUtf8()
{
	if (m_utf8Content.empty())
	{
		m_utf8Content = CUtil::FromWideStr(m_textContent, CP_UTF8);
	}

	return m_utf8Content;
}


size_t CNFOData::GetGridWidth() const
{
	return (m_grid ? m_grid->GetCols() : -1);
}


size_t CNFOData::GetGridHeight() const
{
	return (m_grid ? m_grid->GetRows() : -1);
}


wchar_t CNFOData::GetGridChar(size_t a_row, size_t a_col) const
{
	return (m_grid
		&& a_row < m_grid->GetRows()
		&& a_col < m_grid->GetCols()
		? (*m_grid)[a_row][a_col]
		: 0);
}

static std::string emptyUtf8String;

const std::string& CNFOData::GetGridCharUtf8(size_t a_row, size_t a_col) const
{
	const wchar_t grid_char = GetGridChar(a_row, a_col);

	return (grid_char > 0 ? GetGridCharUtf8(grid_char) : emptyUtf8String);
}


const std::string& CNFOData::GetGridCharUtf8(wchar_t a_wideChar) const
{
	const auto it = m_utf8Map.find(a_wideChar);

	return (it != std::end(m_utf8Map) ? it->second : emptyUtf8String);
}


const std::wstring CNFOData::GetCharsetName(ENfoCharset a_charset)
{
	switch (a_charset)
	{
	case NFOC_AUTO:
		return L"(auto)";
	case NFOC_UTF16:
		return L"UTF-16";
	case NFOC_UTF8_SIG:
		return L"UTF-8 (Signature)";
	case NFOC_UTF8:
		return L"UTF-8";
	case NFOC_CP437:
		return L"CP 437";
	case NFOC_CP437_IN_UTF8:
		return L"CP 437 (in UTF-8)";
	case NFOC_CP437_IN_UTF16:
		return L"CP 437 (in UTF-16)";
	case NFOC_CP437_IN_CP437:
		return L"CP 437 (double encoded)";
	case NFOC_CP437_IN_CP437_IN_UTF8:
		return L"CP 437 (double encoded + UTF-8)";
	case NFOC_CP437_STRICT:
		return L"CP 437 (strict mode)";
	case NFOC_WINDOWS_1252:
		return L"Windows-1252";
	default:
		break;
	}

	return L"(huh?)";
}


const std::wstring CNFOData::GetCharsetName() const
{
	if (m_isAnsi)
		return GetCharsetName(m_sourceCharset) + L" (ANSI ART)";
	else
		return GetCharsetName(m_sourceCharset);
}


/************************************************************************/
/* Compound Whitespace Code                                             */
/************************************************************************/

std::wstring CNFOData::GetWithBoxedWhitespace() const
{
	std::wstring l_result;

	for (size_t rr = 0; rr < m_grid->GetRows(); rr++)
	{
		for (size_t cc = 0; cc < m_grid->GetCols(); cc++)
		{
			const wchar_t l_tmp = (*m_grid)[rr][cc];
			l_result += (l_tmp != 0 ? l_tmp : L' ');
		}

		l_result += L"\n";
	}

	return l_result;
}


/************************************************************************/
/* Hyper Link Code                                                      */
/************************************************************************/

const CNFOHyperLink* CNFOData::GetLink(size_t a_row, size_t a_col) const
{
	const auto l_range = m_hyperLinks.equal_range(a_row);

	for (auto it = l_range.first; it != l_range.second; it++)
	{
		if (a_col >= it->second.GetColStart() && a_col <= it->second.GetColEnd())
		{
			return &it->second;
		}
	}

	return nullptr;
}


const CNFOHyperLink* CNFOData::GetLinkByIndex(size_t a_index) const
{
	if (a_index < m_hyperLinks.size())
	{
		/*std::multimap<size_t, CNFOHyperLink>::const_iterator it = m_hyperLinks.cbegin();
		for (size_t i = 0; i < a_index; i++, it++);*/
		const auto it = std::next(m_hyperLinks.cbegin(), a_index);
		return &it->second;
	}

	return nullptr;
}


const std::vector<const CNFOHyperLink*> CNFOData::GetLinksForLine(size_t a_row) const
{
	std::vector<const CNFOHyperLink*> l_result;

	const auto l_range = m_hyperLinks.equal_range(a_row);

	for (auto it = l_range.first; it != l_range.second; it++)
	{
		l_result.push_back(&it->second);
	}

	return l_result;
}

const std::string& CNFOData::GetLinkUrlUtf8(size_t a_row, size_t a_col) const
{
	static const std::string noLink;

	const CNFOHyperLink* l_link = GetLink(a_row, a_col);

	return (l_link ? l_link->GetHrefUtf8() : noLink);
}


/************************************************************************/
/* Raw Stripper Code                                                    */
/************************************************************************/

static std::wstring _TrimParagraph(const std::wstring& a_text)
{
	std::vector<std::wstring> l_lines;

	// split text into lines:
	std::wstring::size_type l_pos = a_text.find(L'\n'), l_prevPos = 0;
	std::wstring::size_type l_minWhite = std::numeric_limits<std::wstring::size_type>::max();

	while (l_pos != std::wstring::npos)
	{
		const std::wstring l_line = a_text.substr(l_prevPos, l_pos - l_prevPos);

		l_lines.push_back(std::move(l_line));

		l_prevPos = l_pos + 1;
		l_pos = a_text.find(L'\n', l_prevPos);
	}

	if (l_prevPos < a_text.size() - 1)
	{
		l_lines.push_back(a_text.substr(l_prevPos));
	}

	// find out the minimum number of leading whitespace characters.
	// all other lines will be reduced to this number.
	for (auto it = l_lines.cbegin(); it != l_lines.cend(); it++)
	{
		std::wstring::size_type p = 0;
		while (p < it->size() && (*it)[p] == L' ') p++;

		if (p < l_minWhite)
		{
			l_minWhite = p;
		}
	}

	// kill whitespace and put lines back together:
	std::wstring l_result;
	l_result.reserve(a_text.size());

	for (auto it = l_lines.cbegin(); it != l_lines.cend(); it++)
	{
		l_result += (*it).substr(l_minWhite);
		l_result += L'\n';
	}

	CUtil::StrTrimRight(l_result, L"\n");

	return l_result;
}

// no multine flag in std::regex (yet), so do it line by line:
static std::wstring _StripSingleLine(const std::wstring& line)
{
	if (std::regex_match(line, std::wregex(L"^[^a-zA-Z0-9]+$")))
	{
		return L"";
	}
	else if (std::regex_match(line, std::wregex(L"^(.)\\1+$")))
	{
		return L"";
	}

	std::wstring work
		= std::regex_replace(line, std::wregex(L"^([\\S])\\1+\\s{3,}(.+?)$"), L"$2");

	work
		= std::regex_replace(work, std::wregex(L"^(.+?)\\s{3,}([\\S])\\2+$"), L"$1");

	work
		= std::regex_replace(work, std::wregex(L"\\s+[\\\\/:.#_|()\\[\\]*@=+ \\t-]{3,}$"), L"");

	if (work.empty() || std::regex_match(work, std::wregex(L"^\\s*.{1,3}\\s*$")))
	{
		return L"";
	}

	return work;
}

std::wstring CNFOData::GetStrippedText() const
{
	std::wstring l_text;
	std::wstring l_line;

	l_text.reserve(m_textContent.size() / 2);
	l_line.reserve(200);

	// remove "special" characters and process by line:
	for (const wchar_t& c : m_textContent)
	{
#if defined(_WIN32) || defined(MACOSX)
		if (iswascii(c) || iswalnum(c) || iswspace(c))
#else
		if (iswalnum(c) || iswspace(c))
#endif
		{
			if (c == L'\n')
			{
				CUtil::StrTrimRight(l_line); // unify newlines

				l_text += _StripSingleLine(l_line);
				l_text += L'\n';

				l_line.clear();
			}
			else
			{
				l_line += c;
			}
		}
		else
		{
			l_line += L' '; // do this to make it easier to nicely retain paragraphs later on
		}
	}

	CUtil::StrTrimLeft(l_text, L"\n");

	l_text = std::regex_replace(l_text, std::wregex(L"\\n{2,}"), L"\n\n");

	// adjust indention for each paragraph:
	{
		std::wstring l_newText;
		std::wstring::size_type l_pos = l_text.find(L"\n\n"), l_prevPos = 0;

		while (l_pos != std::wstring::npos)
		{
			const std::wstring l_paragraph = l_text.substr(l_prevPos, l_pos - l_prevPos);
			const std::wstring l_newPara = _TrimParagraph(l_paragraph);

			l_newText += l_newPara + L"\n\n";

			l_prevPos = l_pos + 2;
			l_pos = l_text.find(L"\n\n", l_prevPos);
		}

		if (l_prevPos < l_text.size())
		{
			const std::wstring l_paragraph = l_text.substr(l_prevPos);
			l_newText += _TrimParagraph(l_paragraph);
		}

		l_text = l_newText;
	}

	return l_text;
}

const std::vector<char> CNFOData::GetTextCP437(size_t& ar_charsNotConverted, bool a_compoundWhitespace) const
{
	const std::wstring& l_input = (a_compoundWhitespace ? GetWithBoxedWhitespace() : m_textContent);
	std::map<wchar_t, char> l_transl;
	std::vector<char> l_converted;

	for (int j = 0; j < 32; j++)
	{
		l_transl[map_cp437_to_unicode_control_range[j]] = j;
	}

	for (int j = CP437_MAP_LOW; j <= 0xFF; j++)
	{
		l_transl[map_cp437_to_unicode_high_bit[j - CP437_MAP_LOW]] = j;
	}

	ar_charsNotConverted = 0;

	l_converted.resize(l_input.size(), ' ');

#pragma omp parallel for
	for (int i = 0; i < static_cast<int>(l_input.size()); i++)
	{
		const wchar_t wc = l_input[i];

		if ((wc > 0x1F && wc < CP437_MAP_LOW) || wc == L'\n' || wc == L'\r')
		{
			l_converted[i] = (char)wc;
		}
		else if (const auto it = l_transl.find(wc); it != l_transl.end())
		{
			l_converted[i] = it->second;
		}
		else
		{
#pragma omp atomic
			ar_charsNotConverted++;
		}
	}

	return l_converted;
}


bool CNFOData::SaveToCP437File(const std::_tstring& a_filePath, size_t& ar_charsNotConverted, bool a_compoundWhitespace)
{
	FILE* fp = OpenFileForWritingWithErrorMessage(a_filePath);

	if (!fp)
	{
		return false;
	}

	const std::vector<char> l_converted = GetTextCP437(ar_charsNotConverted, a_compoundWhitespace);

	bool l_success = (fwrite(l_converted.data(), 1, l_converted.size(), fp) == l_converted.size());

	fclose(fp);

	return l_success;
}


void CNFOData::SetLastError(EErrorCode a_code, const std::string& a_descr)
{
	m_lastErrorCode = a_code;
	m_lastErrorDescr = a_descr;
}


void CNFOData::SetLastError(EErrorCode a_code, const std::wstring& a_descr)
{
	m_lastErrorCode = a_code;
	m_lastErrorDescr = CUtil::FromWideStr(a_descr, CP_UTF8);
}


void CNFOData::ClearLastError()
{
	m_lastErrorCode = NDE_NO_ERROR;
	m_lastErrorDescr.clear();
}


#ifdef INFEKT_2_CXXRUST
std::unique_ptr<CNFOData> new_nfo_data()
{
	return std::make_unique<CNFOData>();
}
#endif
