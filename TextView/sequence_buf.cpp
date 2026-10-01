/*
	sequence_buf.cpp

	Backing-buffer management for the data-sequence class.
*/

#include <memory.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "sequence.h"

sequence::buffer_control::buffer_control()
{
	buffer = 0;
	length = 0;
	maxsize = 0;
	line_offsets = 0;
	line_count = 0;
	id = 0;
}

sequence::buffer_control::~buffer_control()
{
	clear();
}

bool sequence::buffer_control::init(size_t max)
{
	clear();

	if((buffer = new seqchar[max]) == 0)
		return false;

	length = 0;
	maxsize = max;
	return true;
}

bool sequence::buffer_control::init(const seqchar *source, size_w len)
{
	clear();

	if((buffer = new seqchar[len]) == 0)
		return false;

	memcpy(buffer, source, len * sizeof(seqchar));
	length = len;
	maxsize = len;
	return true;
}

bool sequence::buffer_control::append(const seqchar *source, size_t len, size_t *buffer_offset)
{
	if(length + len > maxsize)
		return false;

	if(buffer_offset)
		*buffer_offset = length;

	memcpy(buffer + length, source, len * sizeof(seqchar));
	length += len;
	return true;
}

seqchar *sequence::buffer_control::getptr(size_w offset, size_w len)
{
	if(offset > length || len > length - offset)
		return 0;

	return buffer + offset;
}

void sequence::buffer_control::clear()
{
	delete[] line_offsets;
	line_offsets = 0;
	line_count = 0;

	delete[] buffer;
	buffer = 0;

	length = 0;
	maxsize = 0;
}

//
//	Allocate a buffer and add it to our 'buffer control' list
//
sequence::buffer_control* sequence::alloc_buffer(size_t maxsize)
{
	buffer_control *bc;

	if((bc = new buffer_control) == 0)
		return 0;

	if(!bc->init(maxsize))
	{
		delete bc;
		return 0;
	}

	bc->id = buffer_list.size();		// assign the id
	buffer_list.push_back(bc);

	return bc;
}

sequence::buffer_control* sequence::alloc_modifybuffer(size_t maxsize)
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
bool sequence::import_buffer(const seqchar *buf, size_t len, size_t *buffer_offset)
{
	buffer_control *bc;
	
	// get the current modify-buffer
	bc = buffer_list[modifybuffer_id];

	// if there isn't room then allocate a new modify-buffer
	if(bc == 0 || !bc->append(buf, len, buffer_offset))
	{
		bc = alloc_modifybuffer(len + 0x10000);
		
		// make sure that no old spans use this buffer
		record_action(action_invalid, 0);

		if(bc == 0 || !bc->append(buf, len, buffer_offset))
			return false;
	}

	update_buffer_lines(bc);
	return true;
}
