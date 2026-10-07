#ifndef SEQUENCE_INCLUDED
#define SEQUENCE_INCLUDED

#include <windows.h>
#include <vector>

//
//	Define the underlying string/character type of the sequence.
//
//	'seqchar' can be redefined to BYTE, WCHAR, ULONG etc 
//	depending on what kind of string you want your sequence to hold
//
typedef unsigned char	  seqchar;

#if defined(SEQUENCE64) || defined(SEQUENCE_64)
typedef unsigned long long size_w;
#else
typedef unsigned long	  size_w;
#endif

const size_w MAX_SEQUENCE_LENGTH = ((size_w)(-1) / sizeof(seqchar));
const size_w MEM_BLOCK_SIZE = 0x40000;
const size_w LINE_PAGE_SIZE = 0x10000;		// granularity of each buffer's cached line-break counts
const size_w LINE_SCAN_AHEAD = 0x400000;	// how far offset_from_lineno() may scan past the exact prefix

//
//	sequence class!
//
class sequence
{
public:
	// forward declare the nested helper-classes
	class			span;
	class			span_range;
	class			buffer_control;
	class			iterator;
	class			ref;
	enum			action;
	enum			line_scan_mode;

public:

	// sequence construction
	sequence();
	~sequence();

	//
	// initialize with a file
	//
	bool		init();
	bool		open(TCHAR *filename, bool readonly);
	bool		clear();

	//
	// initialize from an in-memory buffer
	//
	bool		init(const seqchar *buffer, size_t length);

	//
	//	Sequence size and line index
	//
	size_w		size() const;

	// Line offsets are raw sequence offsets. linecount() may be estimated until every lazy file page has been indexed.
	size_w		linecount() const;
	bool		linecount_known() const;

	void		set_line_scan_mode(line_scan_mode mode);
	line_scan_mode get_line_scan_mode() const;

	// Exactness checks for sequence line-number facts.
	bool		lineno_known(size_w lineno) const;
	bool		lineno_known_at(size_w offset) const;

	// Build lazy line metadata for a touched sequence range.
	void		index_lines(size_w offset, size_w length);

	// Physical line lookup. Offsets are sequence offsets; line numbers may be estimated while lazy.
	bool		next_line_from_offset(size_w offset, size_w *line_begin) const;
	bool		line_bounds_from_offset(size_w offset, size_w *line_begin, size_w *line_next);
	bool		offset_from_lineno(size_w lineno, size_w *line_begin) const;
	bool		lineno_from_offset(size_w offset, size_w *lineno, size_w *line_begin) const;
	
	//
	// sequence manipulation 
	//
	bool		insert (size_w index, const seqchar *buf, size_w length);
	bool		insert (size_w index, const seqchar  val);
	bool		replace(size_w index, const seqchar *buf, size_w length, size_w erase_length);
	bool		replace(size_w index, const seqchar *buf, size_w length);
	bool		replace(size_w index, const seqchar  val);
	bool		erase  (size_w index, size_w len);
	bool		erase  (size_w index);
	bool		append (const seqchar *buf, size_w len);
	bool		append (const seqchar val);
	void		breakopt();

	//
	// undo/redo support
	//
	bool		undo();
	bool		redo();
	bool		canundo() const;
	bool		canredo() const;
	void		group();
	void		ungroup();

	// Bytes changed by the last undo/redo: [offset, offset + erased) became [offset, offset + inserted).
	void		event_change(size_w *offset, size_w *erased, size_w *inserted) const;

	// print out the sequence
	void		debug1();
	void		debug2();

	//
	// access and iteration
	//
	size_w		render(size_w index, seqchar *buf, size_w len) const;
	seqchar		peek(size_w index) const;
	bool		poke(size_w index, seqchar val);

	seqchar		operator[] (size_w index) const;
	ref			operator[] (size_w index);

private:

	typedef			std::vector<span_range*>	  eventstack;
	typedef			std::vector<buffer_control*>  bufferlist;
	template <class type> void clear_vector(type &source);

	//
	//	Span-table management
	//
	void			deletefromsequence(span **sptr);
	span		*	spanfromindex(size_w index, size_w *spanindex) const;
	span		*	alloc_span(size_w offset, size_w length, int buffer, span *next = 0, span *prev = 0);

	size_w			sequence_length;
	span		*	head;
	span		*	tail;	
	span		*	frag1;
	span		*	frag2;

private:
	//
	//	Line engine internals (sequence_lines.cpp).
	//
	//	Navigation (line_start_at, next_line_start) scans locally around an offset
	//	and never depends on the line index. Numbering sums per-span break counts,
	//	and is exact only across the leading run of spans whose counts are known.
	//
	class			span_cursor;

	bool			line_start_at(size_w offset, size_w *start) const;
	bool			next_line_start(size_w offset, size_w *next) const;
	bool			unit_at(size_w offset, unsigned long *ch) const;
	bool			crlf_straddles(size_w offset) const;
	size_w			line_scan_unit_size() const;

