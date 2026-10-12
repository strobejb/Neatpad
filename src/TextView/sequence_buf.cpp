/*
	sequence_buf.cpp

	Backing-buffer management for the data-sequence class.
*/

#include <stdio.h>
#include <string.h>
#include "sequence.h"

// 64-bit file positions. Elsewhere, fseeko/ftello are 64-bit when built with
// _FILE_OFFSET_BITS=64.
#ifdef _WIN32
#define seq_fseek(fp, offset, origin) _fseeki64((fp), (offset), (origin))
#define seq_ftell(fp) _ftelli64(fp)
#else
#define seq_fseek(fp, offset, origin) fseeko((fp), (off_t)(offset), (origin))
#define seq_ftell(fp) ((long long)ftello(fp))
#endif

// Open a file to read, letting others write it or not (at the end of the file)
static FILE *seq_fopen_shared(const seqfilechar *filename, bool others_write);

sequence::buffer_control::buffer_control()
{
	int i;

	buffer = 0;
	line_pages = 0;
	line_page_count = 0;
	scanned_bytes = 0;
	scanned_breaks = 0;
	length = 0;
	maxsize = 0;
	fp = 0;
	own_memory = true;
	readonly = false;
	id = 0;
	line_mode = line_scan_bytes;

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
	return alloc_line_pages(max);
}

bool sequence::buffer_control::init(const seqchar *source, size_w len)
{
	clear();

	if(len > (size_w)(size_t)-1)
		return false;

	if((buffer = new seqchar[(size_t)len]) == 0)
		return false;

	memcpy(buffer, source, (size_t)len * sizeof(seqchar));

	viewlist[0].buffer = buffer;
	viewlist[0].offset = 0;
	viewlist[0].length = len;
	viewlist[0].initialized = true;

	length = len;
	maxsize = len;
	own_memory = true;
	return alloc_line_pages(len);
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
	size_t bytes = (size_t)length * sizeof(seqchar);

	if(seq_fseek(fp, (long long)(offset * sizeof(seqchar)), SEEK_SET) != 0)
		return false;

	return fread(buffer, 1, bytes, fp) == bytes;
}

//
//	The length of an open file in seqchars, or -1 if it can't be told
//
static long long file_length_of(FILE *file)
{
	long long bytes;

	if(seq_fseek(file, 0, SEEK_END) != 0 || (bytes = seq_ftell(file)) < 0)
		return -1;

	return bytes / (long long)sizeof(seqchar);
}

