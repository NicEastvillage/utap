// -*- mode: C++; c-file-style: "stroustrup"; c-basic-offset: 4; -*-

/* libutap - Uppaal Timed Automata Parser.
   Copyright (C) 2002-2004 Uppsala University and Aalborg University.
   
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

#include <libxml/xmlreader.h>
#include <cstdarg>
#include <cctype>
#include <cassert>
#include <algorithm>
#include <list>
#include <vector>
#include <map>
#include <sstream>

#include "libparser.h"

using namespace UTAP;

using std::map;
using std::vector;
using std::list;
using std::ostringstream;
using std::string;

/**
 * Enumeration type for tags. We use gperf to generate a perfect hash
 * function to map tag strings to one of these tags.
 */

enum tag_t
{
    TAG_NTA, TAG_IMPORTS, TAG_DECLARATION, TAG_TEMPLATE, TAG_INSTANTIATION,
    TAG_SYSTEM, TAG_NAME, TAG_PARAMETER, TAG_LOCATION, TAG_INIT,
    TAG_TRANSITION, TAG_URGENT, TAG_COMMITTED, TAG_SOURCE, TAG_TARGET,
    TAG_LABEL, TAG_NAIL
};

#include "tags.cc"

/**
 * Returns TRUE if string is NULL, zero length or contains only white
 * spaces otherwise FALSE
 */
static bool isempty(const char *p)
{
    if (p) 
    {
	while (*p) 
	{
	    if (!isspace(*p))
	    {
		return false;
	    }
	    p++;
	}
    }
    return true;
}

static bool isAlpha(char c)
{
    return isalpha(c) || c == '_';
}
 
static bool isIdChr(char c)
{
    return isalnum(c) || c == '_' || c == '$' || c == '#';
} 

/**
 * Extracts the alpha-numerical symbol used for variable/type
 * identifiers.  Identifier starts with alpha and further might
 * contain digits, white spaces are ignored.
 *
 * Throws a TypeException is identifier is invalid or a newly
 * allocated string to be destroyed with delete [].
 */
static char* symbol(const char *str)
{
    if (str == NULL)
    {
        throw "Identifier expected";
    }
    while (isspace(*str)) 
    {
	str++;
    }
    if (*str == 0)
    {
        throw "Identifier expected";
    }
    if (!isAlpha(*str))
    {
        throw "Invalid identifier";
    }
    const char *end = str;
    while (isIdChr(*end)) end++;
    const char *p = end;
    while (isspace(*p))
    {
	p++;
    }
    if (*p)
    {
        throw "Invalid identifier";
    }
    int32_t len = end - str;
    char *res = strncpy(new char[len + 1], str, len);
    res[len] = 0;
    return res;
}
                                                                                


/**
 * Comparator structure for comparing two xmlChar* strings.
 */
struct compare_str 
{
    bool operator()(const xmlChar* x, const xmlChar* y) const 
    {
	return (strcmp((const char*)x, (const char*)y)<0);
    }
};

/** 
 * Path to current node. This path also contains information about the
 * left siblings of the nodes. This information is used to generated
 * an XPath expression.
 *
 * @see get()
 */
class Path : public XPath
{

private:
    list<vector<tag_t> > path;
public:
    Path();
    void push(tag_t);
    tag_t pop();
    virtual string get() const;
};

Path::Path()
{
    path.push_back(vector<tag_t>());
}

void Path::push(tag_t tag)
{
    path.back().push_back(tag);
    path.push_back(vector<tag_t>());    
}

tag_t Path::pop()
{
    path.pop_back();
    return path.back().back();
}

