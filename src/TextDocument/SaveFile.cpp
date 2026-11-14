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

TempAppendFile::TempAppendFile()
{
	m_file = -1;
}

TempAppendFile::~TempAppendFile()
{
	close();
}

const seqfilechar *TempAppendFile::path() const
{
	return m_path.c_str();
}

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

//
//	The temporary file goes when its last handle closes. Others may read,
//	write and delete it, so a document can read it as it's written.
//
bool TempAppendFile::create()
{
	wchar_t dir[MAX_PATH];
	wchar_t path[MAX_PATH];
	HANDLE  file;

	close();

	if(!GetTempPathW(MAX_PATH, dir) || !GetTempFileNameW(dir, L"npf", 0, path))
		return false;

	file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0,
					   CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, 0);

	if(file == INVALID_HANDLE_VALUE)
	{
		DeleteFileW(path);
		return false;
	}

	m_path = path;
	m_file = (intptr_t)file;
	return true;
}

bool TempAppendFile::write(const void *data, size_t length)
{
	DWORD written = 0;

	return m_file != -1 && length <= MAXDWORD && WriteFile((HANDLE)m_file, data, (DWORD)length, &written, 0) &&
		   written == length;
}

void TempAppendFile::close()
{
	if(m_file != -1)
		CloseHandle((HANDLE)m_file);

	m_file = -1;
	m_path.clear();
}

#else

#include <stdlib.h>
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

//
//	The temporary file is unlinked when it's closed: a document that still
//	has it open keeps reading it until it closes it too
//
bool TempAppendFile::create()
{
	const char *dir = getenv("TMPDIR");
	std::string path;
	int         fd;

	close();

	path = std::string(dir && *dir ? dir : "/tmp") + "/npfXXXXXX";
	fd   = mkstemp(&path[0]);

	if(fd == -1)
		return false;

	m_path = path;
	m_file = fd;
	return true;
}

bool TempAppendFile::write(const void *data, size_t length)
{
	const char *bytes = (const char *)data;

	while(m_file != -1 && length > 0)
	{
		ssize_t written = ::write((int)m_file, bytes, length);

		if(written < 0 && errno == EINTR)
			continue;

		if(written <= 0)
			return false;

		bytes  += written;
		length -= (size_t)written;
	}

	return m_file != -1;
}

void TempAppendFile::close()
{
	if(m_file != -1)
	{
		::close((int)m_file);
		unlink(m_path.c_str());
	}

	m_file = -1;
	m_path.clear();
}

#endif
