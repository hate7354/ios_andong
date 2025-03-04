// pch.h: 미리 컴파일된 헤더 파일입니다.
// 아래 나열된 파일은 한 번만 컴파일되었으며, 향후 빌드에 대한 빌드 성능을 향상합니다.
// 코드 컴파일 및 여러 코드 검색 기능을 포함하여 IntelliSense 성능에도 영향을 미칩니다.
// 그러나 여기에 나열된 파일은 빌드 간 업데이트되는 경우 모두 다시 컴파일됩니다.
// 여기에 자주 업데이트할 파일을 추가하지 마세요. 그러면 성능이 저하됩니다.
#pragma once

#ifndef PCH_H
#define PCH_H

// 여기에 미리 컴파일하려는 헤더 추가
#include "framework.h"

#include <vector>
#include <string>
#include <iostream>

using namespace std;

#endif //PCH_H

#if 0
static void split(const string& str, const string& delim, vector<string>& parts)
{
	size_t start, end = 0;

	while (end < str.size())
	{
		start = end;

		while (start < str.size() && (delim.find(str[start]) != string::npos))
		{

			start++;  // skip initial whitespace

		}

		end = start;

		while (end < str.size() && (delim.find(str[end]) == string::npos))

		{

			end++; // skip to end of word

		}

		if (end - start != 0)

		{  // just ignore zero-length strings.

			parts.push_back(string(str, start, end - start));

		}

	}

}
#endif

static void split(const string& str, const string& delim, vector<string>& parts)
{
	size_t start, end = 0;

	while (end < str.size())
	{
		start = end;

		while (start < str.size() && delim == string(str,start,2))
		{

			start+=2;  // skip initial whitespace

		}
		end = start;

		while (end < str.size() && delim != string(str, end, 2))

		{

			end++; // skip to end of word

		}

		if (end - start != 0)

		{  // just ignore zero-length strings.

			parts.push_back(string(str, start, end - start));

		}

	}

}

static void MakeExtractSubString(CString& strpart, CString strfull, int TokenNum, CString strToken)
{
	vector<string> tok;

	string sfull;
	sfull = CT2CA(strfull);
	string sToken;
	sToken = CT2CA(strToken);
	split(sfull, sToken, tok);


	int i = 0;
	for (string s : tok)
	{
		//cout << s << endl;

		if (i == TokenNum)
		{
			strpart = s.c_str();
			return;
		}
		i++;

	}

}