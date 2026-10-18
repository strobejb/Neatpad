// Recovery at the view boundary. Sequence persistence stays independent of
// encoding, filenames and caret/selection/scroll positions.
#define STRICT
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tchar.h>
#include <memory>
#include "TextView.h"
#include "TextViewInternal.h"
#include "sequence_state.h"

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

BOOL TextView::SaveState(TEXTVIEWRECOVERY *state)
{
    if (!state || !state->directory)
        return FALSE;
    state->originalUnavailable = FALSE;
    try
    {
        std::vector<uint8_t> context;
        size_t               length = 0;

        while (length < MAX_PATH && state->filename[length])
            ++length;

        if (length == MAX_PATH)
            return FALSE;

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
        return FALSE;
    }
}

BOOL TextView::LoadState(TEXTVIEWRECOVERY *state)
{
    if (!state || !state->directory)
        return FALSE;
    state->originalUnavailable = FALSE;
    try
    {
        state_reader reader(state->directory);

        std::vector<uint8_t> context;

        if (!reader.read_context(context) || context.size() < 56)
            return FALSE;

        size_t at = 16;

        if (get(context, at, 4) != 0x5756504e || get(context, at, 4) != 1)
            return FALSE;

        uint32_t crlf   = (uint32_t)get(context, at, 4);
        size_t   length = (size_t)get(context, at, 4);
        uint64_t cursor = get(context, at, 8), anchor = get(context, at, 8), top = get(context, at, 8);

        if (length >= MAX_PATH || context.size() != at + length * 2 ||
            (crlf != TXL_CR && crlf != TXL_LF && crlf != TXL_CRLF))
            return FALSE;

        TCHAR filename[MAX_PATH];

        for (size_t i = 0; i < length; ++i)
        {
            filename[i] = (TCHAR)get(context, at, 2);
            if (!filename[i])
                return FALSE;
        }
        filename[length] = 0;

        std::unique_ptr<TextDocument> document(new TextDocument);

        document->set_in_memory_limit(FileDoc()->in_memory_limit());
        if (!document->load_state(reader))
        {
            state->originalUnavailable = reader.original_unavailable();
            return FALSE;
        }
        if (!document->modified())
            return FALSE;
        if (cursor > document->byte_length() || anchor > document->byte_length() || top > document->byte_length())
            return FALSE;

        TextCoord caret, selection, scroll;

        if (!document->coord_from_pos(TextPos(cursor), &caret) ||
            !document->coord_from_pos(TextPos(anchor), &selection) || !document->coord_from_pos(TextPos(top), &scroll))
            return FALSE;

        ClearFile();
        delete m_pTextDoc;

        m_pTextDoc    = document.release();
        m_fFilter     = FALSE;

        m_sMatchText.clear();
        m_nEditMode   = MODE_INSERT;
        m_nCRLFMode   = crlf;
        m_nHScrollPos = 0;
        m_cursorPos   = caret;
        m_selAnchor   = selection;

        SetScrollCoord(&scroll);
        m_nLineCount = m_pTextDoc->linecount();

        ResetLineCache();
        UpdateViewState(TRUE);
        SetScrollCoord(&scroll);
        SetupScrollbars();
        RepositionCaret();
        RefreshWindow();
        NotifyCursorChange();
        NotifyIndexChange();
        NotifyParent(TVN_CHANGED);
        lstrcpy(state->filename, filename);

        return TRUE;
    }
    catch (...)
    {
        return FALSE;
    }
}
