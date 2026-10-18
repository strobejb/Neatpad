#ifndef SEQUENCE_STATE_INCLUDED
#define SEQUENCE_STATE_INCLUDED

#include <stdint.h>
#include <string>
#include "sequence.h"

// A private directory belonging to one document incarnation (or its restored
// continuation). Start a new store after init/open/clear. Checkpoint calls are
// synchronous: the sequence must not change while being saved. The directory
// and its buffers must outlive every sequence restored from it.
//
// Originals are referenced with identity/size/timestamps; only modify buffers
// are copied to append-only buffer-N.bin files. Publishing
// state.bin commits their recorded prefixes; an interrupted write leaves the
// previous checkpoint usable. Neither class knows about the application's UI.
class state_writer
{
  public:
    explicit state_writer(const seqfilechar *directory) : directory(directory)
    {
    }

    // Extra recovery metadata, stored with the sequence but not interpreted here.
    // TextDocument prefixes its encoding/BOM header to TextView's filename and
    // caret/selection/scroll settings. This contains no document text or history.
    void set_context(const std::vector<uint8_t> &bytes)
    {
        context = bytes;
    }

    // Currently attached metadata; lets TextDocument prepend its header and
    // restore the caller's metadata after saving.
    const std::vector<uint8_t> &get_context() const
    {
        return context;
    }

    void set_original_file(const std::basic_string<seqfilechar> &name)
    {
        original_file = name;
    }

    const std::basic_string<seqfilechar> &get_original_file() const
    {
        return original_file;
    }

    // Before replacing the original on Save, retain referenced bases for undo.
    // Uses hard links where possible; otherwise copies at save time.
    bool preserve_sources();

  private:
    friend class sequence;

    std::basic_string<seqfilechar> directory, original_file;
    std::vector<uint8_t>           context;
};

class state_reader
{
  public:
    explicit state_reader(const seqfilechar *directory) : directory(directory)
    {
    }

    // Returns the extra recovery metadata and pins its validated manifest, so
    // metadata inspection and the following load_state use the same checkpoint.
    bool read_context(std::vector<uint8_t> &bytes);

    bool original_unavailable() const
    {
        return source_unavailable;
    }

  private:
    friend class sequence;

    std::basic_string<seqfilechar> directory;
    std::vector<uint8_t>           checkpoint;
    bool                           source_unavailable = false;
};

#endif
