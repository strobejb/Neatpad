//
//	SaveFile.cpp
//
//	Saving a file safely: see SaveFile.h
//

#include <errno.h>
#include "SaveFile.h"

// File access that differs between systems (at the end of the file)
static FILE *create_new(const seqfilechar *filename);
static bool  sync_to_disk(FILE *fp);
static bool  replace_file(const seqfilechar *from, const seqfilechar *to);
static void  remove_file(const seqfilechar *filename);

SaveFile::SaveFile()
{
	m_fp = 0;
}

SaveFile::~SaveFile()
{
	discard();
}

//
//	Create the new file beside the target: the target's name plus ".~0", or
//	".~1" if that is taken, and so on
//
bool SaveFile::open(const seqfilechar *filename)
{
	discard();
	m_target = filename;

	for(int i = 0; i < 10; i++)
	{
		std::basic_string<seqfilechar> name = filename;

		name += '.';
		name += '~';
		name += (seqfilechar)('0' + i);

		if((m_fp = create_new(name.c_str())) != 0)
		{
			m_tempname = name;
			return true;
		}

		if(errno != EEXIST)
			break;
	}

	return false;
}

bool SaveFile::write(const void *data, size_t length)
{
	return m_fp != 0 && (length == 0 || fwrite(data, 1, length, m_fp) == length);
}

//
//	Put everything written on the disk, then replace the target with it
//
bool SaveFile::commit()
{
	bool written;

	if(m_fp == 0)
		return false;

	written = sync_to_disk(m_fp);
	written = fclose(m_fp) == 0 && written;
	m_fp = 0;

	if(!written || !replace_file(m_tempname.c_str(), m_target.c_str()))
	{
		discard();
		return false;
	}

	m_tempname.clear();
	return true;
}

void SaveFile::discard()
{
	if(m_fp)
	{
		fclose(m_fp);
		m_fp = 0;
	}

	if(!m_tempname.empty())
	{
		remove_file(m_tempname.c_str());
		m_tempname.clear();
	}
}

//
//	File access that differs between systems
//
#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>

// Create a file to write, failing (with errno EEXIST) if it already exists
static FILE *create_new(const seqfilechar *filename)
{
	FILE *fp = 0;

	errno = _wfopen_s(&fp, filename, L"wbx");
	return fp;
}

// Write everything buffered for the file through to the disk
static bool sync_to_disk(FILE *fp)
{
	return fflush(fp) == 0 && _commit(_fileno(fp)) == 0;
}

//
//	Move a file over another, or to where there is none. ReplaceFile keeps the
//	replaced file's attributes, security and creation time, and works while
//	the replaced file is open - as a document reading it on demand has it. If
//	ReplaceFile removed the replaced file but then couldn't move the new one
//	into its place, the move is tried again on its own.
//
static bool replace_file(const seqfilechar *from, const seqfilechar *to)
{
	if(ReplaceFileW(to, from, 0, REPLACEFILE_IGNORE_MERGE_ERRORS, 0, 0))
		return true;

	switch(GetLastError())
	{
	case ERROR_FILE_NOT_FOUND:
	case ERROR_UNABLE_TO_MOVE_REPLACEMENT:
		return MoveFileExW(from, to, MOVEFILE_WRITE_THROUGH) != 0;

	default:
		return false;
	}
}

// Delete a file, keeping the last error, which says why the save failed
static void remove_file(const seqfilechar *filename)
{
	DWORD err = GetLastError();

	DeleteFileW(filename);
	SetLastError(err);
}

#else

#include <sys/stat.h>
#include <unistd.h>

static FILE *create_new(const seqfilechar *filename)
{
	return fopen(filename, "wbx");
}

static bool sync_to_disk(FILE *fp)
{
	return fflush(fp) == 0 && fsync(fileno(fp)) == 0;
}

// Renaming over a file replaces it in one step; the new file is given the
// replaced file's permissions first
static bool replace_file(const seqfilechar *from, const seqfilechar *to)
{
	struct stat st;

	if(stat(to, &st) == 0)
		chmod(from, st.st_mode & 07777);

	return rename(from, to) == 0;
}

static void remove_file(const seqfilechar *filename)
{
	remove(filename);
}

#endif