/** Returns the XPath encoding of the current path. */
string Path::get() const
{
    ostringstream str;
    list<vector<tag_t> >::const_iterator i;
    for (i = path.begin(); !i->empty() && i != path.end(); i++)
    {
	switch (i->back()) 
	{
	case TAG_NTA:           
	    str << "/nta"; 
	    break; 
	case TAG_IMPORTS:       
	    str << "/imports"; 
	    break;
	case TAG_DECLARATION:
	    str << "/declaration"; 
	    break;
	case TAG_TEMPLATE:
	    str << "/template[" <<
		std::count(i->begin(), i->end(), TAG_TEMPLATE) << "]";
	    break;
	case TAG_INSTANTIATION: 
	    str << "/instantiation"; 
	    break;
	case TAG_SYSTEM:        
	    str << "/system"; 
	    break;
	case TAG_NAME:
	    str << "/name"; 
	    break;
	case TAG_PARAMETER:     
	    str << "/parameter"; 
	    break;
	case TAG_LOCATION:      
	    str << "/location[" 
		<< std::count(i->begin(), i->end(), TAG_LOCATION) << "]"; 
	    break;
	case TAG_INIT:
	    str << "/init"; 
	    break;
	case TAG_TRANSITION:
	    str << "/transition[" <<
		std::count(i->begin(), i->end(), TAG_TRANSITION) << "]"; 
	    break;
	case TAG_LABEL:         
	    str << "/label[" <<
		std::count(i->begin(), i->end(), TAG_LABEL) << "]";
	    break;
	case TAG_URGENT:
	    str << "/urgent"; 
	    break;
	case TAG_COMMITTED:
	    str << "/committed"; 
	    break;
	case TAG_SOURCE:
	    str << "/source"; 
	    break;
	case TAG_TARGET:
	    str << "/target"; 
	    break;
	case TAG_NAIL:
	    str << "/nail[" 
		<< std::count(i->begin(), i->end(), TAG_NAIL) << "]"; 
	    break;
	default: 
	    /* Strange tag on stack */
	    throw -1;
	}
    }
    return str.str();
}

/**
 * Implements a recursive descent parser for UPPAAL XML documents.
 * Uses the xmlTextReader API from libxml2. 
 */
class XMLReader
{
private:
    typedef map<xmlChar*, char*, compare_str> locationmap_t;

    xmlTextReaderPtr reader;         /**< The underlying xmlTextReader */
    locationmap_t locations;         /**< Map from location id's to location names. */
    ParserBuilder *parser;           /**< The parser builder to which to push the model. */
    ErrorHandler *errorHandler;      /**< The error handler. */
    bool newxta;                     /**< True if we should use new syntax. */
    Path path;                       /**< Path to the current node. */

    tag_t getElement();
    bool isEmpty();
    int getNodeType();
    void read();
    bool begin(tag_t, bool skipEmpty = true);

    const char *getLocation(const xmlChar *id);
    int parse(const xmlChar *, xta_part_t syntax);

    bool declaration();
    bool label();
    bool committed();
    bool urgent();
    bool location();
    bool init();
    char *name();
    const char *source();
    const char *target();
    bool transition();
    bool templ();
    int parameter();
    bool instantiation();
    bool system();
public:
    XMLReader(
	xmlTextReaderPtr reader, ParserBuilder *parser, 
	ErrorHandler *errorHandler, bool newxta);
    virtual ~XMLReader();
    void nta();
};

XMLReader::XMLReader(
    xmlTextReaderPtr reader, ParserBuilder *parser, 
    ErrorHandler *errorHandler, bool newxta)
    : reader(reader), parser(parser), 
      errorHandler(errorHandler), newxta(newxta)
{
    errorHandler->setCurrentPath(&path);
    read();
}

XMLReader::~XMLReader()
{
    locationmap_t::iterator i;
    for (i = locations.begin(); i != locations.end(); ++i) 
    {
	xmlFree(i->first);
	delete[] i->second;
    }
    xmlFreeTextReader(reader);
}

/** Returns the type of the current node. */
int XMLReader::getNodeType()
{
  return xmlTextReaderNodeType(reader);
}

/**
 * Returns the tag of the current element. Throws an exception
 * if the tag is not known.
 */
