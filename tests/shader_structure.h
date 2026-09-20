#ifndef _CRY_TEST_SHADER_STRUCTURE_H_
#define _CRY_TEST_SHADER_STRUCTURE_H_

/*!
	Structural checks on generated shader source, for both generators.

	WHY THESE EXIST

	The generator tests originally asserted that the right text APPEARED in the
	output. That is not the same as the shader being valid, and the difference
	was not academic: the WGSL generator emitted "let texel = ..." once per
	stage into one function scope, so every multi-stage pass was a
	redeclaration and failed to compile in the browser -- while every substring
	assertion passed, because both stages' text was present exactly as
	expected.

	These check STRUCTURE instead. They are not a parser for either language and
	cannot be: what they are is the smallest set of rules that the generators
	can actually violate. Both were confirmed to fail against the old generator
	before being kept.

	They are shared between the WGSL and GLSL ES tests because both languages
	have the same rule -- no redeclaration in a scope -- and both generators
	emit a flat run of declarations into one function body from the same shared
	code. A bug in that shared code has to be catchable from either side.
*/

#include <string>

//////////////////////////////////////////////////////////////////////////

inline bool CryTest_IsIdentChar(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
	    || (c >= '0' && c <= '9') || c == '_';
}

//! Does a declaration keyword start at position i, and if so how long is it?
//!
//! The two languages declare differently -- WGSL has let/var, GLSL ES uses the
//! type name -- so the keywords are passed in rather than assumed.
inline size_t CryTest_DeclKeywordAt(const std::string& s, size_t i,
                                    const char* const* pKeywords, int nKeywords)
{
	if (i > 0 && CryTest_IsIdentChar(s[i - 1]))
		return 0;

	for (int k = 0; k < nKeywords; ++k)
	{
		const size_t nLen = strlen(pKeywords[k]);
		if (s.compare(i, nLen, pKeywords[k]) == 0
		 && i + nLen < s.size() && s[i + nLen] == ' ')
			return nLen + 1;		// including the space
	}

	return 0;
}

//////////////////////////////////////////////////////////////////////////
//! No identifier is declared twice within one brace depth of one function.
//!
//! Both languages reject a redeclaration in the same scope. The generators
//! emit a flat sequence of declarations into the fragment entry point's top
//! level, so this is exactly the rule they can break.
//////////////////////////////////////////////////////////////////////////
inline bool CryTest_DeclarationsAreUnique(const std::string& s,
                                          const char* const* pKeywords, int nKeywords,
                                          std::string& sDup)
{
	// A declaration's scope is identified by the brace depth it appears at.
	// The generators never re-enter a depth they have left within one function
	// (the alpha-test discard is the only nesting, and declares nothing), so
	// depth alone is a sufficient scope key here.
	std::string aDeclared[64];		// depth -> space-separated names seen
	int nDepth = 0;

	size_t i = 0;
	while (i < s.size())
	{
		const char c = s[i];

		if (c == '{')
		{
			++nDepth;
			if (nDepth >= 64) { sDup = "brace depth out of range"; return false; }
			aDeclared[nDepth].clear();
			++i;
			continue;
		}
		if (c == '}')
		{
			if (nDepth > 0) --nDepth;
			++i;
			continue;
		}

		const size_t nSkip = CryTest_DeclKeywordAt(s, i, pKeywords, nKeywords);
		if (!nSkip)
		{
			++i;
			continue;
		}

		size_t j = i + nSkip;
		while (j < s.size() && s[j] == ' ') ++j;

		const size_t nStart = j;
		while (j < s.size() && CryTest_IsIdentChar(s[j])) ++j;

		if (j == nStart) { i += nSkip; continue; }

		const std::string sName = s.substr(nStart, j - nStart);

		// Module- and file-scope declarations are the uniforms and varyings,
		// which carry qualifiers and are covered by the binding assertions in
		// the generator tests themselves.
		if (nDepth > 0)
		{
			const std::string sKey = " " + sName + " ";
			if (aDeclared[nDepth].find(sKey) != std::string::npos)
			{
				sDup = sName;
				return false;
			}
			aDeclared[nDepth] += sKey;
		}

		i = j;
	}

	return true;
}

//////////////////////////////////////////////////////////////////////////
//! Every local the generator names is declared before it is used.
//!
//! Restricted to the generators' own naming scheme -- texelN, cN, aN -- which
//! is where an off-by-one between the emit site and the argument resolver would
//! show up. A general "is every identifier bound" check would need a real
//! parser; this needs none and catches the mistake that is actually available.
//////////////////////////////////////////////////////////////////////////
inline bool CryTest_GeneratedLocalsAreDeclared(const std::string& s,
                                               const char* const* pKeywords, int nKeywords,
                                               std::string& sUndeclared)
{
	std::string sDeclared;

	size_t i = 0;
	while (i < s.size())
	{
		const bool bAtTokenStart = (i == 0) || !CryTest_IsIdentChar(s[i - 1]);

		if (!bAtTokenStart || !CryTest_IsIdentChar(s[i]))
		{
			++i;
			continue;
		}

		size_t j = i;
		while (j < s.size() && CryTest_IsIdentChar(s[j])) ++j;

		const std::string sTok = s.substr(i, j - i);

		// Is this token one of the generators' per-stage locals?
		bool bGenerated = false;
		{
			size_t nPrefix = 0;
			if (sTok.compare(0, 5, "texel") == 0)                           nPrefix = 5;
			else if (sTok.size() > 1 && (sTok[0] == 'c' || sTok[0] == 'a')) nPrefix = 1;

			if (nPrefix && sTok.size() > nPrefix)
			{
				bGenerated = true;
				for (size_t k = nPrefix; k < sTok.size(); ++k)
					if (sTok[k] < '0' || sTok[k] > '9') { bGenerated = false; break; }
			}
		}

		if (bGenerated)
		{
			const std::string sKey = " " + sTok + " ";

			// A declaration is this token preceded by a keyword and a space.
			bool bIsDecl = false;
			for (int k = 0; k < nKeywords && !bIsDecl; ++k)
			{
				const size_t nLen = strlen(pKeywords[k]) + 1;
				if (i >= nLen && s.compare(i - nLen, nLen - 1, pKeywords[k]) == 0
				 && s[i - 1] == ' ')
					bIsDecl = true;
			}

			if (bIsDecl)
				sDeclared += sKey;
			else if (sDeclared.find(sKey) == std::string::npos)
			{
				sUndeclared = sTok;
				return false;
			}
		}

		i = j;
	}

	return true;
}

//! The declaration keywords of each language.
static const char* const kWGSLDeclKeywords[] = { "let", "var" };
static const char* const kGLSLDeclKeywords[] = { "vec4", "vec3", "vec2", "float", "int" };

#endif //_CRY_TEST_SHADER_STRUCTURE_H_
