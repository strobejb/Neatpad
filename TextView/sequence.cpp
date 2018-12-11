/*
	sequence.cpp

	data-sequence class

	Copyright J Brown 1999-2006
	www.catch22.net

	Freeware
*/
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include "sequence.h"

#ifdef DEBUG_SEQUENCE

char debugfile[_MAX_PATH];
void odebug(const char *fmt, ...)
{
	va_list varg;
	va_start(varg, fmt);
	char buf[512];

	vsprintf(buf, fmt, varg);
	OutputDebugString(buf);

	va_end(varg);
}

void debug(const char *fmt, ...)
{
	FILE *fp;
	va_list varg;
	va_start(varg, fmt);

	if((fp = fopen(debugfile, "a")) != 0)
	{
		vfprintf(fp, fmt, varg);
		fclose(fp);
	}

	va_end(varg);
}

#else
#define debug
#define odebug
#endif


sequence::sequence ()
{
	record_action(action_invalid, 0);
	
	head = tail		= 0;
	sequence_length = 0;
	group_id		= 0;
	group_refcount	= 0;

	head			= new span(0, 0, 0);
	tail			= new span(0, 0, 0);
	head->next		= tail;
	tail->prev		= head;

#ifdef DEBUG_SEQUENCE
	SYSTEMTIME st;
	GetLocalTime(&st);
	sprintf(debugfile, "seqlog-%04d%02d%02d-%02d%02d%0d.txt", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
#endif
}

sequence::~sequence ()
{
	clear();

	delete head;
	delete tail;
}

bool sequence::init ()
{
	sequence_length = 0;

	if(!alloc_modifybuffer(0x10000))
		return false;

	record_action(action_invalid, 0);
	group_id		= 0;
	group_refcount	= 0;
	undoredo_index	= 0;
	undoredo_length = 0;

	return true;
}

bool sequence::init (const seqchar *buffer, size_t length)
{
	clear();

	if(!init())
		return false;

	buffer_control *bc = alloc_modifybuffer(length);
	memcpy(bc->buffer, buffer, length * sizeof(seqchar));
	bc->length = length;

	span *sptr = new span(0, length, bc->id, tail, head);
	head->next = sptr;
	tail->prev = sptr;

	sequence_length = length;
	return true;
}

//
//	Initialize from an on-disk file
//
bool sequence::open(TCHAR *filename, bool readonly)
{
	return false;
}

//
//	Initialize from an on-disk file
//
//bool sequence::save(TCHAR *filename)
//{
//	return false;
//}

template <class type>
void sequence::clear_vector (type &vectorobject)
{
	for(size_t i = 0; i < vectorobject.size(); i++)
	{
		delete vectorobject[i];
	}
}

void sequence::clearstack (eventstack &dest)
{
	for(size_t i = 0; i < dest.size(); i++)
	{
		dest[i]->free();
		delete dest[i];
	}

	dest.clear();
}

void sequence::debug1 ()
{
	span *sptr;

	for(sptr = head; sptr; sptr = sptr->next)
	{
		char *buffer = (char *)buffer_list[sptr->buffer]->buffer;
		printf("%.*s", sptr->length, buffer + sptr->offset);
	}

	printf("\n");
}

void sequence::debug2 ()
{
	span *sptr;

	printf("**********************\n");
	for(sptr = head; sptr; sptr = sptr->next)
	{
		char *buffer = (char *)buffer_list[sptr->buffer]->buffer;
		
		printf("[%d] [%4d %4d] %.*s\n", sptr->id, 
			sptr->offset, sptr->length,
			sptr->length, buffer + sptr->offset);
	}

	printf("-------------------------\n");

	for(sptr = tail; sptr; sptr = sptr->prev)
	{
		char *buffer = (char *)buffer_list[sptr->buffer]->buffer;
		
		printf("[%d] [%4d %4d] %.*s\n", sptr->id, 
			sptr->offset, sptr->length,
			sptr->length, buffer + sptr->offset);
	}

	printf("**********************\n");

	for(sptr = head; sptr; sptr = sptr->next)
	{
		char *buffer = (char *)buffer_list[sptr->buffer]->buffer;
		printf("%.*s", sptr->length, buffer + sptr->offset);
	}

	printf("\nsequence length = %d chars\n", sequence_length);
	printf("\n\n");
}

//
//	Allocate a buffer and add it to our 'buffer control' list
//
sequence::buffer_control* sequence::alloc_buffer (size_t maxsize)
{
	buffer_control *bc;

	if((bc = new buffer_control) == 0)
		return 0;

	// allocate a new buffer of byte/wchar/long/whatever
	if((bc->buffer  = new seqchar[maxsize]) == 0)
	{
		delete bc;
		return 0;
	}

	bc->length  = 0;
	bc->maxsize = maxsize;
	bc->id		= buffer_list.size();		// assign the id

	buffer_list.push_back(bc);

	return bc;
}

sequence::buffer_control* sequence::alloc_modifybuffer (size_t maxsize)
{
	buffer_control *bc;
	
	if((bc = alloc_buffer(maxsize)) == 0)
		return 0;

	modifybuffer_id  = bc->id;
	modifybuffer_pos = 0;

	return bc;
}

//
//	Import the specified range of data into the sequence so we have our own private copy
//
bool sequence::import_buffer (const seqchar *buf, size_t len, size_t *buffer_offset)
{
	buffer_control *bc;
	
	// get the current modify-buffer
	bc = buffer_list[modifybuffer_id];

	// if there isn't room then allocate a new modify-buffer
	if(bc->length + len >= bc->maxsize)
	{
		bc = alloc_modifybuffer(len + 0x10000);
		
		// make sure that no old spans use this buffer
		record_action(action_invalid, 0);
	}

	if(bc == 0)
		return false;

	// import the data
	memcpy(bc->buffer + bc->length, buf, len * sizeof(seqchar));
	
	*buffer_offset = bc->length;
	bc->length += len;

	return true;
}


//
//	sequence::spanfromindex
//
//	search the spanlist for the span which encompasses the specified index position
//
//	index		- character-position index
//	*spanindex  - index of span within sequence
//
sequence::span* sequence::spanfromindex (size_w index, size_w *spanindex = 0) const
{
	span * sptr;
	size_w curidx = 0;
	
	// scan the list looking for the span which holds the specified index
	for(sptr = head->next; sptr->next; sptr = sptr->next)
	{
		if(index >= curidx && index < curidx + sptr->length)
		{
			if(spanindex) 
				*spanindex = curidx;

			return sptr;
		}

		curidx += sptr->length;
	}

	// insert at tail
	if(sptr && index == curidx)
	{
		*spanindex = curidx;
		return sptr;
	}

	return 0;
}

void sequence::swap_spanrange(span_range *src, span_range *dest)
{
	if(src->boundary)
	{
		if(!dest->boundary)
		{
			src->first->next = dest->first;
			src->last->prev  = dest->last;
			dest->first->prev = src->first;
			dest->last->next  = src->last;
		}
	}
	else
	{
		if(dest->boundary)
		{
			src->first->prev->next = src->last->next;
			src->last->next->prev  = src->first->prev;
		}
		else
		{
			src->first->prev->next = dest->first;
			src->last->next->prev  = dest->last;
			dest->first->prev = src->first->prev;
			dest->last->next = src->last->next;
		}	
	}
}

void sequence::restore_spanrange (span_range *range, bool undo_or_redo)
{
	if(range->boundary)
	{
		span *first = range->first->next;
		span *last  = range->last->prev;

		// unlink spans from main list
		range->first->next = range->last;
		range->last->prev  = range->first;

		// store the span range we just removed
		range->first = first;
		range->last  = last;
		range->boundary = false;
	}
	else
	{
		span *first = range->first->prev;
		span *last  = range->last->next;

		// are we moving spans into an "empty" region?
		// (i.e. inbetween two adjacent spans)
		if(first->next == last)
		{
			// move the old spans back into the empty region
			first->next = range->first;
			last->prev  = range->last;

			// store the span range we just removed
			range->first  = first;
			range->last   = last;
			range->boundary  = true;
		}
		// we are replacing a range of spans in the list,
		// so swap the spans in the list with the one's in our "undo" event
		else
		{
			// find the span range that is currently in the list
			first = first->next;
			last  = last->prev;

			// unlink the the spans from the main list
			first->prev->next = range->first;
			last->next->prev  = range->last;

			// store the span range we just removed
			range->first = first;
			range->last  = last;
			range->boundary = false;
		}
	}

	// update the 'sequence length' and 'quicksave' states
	std::swap(range->sequence_length,    sequence_length);
	std::swap(range->quicksave,			 can_quicksave);

	undoredo_index	= range->index;

	if(range->act == action_erase && undo_or_redo == true || 
		range->act != action_erase && undo_or_redo == false)
	{
		undoredo_length = range->length;
	}
	else
	{
		undoredo_length = 0;
	}
}

//
//	sequence::undoredo
//
//	private routine used to undo/redo spanrange events to/from 
//	the sequence - handles 'grouped' events
//
bool sequence::undoredo (eventstack &source, eventstack &dest)
{
	span_range *range = 0;
	size_t group_id;

	if(source.empty())
		return false;

	// make sure that no "optimized" actions can occur
	record_action(action_invalid, 0);

	group_id = source.back()->group_id;

	do
	{
		// remove the next event from the source stack
		range = source.back();
		source.pop_back();

		// add event onto the destination stack
		dest.push_back(range);

		// do the actual work
		restore_spanrange(range, source == undostack ? true : false);
	}
	while(!source.empty() && (source.back()->group_id == group_id && group_id != 0));

	return true;
}

// 
//	UNDO the last action
//
bool sequence::undo ()
{
	debug("Undo\n");
	return undoredo(undostack, redostack);
}

//
//	REDO the last UNDO
//
bool sequence::redo ()
{
	debug("Redo\n");
	return undoredo(redostack, undostack);
}

//
//	Will calling sequence::undo change the sequence?
//
bool sequence::canundo () const
{
	return undostack.size() != 0;
}

//
//	Will calling sequence::redo change the sequence?
//
bool sequence::canredo () const
{
	return redostack.size() != 0;
}

//
//	Group repeated actions on the sequence (insert/erase etc)
//	into a single 'undoable' action
//
void sequence::group()
{
	if(group_refcount == 0)
	{
		if(++group_id == 0)
			++group_id;

		group_refcount++;
	}
}

//
//	Close the grouping
//
void sequence::ungroup()
{
	if(group_refcount > 0)
		group_refcount--;
}

//
//	Return logical length of the sequence
//
size_w sequence::size () const
{
	return sequence_length;
}

//
//	sequence::initundo
//
//	create a new (empty) span range and save the current sequence state
//
sequence::span_range* sequence::initundo (size_w index, size_w length, action act)
{
	span_range *event = new span_range (
								sequence_length, 
								index,
								length,
								act,
								can_quicksave, 
								group_refcount ? group_id : 0
								);

	undostack.push_back(event);
	
	return event;
}

sequence::span_range* sequence::stackback(eventstack &source, size_t idx)
{
	size_t length = source.size();
	
	if(length > 0 && idx < length)
	{
		return source[length - idx - 1];
	}
	else
	{
		return 0;
	}
}

void sequence::record_action (action act, size_w index)
{
	lastaction_index = index;
	lastaction       = act;
}

bool sequence::can_optimize (action act, size_w index)
{
	return (lastaction == act && lastaction_index == index);
}

//
//	sequence::insert_worker
//
bool sequence::insert_worker (size_w index, const seqchar *buf, size_w length, action act)
{
	span *		sptr;
	size_w		spanindex;
	size_t		modbuf_offset;
	span_range	newspans;
	size_w		insoffset;

	if(index > sequence_length)
		return false;

	// find the span that the insertion starts at
	if((sptr = spanfromindex(index, &spanindex)) == 0)
		return false;

	// ensure there is room in the modify buffer...
	// allocate a new buffer if necessary and then invalidate span cache
	// to prevent a span using two buffers of data
	if(!import_buffer(buf, length, &modbuf_offset))
		return false;

	debug("Inserting: idx=%d len=%d %.*s\n", index, length, length, buf);

	clearstack(redostack);
	insoffset = index - spanindex;

	// special-case #1: inserting at the end of a prior insertion, at a span-boundary
	if(insoffset == 0 && can_optimize(act, index))
	{
		// simply extend the last span's length
		span_range *event = undostack.back();
		sptr->prev->length	+= length;
		event->length		+= length;
	}
	// general-case #1: inserting at a span boundary?
	else if(insoffset == 0)
	{
		//
		// Create a new undo event; because we are inserting at a span
		// boundary there are no spans to replace, so use a "span boundary"
		//
		span_range *oldspans = initundo(index, length, act);
		oldspans->spanboundary(sptr->prev, sptr);
		
		// allocate new span in the modify buffer
		newspans.append(new span(
			modbuf_offset, 
			length, 
			modifybuffer_id)
			);
		
		// link the span into the sequence
		swap_spanrange(oldspans, &newspans);
	}
	// general-case #2: inserting in the middle of a span
	else
	{
		//
		//	Create a new undo event and add the span
		//  that we will be "splitting" in half
		//
		span_range *oldspans = initundo(index, length, act);
		oldspans->append(sptr);

		//	span for the existing data before the insertion
		newspans.append(new span(
							sptr->offset, 
							insoffset, 
							sptr->buffer)
						);

		// make a span for the inserted data
		newspans.append(new span(
							modbuf_offset, 
							length, 
							modifybuffer_id)
						);

		// span for the existing data after the insertion
		newspans.append(new span(
							sptr->offset + insoffset, 
							sptr->length - insoffset, 
							sptr->buffer)
						);

		swap_spanrange(oldspans, &newspans);
	}

	sequence_length += length;

	return true;
}

//
//	sequence::insert
//
//	Insert a buffer into the sequence at the specified position.
//	Consecutive insertions are optimized into a single event
//
bool sequence::insert (size_w index, const seqchar *buf, size_w length)
{
	if(insert_worker(index, buf, length, action_insert))
	{
		record_action(action_insert, index + length);
		return true;
	}
	else
	{
		return false;
	}
}

//
//	sequence::insert
//
//	Insert specified character-value into sequence
//
bool sequence::insert (size_w index, const seqchar val)
{
	return insert(index, &val, 1);
}

//
//	sequence::deletefromsequence
//
//	Remove + delete the specified *span* from the sequence
//
void sequence::deletefromsequence(span **psptr)
{
	span *sptr = *psptr;
	sptr->prev->next = sptr->next;
	sptr->next->prev = sptr->prev;

	memset(sptr, 0, sizeof(span));
	delete sptr;
	*psptr = 0;
}

//
//	sequence::erase_worker
//
bool sequence::erase_worker (size_w index, size_w length, action act)
{
	span		*sptr;
	span_range	 oldspans;
	span_range	 newspans;
	span_range	*event;
	size_w		 spanindex;
	size_w		 remoffset;
	size_w		 removelen;
	bool		 append_spanrange;	

	debug("Erasing: idx=%d len=%d\n", index, length);

	// make sure we stay within the range of the sequence
	if(length == 0 || length > sequence_length || index > sequence_length - length)
		return false;

	// find the span that the deletion starts at
	if((sptr = spanfromindex(index, &spanindex)) == 0)
		return false;

	// work out the offset relative to the start of the *span*
	remoffset = index - spanindex;
	removelen = length;

	//
	//	can we optimize?
	//
	//	special-case 1: 'forward-delete'
	//	erase+replace operations will pass through here
	//
	if(index == spanindex && can_optimize(act, index))
	{
		event = stackback(undostack, act == action_replace ? 1 : 0);
		event->length	+= length;
		append_spanrange = true;

		if(frag2 != 0)
		{
			if(length < frag2->length)
			{
				frag2->length	-= length;
				frag2->offset	+= length;
				sequence_length -= length;
				return true;
			}
			else
			{
				if(act == action_replace)
					stackback(undostack, 0)->last = frag2->next;

				removelen	-= sptr->length;
				sptr = sptr->next;
				deletefromsequence(&frag2);
			}
		}
	}
	//
	//	special-case 2: 'backward-delete'
	//	only erase operations can pass through here
	//
	else if(index + length == spanindex + sptr->length && can_optimize(action_erase, index+length))
	{
		event = undostack.back();
		event->length	+= length;
		event->index	-= length;
		append_spanrange = false;

		if(frag1 != 0)
		{
			if(length < frag1->length)
			{
				frag1->length	-= length;
				frag1->offset	+= 0;
				sequence_length -= length;
				return true;
			}
			else
			{
				removelen -= frag1->length;
				deletefromsequence(&frag1);
			}
		}
	}
	else
	{
		append_spanrange = true;
		frag1 = frag2 = 0;

		if((event = initundo(index, length, act)) == 0)
			return false;
	}

	//
	//	general-case 2+3
	//
	clearstack(redostack);

	// does the deletion *start* mid-way through a span?
	if(remoffset != 0)
	{
		// split the span - keep the first "half"
		newspans.append(new span(sptr->offset, remoffset, sptr->buffer));
		frag1 = newspans.first;
		
		// have we split a single span into two?
		// i.e. the deletion is completely within a single span
		if(remoffset + removelen < sptr->length)
		{
			// make a second span for the second half of the split
			newspans.append(new span(
							sptr->offset + remoffset + removelen, 
							sptr->length - remoffset - removelen, 
							sptr->buffer)
							);

			frag2 = newspans.last;
		}

		removelen -= min(removelen, (sptr->length - remoffset));

		// archive the span we are going to replace
		oldspans.append(sptr);
		sptr = sptr->next;	
	}

	// we are now on a proper span boundary, so remove
	// any further spans that the erase-range encompasses
	while(removelen > 0 && sptr != tail)
	{
		// will the entire span be removed?
		if(removelen < sptr->length)
		{
			// split the span, keeping the last "half"
			newspans.append(new span(
						sptr->offset + removelen, 
						sptr->length - removelen, 
						sptr->buffer)
						);

			frag2 = newspans.last;
		}

		removelen -= min(removelen, sptr->length);

		// archive the span we are replacing
		oldspans.append(sptr);
		sptr = sptr->next;
	}

	// for replace operations, update the undo-event for the
	// insertion so that it knows about the newly removed spans
	if(act == action_replace && !oldspans.boundary)
		stackback(undostack, 0)->last = oldspans.last->next;

	swap_spanrange(&oldspans, &newspans);
	sequence_length -= length;

	if(append_spanrange)
		event->append(&oldspans);
	else
		event->prepend(&oldspans);

	return true;
}

//
//	sequence::erase 
//
//  "removes" the specified range of data from the sequence. 
//
bool sequence::erase (size_w index, size_w len)
{
	if(erase_worker(index, len, action_erase))
	{
		record_action(action_erase, index);
		return true;
	}
	else
	{
		return false;
	}
}

//
//	sequence::erase
//
//	remove single character from sequence
//
bool sequence::erase (size_w index)
{
	return erase(index, 1);
}

//
//	sequence::replace
//
//	A 'replace' (or 'overwrite') is a combination of erase+inserting
//  (first we erase a section of the sequence, then insert a new block
//  in it's place). 
//
//	Doing this as a distinct operation (erase+insert at the 
//  same time) is really complicated, so I just make use of the existing 
//  sequence::erase and sequence::insert and combine them into action. We
//	need to play with the undo stack to combine them in a 'true' sense.
//
bool sequence::replace(size_w index, const seqchar *buf, size_w length, size_w erase_length)
{
	size_t remlen = 0;

	debug("Replacing: idx=%d len=%d %.*s\n", index, length, length, buf);

	// make sure operation is within allowed range
	if(index > sequence_length || MAX_SEQUENCE_LENGTH - index < length)
		return false;

	// for a "replace" which will overrun the sequence, make sure we 
	// only delete up to the end of the sequence
	remlen = min(sequence_length - index, erase_length);

	// combine the erase+insert actions together
	group();

	// first of all remove the range
	if(remlen > 0 && index < sequence_length && !erase_worker(index, remlen, action_replace))
	{
		ungroup();
		return false;
	}
	
	// then insert the data
	if(insert_worker(index, buf, length, action_replace))
	{
		ungroup();
		record_action(action_replace, index + length);
		return true;
	}
	else
	{
		// failed...cleanup what we have done so far
		ungroup();
		record_action(action_invalid, 0);

		span_range *range = undostack.back();
		undostack.pop_back();
		restore_spanrange(range, true);
		delete range;

		return false;
	}
}

//
//	sequence::replace
//
//	overwrite with the specified buffer
//
bool sequence::replace (size_w index, const seqchar *buf, size_w length)
{
	return replace(index, buf, length, length);
}

//
//	sequence::replace
//
//	overwrite with a single character-value
//
bool sequence::replace (size_w index, const seqchar val)
{
	return replace(index, &val, 1);
}

//
//	sequence::append
//
//	very simple wrapper around sequence::insert, just inserts at
//  the end of the sequence
//
bool sequence::append (const seqchar *buf, size_w length)
{
	return insert(size(), buf, length);
}

//
//	sequence::append
//
//	append a single character to the sequence
//
bool sequence::append (const seqchar val)
{
	return append(&val, 1);
}

//
//	sequence::clear
//
//	empty the entire sequence, clear undo/redo history etc
//
bool sequence::clear ()
{
	span *sptr, *tmp;
	
	// delete all spans in the sequence
	for(sptr = head->next; sptr != tail; sptr = tmp)
	{
		tmp = sptr->next;
		delete sptr;
	}

	// re-link the head+tail
	head->next = tail;
	tail->prev = head;

	// delete everything in the undo/redo stacks
	clearstack(undostack);
	clearstack(redostack);

	// delete all memory-buffers
	for(size_t i = 0; i < buffer_list.size(); i++)
	{
		delete[] buffer_list[i]->buffer;
		delete   buffer_list[i];
	}

	buffer_list.clear();
	sequence_length = 0;
	return true;
}

//
//	sequence::render
//
//	render the specified range of data (index, len) and store in 'dest'
//
//	Returns number of chars copied into destination
//
size_w sequence::render(size_w index, seqchar *dest, size_w length) const
{
	size_w spanoffset = 0;
	size_w total = 0;
	span  *sptr;

	// find span to start rendering at
	if((sptr = spanfromindex(index, &spanoffset)) == 0)
		return false;

	// might need to start mid-way through the first span
	spanoffset = index - spanoffset;

	// copy each span's referenced data in succession
	while(length && sptr != tail)
	{
		size_w copylen   = min(sptr->length - spanoffset, length);
		seqchar *source  = buffer_list[sptr->buffer]->buffer;

		memcpy(dest, source + sptr->offset + spanoffset, copylen * sizeof(seqchar));
		
		dest	+= copylen;
		length	-= copylen;
		total	+= copylen;

		sptr = sptr->next;
		spanoffset = 0;
	}

	return total;
}

//
//	sequence::peek
//
//	return single element at specified position in the sequence
//
seqchar sequence::peek(size_w index) const
{
	seqchar   value;
	return render(index, &value, 1) ? value : 0;
}

//
//	sequence::poke
//
//	modify single element at specified position in the sequence
//
bool sequence::poke(size_w index, seqchar value) 
{
	return replace(index, &value, 1);
}

//
//	sequence::operator[] const
//
//	readonly array access
//
seqchar sequence::operator[] (size_w index) const
{
	return peek(index);
}

//
//	sequence::operator[] 
//
//	read/write array access
//
sequence::ref sequence::operator[] (size_w index)
{
	return ref(this, index);
}

//
//	sequence::breakopt
//
//	Prevent subsequent operations from being optimized (coalesced) 
//  with the last.
//
void sequence::breakopt()
{
	lastaction = action_invalid;
}