tag_t XMLReader::getElement()
{
    const char *element = (const char *)xmlTextReaderConstLocalName(reader);
    const Tag *tag= Tags::in_word_set(element, strlen(element));
    if (tag == NULL)
    {
	/* Unknown element. */
	throw -1;
    }
    return tag->tag;
}

/** Returns true if the current element is an empty element. */
bool XMLReader::isEmpty()
{
    return xmlTextReaderIsEmptyElement(reader);
}

/**
 * Read until start element. Returns true if that element has the
 * given tag. If skipEmpty is true, empty elements with the given tag
 * are ignored.
 */
bool XMLReader::begin(tag_t tag, bool skipEmpty)
{
    for (;;)
    {
	while (getNodeType() != XML_READER_TYPE_ELEMENT)
	{
	    read();
	}
	
	if (getElement() != tag)
	{
	    return false;
	}
	
	if (!skipEmpty || !isEmpty())
	{
	    return true;
	}
	read();
    }
}

/**
 * Advances the reader. It maintains the path to the current node.
 */
void XMLReader::read()
{
    if (getNodeType() == XML_READER_TYPE_END_ELEMENT
	|| getNodeType() == XML_READER_TYPE_ELEMENT && isEmpty())
    {
	if (path.pop() != getElement())
	{
	    /* Path is corrupted */
	    throw -1;
	}
    }

    if (xmlTextReaderRead(reader) != 1)
    {
	/* Premature end of document. */
	throw -1;
    }

    if (getNodeType() == XML_READER_TYPE_ELEMENT)
    {
	path.push(getElement());
    }
}

/** Returns the name of a location. */
const char *XMLReader::getLocation(const xmlChar *id)
{
    if (id)
    {
	locationmap_t::iterator l = locations.find((xmlChar*)id);
	if (l != locations.end())
	{
	    return l->second;
	}
    }
    throw -1;
}

/** Invokes the bison generated parser to parse the given string. */
int XMLReader::parse(const xmlChar *text, xta_part_t syntax)
{
    return parseXTA((const char*)text, parser, errorHandler, newxta, syntax);
}

/** Parse optional declaration. */
bool XMLReader::declaration()
{   
    if (begin(TAG_DECLARATION))
    {
	read();
	if (getNodeType() == XML_READER_TYPE_TEXT)
	{
	    parse(xmlTextReaderConstValue(reader), S_DECLARATION);
	}
	return true;
    }
    return false;
}

/** Parse optional label. */
bool XMLReader::label()
{
    xmlChar *kind;

    if (begin(TAG_LABEL))
    {
	/* Get kind attribute. */
	kind = xmlTextReaderGetAttribute(reader, (const xmlChar *)"kind");
	read();
	
	/* Read the text and push it to the parser. */
	if (getNodeType() == XML_READER_TYPE_TEXT)
	{
	    const xmlChar *text = xmlTextReaderConstValue(reader);
	    
	    if (strcmp((char*)kind, "invariant") == 0)
	    {
		parse(text, S_INVARIANT);
	    }
	    else if (strcmp((char*)kind, "guard") == 0)
	    {
		parse(text, S_GUARD);
	    }
	    else if (strcmp((char*)kind, "synchronisation") == 0)
	    {
		parse(text, S_SYNC);
	    }
	    else if (strcmp((char*)kind, "assignment") == 0)
	    {
		parse(text, S_ASSIGN);
	    }
	}	    
	xmlFree(kind);    
	return true;
    }
    return false;
}

/** Parse optional name tag. */
char *XMLReader::name()
{
    if (begin(TAG_NAME))
    {
	read();
	if (getNodeType() == XML_READER_TYPE_TEXT)
	{
	    xmlChar *name = xmlTextReaderValue(reader);
	    try 
	    {
		char *id = symbol((char*)name);
		
		if (!isKeyword(id, SYNTAX_OLD | SYNTAX_PROPERTY))
		{
		    xmlFree(name);
		    return id;
		}
		delete[] id;
		errorHandler->handleError("Keywords are not allowed here");
	    }
	    catch (const char *str)
	    {
		errorHandler->handleError(str);
	    }
	    xmlFree(name);
	}
    }
    return NULL;
}

