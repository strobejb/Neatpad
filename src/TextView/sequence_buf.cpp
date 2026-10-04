/*
	sequence_buf.cpp

	Backing-buffer management for the data-sequence class.
*/

#include <memory.h>
#include <stdio.h>
#include <tchar.h>
#include "sequence.h"

#ifdef _MSC_VER
#define seq_fopen(result, filename, mode) (_tfopen_s((result), (filename), (mode)) == 0 && *(result) != 0)
#else
#define seq_fopen(result, filename, mode) ((*(result) = _tfopen((filename), (mode))) != 0)
#endif

sequence::buffer_control::buffer_control()
{
	int i;

	buffer = 0;
	length = 0;
	maxsize = 0;
	fp = 0;
	own_memory = true;
	readonly = false;
	id = 0;

	for(i = 0; i < MAX_VIEWS; i++)
	{
		viewlist[i].buffer = 0;
		viewlist[i].offset = 0;
		viewlist[i].length = 0;
		viewlist[i].initialized = false;
	}
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

	viewlist[0].buffer = buffer;
	viewlist[0].offset = 0;
	viewlist[0].length = max;
	viewlist[0].initialized = true;

	length = 0;
	maxsize = max;
	own_memory = true;
	return true;
}

bool sequence::buffer_control::init(const seqchar *source, size_w len)
{
	clear();

	if((buffer = new seqchar[len]) == 0)
		return false;

	memcpy(buffer, source, len * sizeof(seqchar));

	viewlist[0].buffer = buffer;
	viewlist[0].offset = 0;
	viewlist[0].length = len;
	viewlist[0].initialized = true;

	length = len;
	maxsize = len;
	own_memory = true;
	return true;
}

static size_w calc_index_base(size_w index)
{
	if(index < MEM_BLOCK_SIZE / 2)
		return 0;

	return ((index + MEM_BLOCK_SIZE / 4) & (~(MEM_BLOCK_SIZE / 2 - 1))) - (MEM_BLOCK_SIZE / 2);
}

static size_w calc_view_base(size_w offset, size_w length)
{
	size_w base = calc_index_base(offset);

	if(offset + length > base + MEM_BLOCK_SIZE)
		base = offset + length - MEM_BLOCK_SIZE;

	return base;
}

static bool read_data(void *file, seqchar *buffer, size_w offset, size_w length)
{
	FILE *fp = (FILE *)file;
	size_t bytes = length * sizeof(seqchar);

	if(fseek(fp, offset * sizeof(seqchar), SEEK_SET) != 0)
		return false;

	return fread(buffer, 1, bytes, fp) == bytes;
}

bool sequence::buffer_control::init_file(TCHAR *filename, bool read_only)
{
	FILE *file;
	long file_length;

	clear();
	readonly = read_only;

	if(!seq_fopen(&file, filename, readonly ? _T("rb") : _T("r+b")))
		file = 0;

	if(file == 0 && !readonly)
	{
		readonly = true;
		if(!seq_fopen(&file, filename, _T("rb")))
			file = 0;
	}

	if(file == 0)
		return false;

	if(fseek(file, 0, SEEK_END) != 0)
	{
		fclose(file);
		return false;
	}

	file_length = ftell(file);

	if(file_length < 0)
	{
		fclose(file);
		return false;
	}

	length = file_length / sizeof(seqchar);
	maxsize = length;
	own_memory = true;
	fp = file;

	return true;
}

//
//	buffer_control::append
//
//  used for 'modify' buffer - add new data
//
bool sequence::buffer_control::append(const seqchar *source, size_t len, size_t *buffer_offset)
{
	if(len > maxsize - length)
		return false;

	if(buffer_offset)
		*buffer_offset = length;

	memcpy(buffer + length, source, len * sizeof(seqchar));
	length += len;
	return true;
}

seqchar *sequence::buffer_control::getptr(size_w offset, size_w len)
{
	int i;

	if(offset > length || len > length - offset)
		return 0;

	if(fp == 0)
		return buffer + offset;

	// find and existing view that already contains the requested range
	for(i = 0; i < MAX_VIEWS; i++)
	{
		buffer_view *bv = &viewlist[i];

		if(bv->initialized && offset >= bv->offset && offset + len <= bv->offset + bv->length)
			return bv->buffer + (offset - bv->offset);
	}

	if(len > MEM_BLOCK_SIZE)
		return 0;

	buffer_view *bv = &viewlist[0];

	// no existing view... find an unused view slot before we
	// reuse slot 0
	for(i = 0; i < MAX_VIEWS; i++)
	{
		if(!viewlist[i].initialized)
		{
			bv = &viewlist[i];
			break;
		}
	}

	if(!bv->initialized)
	{
		bv->buffer = new seqchar[MEM_BLOCK_SIZE];
		bv->initialized = true;
	}

	// translate from file coordinate to the buffer-based offset
	bv->offset = calc_view_base(offset, len);
	bv->length = MEM_BLOCK_SIZE;

	if(bv->offset + bv->length > length)
		bv->length = length - bv->offset;

	if(offset + len > bv->offset + bv->length)
		return 0;

	// finally read the data into the newly allocated buffer
	if(!read_data(fp, bv->buffer, bv->offset, bv->length))
		return 0;

	return bv->buffer + (offset - bv->offset);
}

void sequence::buffer_control::clear()
{
	int i;

	if(fp)
	{
		fclose((FILE *)fp);
		fp = 0;
	}

	for(i = 0; i < MAX_VIEWS; i++)
	{
		if(viewlist[i].initialized && own_memory)
			delete[] viewlist[i].buffer;

		viewlist[i].buffer = 0;
		viewlist[i].offset = 0;
		viewlist[i].length = 0;
		viewlist[i].initialized = false;
	}

	buffer = 0;
	length = 0;
	maxsize = 0;
	own_memory = true;
	readonly = false;
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

	return true;
}