	bool			span_line_count(span *sptr) const;
	void			update_line_prefix() const;
	bool			count_breaks_to(size_w limit, size_w target, size_w *stop_offset, size_w *stop_breaks) const;
	bool			scan_to_line(size_w offset, size_w breaks, size_w line, size_w *lineoffset) const;
	bool			exact_line_from_offset(size_w offset, size_w *line) const;
	size_w			estimated_line_from_offset(size_w offset) const;
	bool			estimated_offset_from_line(size_w line, size_w *offset) const;
	void			line_density(size_w *breaks, size_w *bytes) const;
	void			extend_line_prefix(size_w line, size_w offset) const;
	void			index_range(size_w offset, size_w length) const;
	void			lines_changed() const;

	void			update_span_line_data(span* sptr);

	// Cached exact prefix: line numbers are exact for offsets <= prefix_end.
	mutable unsigned long	line_generation;
	mutable unsigned long	prefix_generation;
	mutable size_w			prefix_end;
	mutable size_w			prefix_breaks;
	mutable bool			prefix_complete;

	
	//
	//	Undo and redo stacks
	//
	span_range *	initundo(size_w index, size_w length, action act);
	void			restore_spanrange(span_range *range);
	void			swap_spanrange(span_range *src, span_range *dest);
	bool			undoredo(eventstack &source, eventstack &dest);
	void			clearstack(eventstack &source);
	span_range *	stackback(eventstack &source, size_t idx);

	eventstack		undostack;
	eventstack		redostack;
	size_t			group_id;
	size_t			group_refcount;
	size_w			change_offset;
	size_w			change_erased;
	size_w			change_inserted;

	//
	//	File and memory buffer management
	//
	buffer_control *alloc_buffer(size_t size);
	buffer_control *alloc_modifybuffer(size_t size);
	bool			import_buffer(const seqchar *buf, size_t len, size_t *buffer_offset);

	bufferlist		buffer_list;
	int				modifybuffer_id;
	int				modifybuffer_pos;

	//
	//	Sequence manipulation
	//
	bool			insert_worker (size_w index, const seqchar *buf, size_w len, action act);
	bool			erase_worker  (size_w index, size_w len, action act);
	bool			can_optimize  (action act, size_w index);
	void			record_action (action act, size_w index);

	size_w			lastaction_index;
	action			lastaction;
	bool			can_quicksave;
	line_scan_mode	line_mode;
	
};


//
//	sequence::action
//
//	enumeration of the type of 'edit actions' our sequence supports.
//	only important when we try to 'optimize' repeated operations on the
//	sequence by coallescing them into a single span.
//
enum sequence::action
{ 
	action_invalid, 
	action_insert, 
	action_erase, 
	action_replace 
};

enum sequence::line_scan_mode
{
	line_scan_bytes,
	line_scan_utf16le,
	line_scan_utf16be,
	line_scan_utf32le,
	line_scan_utf32be
};

//
//	sequence::span
//
//	private class to the sequence
//
class sequence::span
{
	friend class sequence;
	friend class span_range;
	friend class span_cursor;

public:
	// constructor
	span(size_w off, size_w len, int buf, span *nx = 0, span *pr = 0)
			:
			next(nx),
			prev(pr),
			offset(off),
			length(len),
			buffer(buf),
			line_count(0),
			line_count_known(0),
			starts_with_lf(0),
			ends_with_cr(0)
	  {
		  static int count=-2;
		  id = count++;
	  }


private:

	span   *next;
	span   *prev;	// double-link-list

	size_w  offset;
	size_w  length;
	int     buffer;

	// Line breaks in this span's own bytes. It depends only on those bytes,
	// so once known it stays valid until the span's range changes.
	size_w	line_count;
	unsigned line_count_known : 1;
	unsigned starts_with_lf : 1;
	unsigned ends_with_cr   : 1;

	int		id;
};	
	


//
//	sequence::span_range
//
//	private class to the sequence. Used to represent a contiguous range of spans.
//	used by the undo/redo stacks to store state. A span-range effectively represents
//	the range of spans affected by an event (operation) on the sequence
//  
//
class sequence::span_range
{
	friend class sequence;

public:

	// constructor
	span_range(	size_w	seqlen = 0, 
				size_w	idx    = 0, 
				size_w	len    = 0, 
				action	a      = action_invalid,
				bool	qs     = false, 
				size_t	id     = 0
			) 
		: 
		first(0), 
		last(0), 
		boundary(true), 
		sequence_length(seqlen), 	
		index(idx),
		length(len),
		act(a),
		quicksave(qs),
		group_id(id)
	{
	}
		
	// destructor does nothing - because sometimes we don't want
	// to free the contents when the span_range is deleted. e.g. when
	// the span_range is just a temporary helper object. The contents
	// must be deleted manually with span_range::free
	~span_range()
	{
	}