/** Parse optional committed tag. */
bool XMLReader::committed()
{
    if (begin(TAG_COMMITTED, false)) 
    {
	read();
	return true;
    }
    return false;
}

/** Parse optional urgent tag. */
bool XMLReader::urgent()
{
    if (begin(TAG_URGENT, false)) 
    {
	read();
	return true;
    }
    return false;
}

/** Parse optional location. */
bool XMLReader::location()
{
    char *l_name = NULL;
    xmlChar *l_id = NULL;
    bool l_committed = false;
    bool l_urgent = false;
    bool l_invariant = false;

    if (begin(TAG_LOCATION, false))
    {
	try 
	{
	    /* Extract ID attribute.
	     */
	    l_id = xmlTextReaderGetAttribute(reader, (const xmlChar*)"id");

	    read();
	    
	    /* Get name of the location.
	     */
	    l_name = name();
		
	    /* Read the labels.
	     */
	    while (label())
	    {
		l_invariant = true;
	    }
		
	    /* Is the location urgent or committed?
	     */
	    l_urgent = urgent();
	    l_committed = committed();
		
	    /* In case of anonymous locations we assign an
	     * internal name based on the ID of the
	     * location. FIXME: Is it ok to use an underscore
	     * here?
	     */
	    if (isempty(l_name))
	    {
		delete[] l_name;
		l_name = new char[strlen((char*)l_id) + 2];
		l_name[0] = '_';
		strcpy(l_name + 1, (char*)l_id);
	    }
	    
	    /* Remember the mapping from id to name
	     */
	    locations[l_id] = l_name;
	    
	    /* Push location to parser builder.
	     */
	    parser->procState(l_name, l_invariant);
	    if (l_committed)
	    {
		parser->procStateCommit(l_name);
	    }
	    if (l_urgent)
	    {
		parser->procStateUrgent(l_name);
	    }
	}
	catch (TypeException &e)
	{
	    errorHandler->handleError(e.what());
	}
	return true;
    }
    return false;
}

/** Parse optional init tag. */
bool XMLReader::init()
{
    if (begin(TAG_INIT, false))
    {
	/* Get reference attribute. 
	 */
	xmlChar *ref = xmlTextReaderGetAttribute(reader, (const xmlChar*)"ref");

	/* Find location name for the reference. 
	 */
	const char *name;
	if (ref && (name = getLocation(ref)))
	{
	    /* Found it. Now push it to the parser builder.
	     */
	    try 
	    {
		parser->procStateInit(name);
	    }
	    catch (TypeException te) 
	    {
		errorHandler->handleError(te.what());
	    }
	}
	else
	{
	    errorHandler->handleError("Missing initial state");
	}
	xmlFree(ref);
	read();
	return true;
    } 
    else
    {
	errorHandler->handleError("Missing initial state");
    }
    return false;
}

/** Parse obligatory source tag. */
const char *XMLReader::source()
{
    const char *name;
    if (begin(TAG_SOURCE, false))
    {
	xmlChar *id = xmlTextReaderGetAttribute(reader, (const xmlChar*)"ref");
	name = getLocation(id);
	xmlFree(id);
	read();
	return name;
    }
    throw -1;
}

/** Parse obligatory target tag. */
const char *XMLReader::target()
{
    const char *name;
    if (begin(TAG_TARGET, false))
    {
	xmlChar *id = xmlTextReaderGetAttribute(reader, (const xmlChar*)"ref");
	name = getLocation(id);
	xmlFree(id);	
	read();
	return name;
    }
    throw -1;
}

