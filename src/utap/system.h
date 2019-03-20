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

#ifndef UTAP_INTERMEDIATE_HH
#define UTAP_INTERMEDIATE_HH

#include <list>
#include <vector>
#include <map>
#include <exception>

#include "utap/symbols.h"
#include "utap/expression.h"

namespace UTAP
{
    /** Base type for variables, clocks, etc.  The user data of the
	corresponding symbol_t points to this structure,
	i.e. v.uid.getData() is a pointer to v.
    */
    struct variable_t 
    {
	symbol_t uid;      /**< The symbol of the variables */
	expression_t expr; /**< The initialiser */
    };

    /** Information about a location.
	The symbol's user data points to this structure, i.e.
	s.uid.getData() is a pointer to s. 
    */
    struct state_t 
    {
	symbol_t uid;		/**< The symbol of the location */
	expression_t invariant; /**< The invariant */
	int32_t locNr;		/**< Location number in template */
    };

    /** Information about an edge.  Edges have a source (src) and a
	destination (dst) locations. The guard, synchronisation and
	assignment are stored as expressions.
    */
    struct edge_t 
    {
	int nr;			/**< Placement in in put file */
	state_t *src;		/**< Pointer to source location */
	state_t *dst;		/**< Pointer to destination location */
	expression_t guard;	/**< The guard */
	expression_t assign;	/**< The assignment */
	expression_t sync;	/**< The synchronisation */
    };

    class BlockStatement; // Forward declaration

    /** Information about a function. The symbol's user data points to
	this structure, i.e. f.uid.getData() is a pointer to f.
    */
    struct function_t
    {
	symbol_t uid;		     /**< The symbol of the function */
	std::set<symbol_t> changes;  /**< Variables changed by this function */
	std::list<variable_t> variables;
	BlockStatement *body;	     /**< Pointer to the block */
	function_t() : body(NULL) {}
	~function_t();
    };

    struct template_t;
	
    struct instance_t 
    {
	symbol_t uid;
	const template_t *templ;
	std::map<symbol_t, expression_t> mapping;
    };

    struct progress_t
    {
	expression_t guard;
	expression_t measure;
    };

    /**
     * Structure holding declarations of various types. Used by
     * templates and block statements.
     */
    struct declarations_t 
    {
	frame_t frame;
	std::list<variable_t> variables;	/**< Variables */
	std::list<function_t> functions;	/**< Functions */
	std::list<progress_t> progress;         /**< Progress measures */

	/** Add function declaration. */
	bool addFunction(type_t type, std::string, function_t *&);
    };

    /**
     * Information about a template. A template is a parameterised
     * automaton.
     */
    struct template_t : public declarations_t
    {
	symbol_t uid;				/**< Symbol of the template */
	int32_t nr;				/**< Placement in input file */
	symbol_t init;				/**< The initial location */
	frame_t parameters;                     /**< The parameters */
	std::list<state_t> states;		/**< Locations */
	std::list<edge_t> edges;	        /**< Edges */
	
	/** Add another location to template. */
	state_t &addLocation(std::string, expression_t inv);

	/** Add edge to template. */
	edge_t &addEdge(symbol_t src, symbol_t dst);
    };

    /** Information about a process. A process is something mentioned
	in the system line of the input file. It basically points to a
	template, but also contains information about offsets for
	local variables, clocks, etc.
    */
    struct process_t : public instance_t 
    {
	int32_t nr;		/**< Placement in the system line */
    };

    class TimedAutomataSystem;
    
    class SystemVisitor
    {
    public:
	virtual ~SystemVisitor() {}
	virtual void visitSystemBefore(TimedAutomataSystem *) {}
	virtual void visitSystemAfter(TimedAutomataSystem *) {}
	virtual void visitVariable(variable_t &) {}
	virtual bool visitTemplateBefore(template_t &) { return true; }
	virtual void visitTemplateAfter(template_t &) {}
	virtual void visitState(state_t &) {}
	virtual void visitEdge(edge_t &) {}
	virtual void visitInstance(instance_t &) {}
	virtual void visitProcess(process_t &) {}
	virtual void visitFunction(function_t &) {}
    };