	// separate 'destruction' used when appropriate
	void free()
	{
		span *sptr, *next, *term;
		
		if(boundary == false)
		{
			// delete the range of spans
			for(sptr = first, term = last->next; sptr && sptr != term; sptr = next)
			{
				next = sptr->next;
				delete sptr;
			}
		}
	}

	// add a span into the range
	void append(span *sptr)
	{
		if(sptr != 0)
		{
			// first time a span has been added?
			if(first == 0)
			{
				first = sptr;
			}
			// otherwise chain the spans together.
			else
			{
				last->next = sptr;
				sptr->prev = last;
			}
			
			last     = sptr;
			boundary = false;
		}
	}

	// join two span-ranges together
	void append(span_range *range)
	{
		if(range->boundary == false)
		{	
			if(boundary)
			{
				first       = range->first;
				last        = range->last;
				boundary    = false;
			}
			else
			{
				range->first->prev = last;
				last->next  = range->first;
				last		= range->last;
			}
		}
	}

	// join two span-ranges together. used only for 'back-delete'
	void prepend(span_range *range)
	{
		if(range->boundary == false)
		{
			if(boundary)
			{
				first       = range->first;
				last        = range->last;
				boundary    = false;
			}
			else
			{
				range->last->next = first;
				first->prev	= range->last;
				first		= range->first;
			}
		}
	}
	
	// An 'empty' range is represented by storing pointers to the
	// spans ***either side*** of the span-boundary position. Input is
	// always the span following the boundary.
	void spanboundary(span *before, span *after)
	{
		first    = before;
		last     = after;
		boundary = true;
	}

	
private:
	
	// the span range
	span	*first;
	span	*last;
	bool	 boundary;

	// sequence state
	size_w	 sequence_length;
	size_w	 index;
	size_w	 length;
	action	 act;
	bool	 quicksave;
	size_t	 group_id;
};

//
//	sequence::ref
//
//	temporary 'reference' to the sequence, used for
//  non-const array access with sequence::operator[]
//
class sequence::ref
{
public:
	ref(sequence *s, size_w i) 
		:  
		seq(s),  
		index(i) 
	{
	}

	operator seqchar() const		
	{ 
		return seq->peek(index);	          
	}
	
	ref & operator= (seqchar rhs)	
	{ 
		seq->poke(index, rhs); 
		return *this;	
	}

private:
	size_w		index;
	sequence *	seq;
};

//
//	buffer_control
//
class sequence::buffer_control
{
public:
	buffer_control();
	~buffer_control();

	bool	init(size_t maxsize);
	bool	init(const seqchar *buffer, size_w length);
	bool	init_file(TCHAR *filename, bool readonly);
	bool	append(const seqchar *buffer, size_t length, size_t *buffer_offset);
	seqchar *getptr(size_w offset, size_w length);
	void	clear();

	//
	//	Line counting (sequence_lines.cpp). Each LINE_PAGE_SIZE page caches its
	//	break count once scanned. Buffer bytes never change, so a scanned page
	//	stays valid; a modify buffer's last page is rescanned after it grows.
	//
	struct line_range
	{
		size_w	 breaks;			// CR, LF and CRLF each count once
		bool	 starts_with_lf;
		bool	 ends_with_cr;
	};

	void	set_line_scan_mode(line_scan_mode mode);
	size_w	line_scan_unit_size() const;
	bool	read_unit(size_w offset, unsigned long *ch);
	bool	count_breaks(size_w offset, size_w end, bool scan_file_pages, line_range *range);
	size_w	countable_end(size_w offset, size_w end) const;
	bool	page_current(size_w page) const;
	bool	scan_page(size_w page);

	enum { MAX_VIEWS = 4 };
	struct buffer_view
	{
		seqchar	*buffer;
		size_w	 offset;
		size_w	 length;
		bool	 initialized;
	};

	struct line_page
	{
		size_w	 breaks;
		size_w	 scanned_length;	// bytes counted; the page is current when this equals its length
		bool	 starts_with_lf;
		bool	 ends_with_cr;
	};

	seqchar	*buffer;
	buffer_view viewlist[MAX_VIEWS];
	line_page *line_pages;
	size_w	 line_page_count;
	size_w	 scanned_bytes;			// totals over scanned pages, used to estimate unscanned text
	size_w	 scanned_breaks;
	size_w	 length;
	size_w	 maxsize;
	void	*fp;
	bool	 own_memory;
	bool	 readonly;
	int		 id;
	line_scan_mode line_mode;

private:
	bool	alloc_line_pages(size_w size);
	void	free_line_pages();
	void	reset_line_pages();
	size_w	page_length(size_w page) const;
	bool	scan_range(size_w offset, size_w end, line_range *range);
};

class sequence::iterator
{
public:

};

#endif