/** Parse optional transition. */
bool XMLReader::transition()
{
    if (begin(TAG_TRANSITION))
    {
	const char *from = NULL;
	const char *to = NULL;

	try
	{
	    read();
	    from = source();
	    to = target();
	    while (label());
	    while (begin(TAG_NAIL));
	    parser->procEdge(from, to);
	}
	catch (TypeException &e)
	{
	    errorHandler->handleError(e.what());
	}

	return true;
    }
    return false;
}

/** 
 * Parses an optional parameter tag and returns the number of
 * parameters.
 */
int XMLReader::parameter()
{
    int count = 0;
    if (begin(TAG_PARAMETER))
    {
	read();
	if (getNodeType() == XML_READER_TYPE_TEXT)
	{
	    count = parse(xmlTextReaderConstValue(reader), S_PARAMETERS);
	}
    }
    return count;
}

/** Parse optional template. */
bool XMLReader::templ()
{
    char *t_name = NULL;
    int p_count;

    if (begin(TAG_TEMPLATE))
    {
	read();
	try 
	{
	    /* Get the name and the parameters of the template.
	     */
	    t_name = name();
	    p_count = parameter();
	    
	    /* Push template beginning to parser builder. This might
	     * throw a TypeException.
	     */
	    parser->procBegin(t_name, p_count);
	    
	    /* Parse declarations, locations, the init tag and the 
	     * transitions of the template.
	     */
	    declaration();
	    while (location());
	    init();
	    while (transition());
	    
	    /* Push template end to parser builder.  FIXME: In case of
	     * errors thrown in procEnd, the path no longer points to
	     * the template.
	     */
	    parser->procEnd();
	}
	catch (TypeException &e) 
	{
	    errorHandler->handleError(e.what());
	}
	delete[] t_name;
	
	return true;
    }
    return false;
}

/** Parse optional instantiation tag. */
bool XMLReader::instantiation()
{
    if (begin(TAG_INSTANTIATION, false))
    {
	const xmlChar *text = (const xmlChar*)"";
	read();
	if (getNodeType() == XML_READER_TYPE_TEXT)
	{
	    text = xmlTextReaderConstValue(reader);
	}
	parse(text, S_INST);
	return true;
    }
    return false;
}

/** Parse optional system tag. */
bool XMLReader::system()
{
    if (begin(TAG_SYSTEM, false))
    {
	const xmlChar *text = (const xmlChar*)"";
	read();
	if (getNodeType() == XML_READER_TYPE_TEXT)
	{
	    text = xmlTextReaderConstValue(reader);
	}
	parse(text, S_SYSTEM);
	return true;
    }
    return false;
}

/** Parse NTA document. */
void XMLReader::nta()
{
    if (!begin(TAG_NTA))
    {
	throw -1;
    }
    read();
    declaration();
    while (templ());
    instantiation();
    system();
    parser->done();
}

int32_t parseXMLFile(const char *filename, ParserBuilder *pb,
		     ErrorHandler *errHandler, bool newxta) 
{
    try
    {
	xmlTextReaderPtr reader = xmlReaderForFile(
	    filename, "", XML_PARSE_NOCDATA | XML_PARSE_NOBLANKS);
	if (reader == NULL)
	{
	    return -1;
	}
	XMLReader(reader, pb, errHandler, newxta).nta();
	return 0;
    } 
    catch (int error)
    {
	return error;
    }
}

int32_t parseXMLBuffer(const char *buffer, ParserBuilder *pb,
		       ErrorHandler *errHandler, bool newxta)
{
    try
    {
	size_t length = strlen(buffer);
	xmlTextReaderPtr reader = xmlReaderForMemory(
	    buffer, length, "", "", XML_PARSE_NOCDATA);
	if (reader == NULL)
	{
	    return -1;
	}
	XMLReader(reader, pb, errHandler, newxta).nta();
	return 0;
    } 
    catch (int error)
    {
	return error;
    }    
}