    class TimedAutomataSystem
    {
    public:
	TimedAutomataSystem();
	virtual ~TimedAutomataSystem();

	/** Returns the global declarations of the system. */
	declarations_t &getGlobals();

	/** Returns the templates of the system. */
	std::list<template_t> &getTemplates();

	/** Returns the processes of the system. */
	std::list<process_t> &getProcesses();

	variable_t *addVariableToFunction(
	    function_t *, frame_t, type_t, std::string, expression_t initital);
	variable_t *addVariable(
	    declarations_t *, type_t type, std::string, expression_t initial);
	void addProgressMeasure(
	    declarations_t *, expression_t guard, expression_t measure);

	template_t &addTemplate(std::string, frame_t params);
	instance_t &addInstance(std::string name, const template_t *);
	process_t &addProcess(symbol_t uid);
	void accept(SystemVisitor &);

	/** Returns the set of symbols declared as constants. */
	const std::set<symbol_t> &getConstants() const;

	/**
	 * Returns a valuation for the constants. Constants evaluate
	 * to their initial value. This valuation is populated by the
	 * type checker, hence it is empty until the type checker has
	 * been run.
	 */
	const std::map<symbol_t, expression_t> &getConstantValuation() const;

	/**
	 * Returns a valuation for the constants. Constants evaluate
	 * to their initial value. This valuation is populated by the
	 * type checker, hence it is empty until the type checker has
	 * been run.
	 */
	std::map<symbol_t, expression_t> &getConstantValuation();	

	void setBeforeUpdate(expression_t);
	expression_t getBeforeUpdate();
	void setAfterUpdate(expression_t);
	expression_t getAfterUpdate();

#ifdef ENABLE_PRIORITY

	/* Set priorities for channels and processes. */
	void setChanPriority(symbol_t uid, int32_t prio);
	void setProcPriority(symbol_t uid, int32_t prio);

	/* Get priorities for channels and processes. */
	int32_t getChanPriority(symbol_t uid);
	int32_t getProcPriority(symbol_t uid);

	/* Returns true if system has some priority declaration. */
	bool hasPriorityDeclaration() const;

    protected:
	bool hasPriority;
	std::map<symbol_t, int32_t> chanPriority;
	std::map<symbol_t, int32_t> procPriority;

#endif /* ENABLE_PRIORITY */
	
    protected:
	// The list of templates.
	std::list<template_t> templates;

	// The list of template instances.
	std::list<instance_t> instances;
	
	// List of processes used in the system line
	std::list<process_t> processes;

	// The set of all constants
	std::set<symbol_t> constants;	

	// Maps constans to their values
	std::map<symbol_t, expression_t> constantValuation;
	
	// Global declarations
	declarations_t global;

        expression_t beforeUpdate;
        expression_t afterUpdate;

	variable_t *addVariable(
	    std::list<variable_t> &variables, frame_t frame, 
	    type_t type, std::string);
    };

    /** Extension of SystemVisitor which tracks the context. It can use
	this information when reporting errors and warnings to an
	ErrorHandler.
    */
    class ContextVisitor : public SystemVisitor, private XPath
    {
    private:
	int currentTemplate;
	std::string path;
	ErrorHandler *errorHandler;
	virtual std::string get() const;
    protected:
	void setContextNone();
	void setContextDeclaration();
	void setContextParameters();
	void setContextInvariant(state_t &);
	void setContextGuard(edge_t &);
	void setContextSync(edge_t &);
	void setContextAssignment(edge_t &);
	void setContextInstantiation();
	
	void handleError(expression_t, std::string);
	void handleWarning(expression_t, std::string);
    public:
	ContextVisitor(ErrorHandler *);
	virtual bool visitTemplateBefore(template_t &);
	virtual void visitTemplateAfter(template_t &);
    };

}
#endif
