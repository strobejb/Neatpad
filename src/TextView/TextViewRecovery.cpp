// Recovery at the view boundary. Sequence persistence stays independent of
// encoding, filenames and caret/selection/scroll positions.
#define STRICT
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tchar.h>
#include <memory>
#include "TextView.h"
#include "TextViewInternal.h"
#include "../TextDocument/sequence_state.h"

namespace
{
void put(std::vector<uint8_t> &out, uint64_t value, unsigned bytes)
{
    for (unsigned i = 0; i < bytes; ++i)
        out.push_back((uint8_t)(value >> (i * 8)));
}

uint64_t get(const std::vector<uint8_t> &in, size_t &at, unsigned bytes)
{
    uint64_t value = 0;

    for (unsigned i = 0; i < bytes; ++i)
        value |= (uint64_t)in[at++] << (i * 8);
    return value;
}
} // namespace

bool TextView::SaveState(TextViewRecovery *state)
{
    if (!state || !state->directory)
        return false;
    state->originalUnavailable = false;
    try
    {
        std::vector<uint8_t> context;
        size_t               length = state->filename.size();

        if (length > TV_RECOVERY_NAME_MAX)
            return false;

        put(context, 0x5756504e, 4);
        put(context, 1, 4);
        put(context, m_nCRLFMode, 4);
        put(context, length, 4);
        put(context, m_pSourceDoc ? 0 : m_cursorPos.byte_offset, 8);
        put(context, m_pSourceDoc ? 0 : m_selAnchor.byte_offset, 8);
        put(context, m_pSourceDoc ? 0 : m_scrollVPos.byte_offset, 8);

        for (size_t i = 0; i < length; ++i)
            put(context, (uint16_t)state->filename[i], 2);

        state_writer writer(state->directory);
        writer.set_context(context);

        return FileDoc()->save_state(writer) && (!state->preserveSources || writer.preserve_sources());
    }
    catch (...)
    {
        return false;
    }
}

bool TextView::LoadState(TextViewRecovery *state)
{
    if (!state || !state->directory)
        return false;
    state->originalUnavailable = false;
    try
    {
        state_reader reader(state->directory);

        std::vector<uint8_t> context;

        if (!reader.read_context(context) || context.size() < 56)
            return false;

        size_t at = 16;

        if (get(context, at, 4) != 0x5756504e || get(context, at, 4) != 1)
            return false;

        uint32_t crlf   = (uint32_t)get(context, at, 4);
        size_t   length = (size_t)get(context, at, 4);
        uint64_t cursor = get(context, at, 8), anchor = get(context, at, 8), top = get(context, at, 8);

        if (length > TV_RECOVERY_NAME_MAX || context.size() != at + length * 2 ||
            (crlf != TXL_CR && crlf != TXL_LF && crlf != TXL_CRLF))
            return false;

        std::basic_string<seqfilechar> filename(length, 0);

        for (size_t i = 0; i < length; ++i)
        {
            filename[i] = (seqfilechar)get(context, at, 2);
            if (!filename[i])
                return false;
        }

        std::unique_ptr<TextDocument> document(new TextDocument);

        document->set_in_memory_limit(FileDoc()->in_memory_limit());
        if (!document->load_state(reader))
        {
            state->originalUnavailable = reader.original_unavailable();
            return false;
        }
        if (!document->modified())
            return false;
        if (cursor > document->byte_length() || anchor > document->byte_length() || top > document->byte_length())
            return false;

        TextCoord caret, selection, scroll;

        if (!document->coord_from_pos(TextPos(cursor), &caret) ||
            !document->coord_from_pos(TextPos(anchor), &selection) || !document->coord_from_pos(TextPos(top), &scroll))
            return false;

        ClearFile();
        delete m_pTextDoc;

        m_pTextDoc    = document.release();
        m_fFilter     = false;

        m_sMatchText.clear();
        m_nEditMode   = MODE_INSERT;
        m_nCRLFMode   = crlf;
        m_nHScrollPos = 0;
        m_cursorPos   = caret;
        m_selAnchor   = selection;

        SetScrollCoord(&scroll);
        m_nLineCount = m_pTextDoc->linecount();

        ResetLineCache();
        UpdateViewState(true);
        SetScrollCoord(&scroll);
        SetupScrollbars();
        RepositionCaret();
        RefreshWindow();
        NotifyCursorChange();
        NotifyIndexChange();
        NotifyParent(TVNOTIFY_CHANGED);
        state->filename = filename;

        return true;
    }
    catch (...)
    {
        return false;
    }
}
