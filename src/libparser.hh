// -*- mode: C++; c-file-style: "stroustrup"; c-basic-offset: 4; -*-

/* libutap - Uppaal Timed Automata Parser.
   Copyright (C) 2002 Uppsala University and Aalborg University.
   
   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public License
   as published by the Free Software Foundation; either version 2.1 of
   the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this library; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
   USA
*/

#ifndef UTAP_LIBPARSER_HH
#define UTAP_LIBPARSER_HH

#include <functional>

#include "utap/builder.hh"

#define MAXLEN 64

enum syntax_t { SYNTAX_OLD = 1,
		SYNTAX_NEW = 2,
		SYNTAX_PROPERTY = 4,
		SYNTAX_GUIDING = 8 };

// type for specifying which XTA part to parse (syntax switch)
typedef enum { 
  S_XTA, // entire system 
  S_DECLARATION, S_LOCAL_DECL, S_INST, S_SYSTEM, S_PARAMETERS, 
  S_INVARIANT, S_GUARD, S_SYNC, S_ASSIGN
} xta_part_t;

/***********************************************************************
 * Helper for creating function objects to members
 */
template <typename ReturnType, typename CalleeType, typename ArgType>
    class other_mem_fun_t
{
    typedef ReturnType (CalleeType::*function_t) (ArgType);
    CalleeType *m_callee;
    function_t m_pfn;

public:
    other_mem_fun_t(CalleeType *callee, function_t pfn)
        : m_callee(callee), m_pfn(pfn)
	{
	}

    ReturnType operator() (ArgType arg) const
    {
        return (m_callee->*m_pfn)(arg);
    }
};

template <typename ReturnType, typename CalleeType, typename ArgType>
other_mem_fun_t<ReturnType, CalleeType, ArgType>
    other_mem_fun(CalleeType *callee, ReturnType (CalleeType::* pfn)(ArgType))
{
    return other_mem_fun_t<ReturnType, CalleeType, ArgType>(callee, pfn);
}



/*************************************************************************
 * Parse system definition in old XTA format (without C extensions)
 * Returns >=0 (success, ended with data specific code). 
 * Returns -1 (failure). 
 */
int32_t parseXTA(FILE *, UTAP::ParserBuilder *,
		 UTAP::ErrorHandler *, 
		 bool newxta, xta_part_t part);
int32_t parseXTA(const char *, UTAP::ParserBuilder *,
		 UTAP::ErrorHandler *, bool newxta,
		 xta_part_t part);

// Defined in keywords.cc
extern bool isKeyword(const char *id, uint32_t syntax);
 
#endif