//
//	Read a file shorter than in_memory_limit seqchars into memory, and close it,
//	leaving others free to write it. A longer one is read from disk as it's
//	needed, so it stays open, and no one else can write it until it's closed -
//	unless another program has it open to write already. That one, and one
//	opened read_only, is read from disk sharing it with the programs that write
//	it: read-only, for them to append to (see grow).
//
bool sequence::buffer_control::init_file(const seqfilechar *filename, bool read_only, size_w in_memory_limit)
{
	FILE *file;
	FILE *locked;
	long long file_length;
	bool read;

	clear();

	// open it, sharing it with any program writing it, to see how long it is
	if((file = seq_fopen_shared(filename, true)) == 0)
		return false;

	if((file_length = file_length_of(file)) < 0)
	{
		fclose(file);
		return false;
	}

	// short enough to read into memory (and to fit this build's address space)
	if(!read_only && (size_w)file_length < in_memory_limit && (size_w)file_length <= (size_w)(size_t)-1)
	{
		read = init((size_t)file_length) && read_data(file, buffer, 0, file_length);
		fclose(file);

		length = maxsize;
		return read;
	}

	// open it again, so that no one can write it while it's read from - if no
	// one is writing it already
	if(!read_only && (locked = seq_fopen_shared(filename, false)) != 0)
	{
		fclose(file);
		file = locked;
		file_length = file_length_of(file);
	}
	else
	{
		read_only = true;
	}

	if(file_length < 0)
	{
		fclose(file);
		return false;
	}

	readonly = read_only;
	length = (size_w)file_length;
	maxsize = length;
	own_memory = true;
	fp = file;

	if(!alloc_line_pages(length))
	{
		fclose(file);
		fp = 0;
		return false;
	}

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
		*buffer_offset = (size_t)length;

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

	free_line_pages();

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
//	Allocate one line page per LINE_PAGE_SIZE of buffer capacity
//
bool sequence::buffer_control::alloc_line_pages(size_w size)
{
	free_line_pages();

	line_page_count = size / LINE_PAGE_SIZE + (size % LINE_PAGE_SIZE ? 1 : 0);

	if(line_page_count == 0)
		return true;

	if(line_page_count > (size_w)(size_t)-1)
		return false;

	if((line_pages = new line_page[(size_t)line_page_count]) == 0)
	{
		line_page_count = 0;
		return false;
	}

	reset_line_pages();
	return true;
}

void sequence::buffer_control::free_line_pages()
{
	delete[] line_pages;
	line_pages = 0;
	line_page_count = 0;
	scanned_bytes = 0;
	scanned_breaks = 0;
}

//
//	Make room for the line pages of a buffer grown to size, keeping those
//	already counted
//
bool sequence::buffer_control::extend_line_pages(size_w size)
{
	size_w     count = size / LINE_PAGE_SIZE + (size % LINE_PAGE_SIZE ? 1 : 0);
	line_page *pages;

	if(count <= line_page_count)
		return true;

	if(count > (size_w)(size_t)-1 || (pages = new line_page[(size_t)count]) == 0)
		return false;

	for(size_w page = 0; page < line_page_count; page++)
		pages[page] = line_pages[page];

	delete[] line_pages;
	line_pages      = pages;
	line_page_count = count;
	return true;
}

//
//	Take in what another program has appended to the file since it was opened,
//	or last grew. The pages counted so far stay counted; the last of them is
//	counted again, now it's longer. False if the file has got shorter: it's
//	been rewritten, not appended to.
//
bool sequence::buffer_control::grow()
{
	long long file_length;

	if(fp == 0 || (file_length = file_length_of((FILE *)fp)) < 0 || (size_w)file_length < length)
		return false;

	if(!extend_line_pages((size_w)file_length))
		return false;

	length  = (size_w)file_length;
	maxsize = length;
	return true;
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

	bc->set_line_scan_mode(line_mode);
	bc->id = (int)buffer_list.size();		// assign the id
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

//
//	Open a file to read, sharing it so that others can read it, and delete,
//	rename or replace it, and write to it if others_write says so. A file whose
//	text is read from it on demand mustn't let them: its bytes must not change
//	underneath. Being replaced while open is how a save swaps in its new file;
//	this handle still reads the old one until it is closed.
//
#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
#include <fcntl.h>

static FILE *seq_fopen_shared(const seqfilechar *filename, bool others_write)
{
	DWORD  share = FILE_SHARE_READ | FILE_SHARE_DELETE | (others_write ? FILE_SHARE_WRITE : 0);

	// the C runtime can't share delete, so open with CreateFile and wrap the handle
	HANDLE h = CreateFileW(filename, GENERIC_READ, share, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
	FILE  *fp;
	int    fd;

	if(h == INVALID_HANDLE_VALUE)
		return 0;

	if((fd = _open_osfhandle((intptr_t)h, _O_RDONLY | _O_BINARY)) == -1)
	{
		CloseHandle(h);
		return 0;
	}

	// from here, closing the descriptor (or the FILE) closes the handle too
	if((fp = _fdopen(fd, "rb")) == 0)
		_close(fd);

	return fp;
}

#else

// Other systems let an open file be renamed over or deleted anyway, and have
// no way to stop others writing to it
static FILE *seq_fopen_shared(const seqfilechar *filename, bool others_write)
{
	return fopen(filename, "rb");
}

#endif
