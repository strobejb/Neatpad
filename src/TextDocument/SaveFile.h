#ifndef SAVEFILE_INCLUDED
#define SAVEFILE_INCLUDED

#include <stdio.h>
#include <string>
#include "sequence.h"		// seqfilechar

//
//	A file that is saved safely, as with Qt's QSaveFile: everything is written
//	to a new file beside the target, which replaces the target at commit(),
//	once all of it is on the disk. Until then the target is untouched, and if
//	anything fails, or commit() is never reached, it stays as it was.
//
class SaveFile
{
public:
	SaveFile();
	~SaveFile();						// discards a file that wasn't committed

	bool	open(const seqfilechar *filename);
	bool	write(const void *data, size_t length);
	bool	commit();

private:
	void	discard();

	std::basic_string<seqfilechar> m_target;
	std::basic_string<seqfilechar> m_tempname;	// empty until the new file exists
	FILE   *m_fp;
};

#endif
