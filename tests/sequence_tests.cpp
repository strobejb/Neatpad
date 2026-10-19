//
//	Sequence tests: inserting, erasing and replacing across spans, undo and
//	redo, and reading files into a sequence
//
#include "tests.h"
#include <tchar.h>
#include "../src/TextView/sequence_state.h"

// Inspect buffer ownership without adding a public sequence API for tests.
class SequenceBufferProbe
{
  public:
    static size_t buffer_count(const sequence &seq)
    {
        return seq.buffer_list.size();
    }

    static int current_modify(const sequence &seq)
    {
        return seq.modifybuffer_id;
    }

    static size_w buffer_length(const sequence &seq, size_t id)
    {
        return seq.buffer_list[id]->length;
    }

    static std::string buffer_text(sequence &seq, size_t id)
    {
        sequence::buffer_control *buffer = seq.buffer_list[id];

        if (!buffer->length)
            return {};

        return std::string(reinterpret_cast<const char *>(buffer->getptr(0, buffer->length)),
                           static_cast<size_t>(buffer->length));
    }

    static std::vector<int> span_buffers(const sequence &seq)
    {
        std::vector<int> buffers;

        for (sequence::span *span = seq.head->next; span != seq.tail; span = span->next)
        {
            if (span->length)
                buffers.push_back(span->buffer);
        }
        return buffers;
    }

    // Reproduce the old init(buffer) layout before any history exists: empty
    // modify buffer 0, initial data in full buffer 1, still selected for edits.
    static void use_legacy_layout(sequence &seq)
    {
        std::swap(seq.buffer_list[0], seq.buffer_list[1]);
        seq.buffer_list[0]->id = 0;
        seq.buffer_list[1]->id = 1;

        for (sequence::span *span = seq.head->next; span != seq.tail; span = span->next)
            span->buffer = 1;

        seq.modifybuffer_id  = 1;
        seq.modifybuffer_pos = 0;
    }
};

namespace
{
struct checkpoint_store
{
    TCHAR dir[MAX_PATH];

    checkpoint_store()
    {
        CHECK(write_temp_file("", 0, dir));
        CHECK(DeleteFile(dir));
        CHECK(CreateDirectory(dir, 0));
    }

    ~checkpoint_store()
    {
        TCHAR           pattern[MAX_PATH], name[MAX_PATH];
        WIN32_FIND_DATA data;
        wsprintf(pattern, _T("%s\\*"), dir);
        HANDLE find = FindFirstFile(pattern, &data);
        if (find != INVALID_HANDLE_VALUE)
        {
            do
            {
                if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                {
                    wsprintf(name, _T("%s\\%s"), dir, data.cFileName);
                    DeleteFile(name);
                }
            } while (FindNextFile(find, &data));
            FindClose(find);
        }
        RemoveDirectory(dir);
    }
};

void sequence_initial_and_modify_buffers_are_separate()
{
    sequence seq;

    CHECK(seq.init());
    CHECK(SequenceBufferProbe::buffer_count(seq) == 2);
    CHECK(SequenceBufferProbe::current_modify(seq) == 1);
    CHECK(SequenceBufferProbe::buffer_length(seq, 0) == 0);
    CHECK(insert_bytes(seq, 0, "typed"));
    CHECK(SequenceBufferProbe::span_buffers(seq) == std::vector<int>({1}));
    CHECK(SequenceBufferProbe::buffer_length(seq, 0) == 0);

    // Reinitializing a used sequence also removes its buffers and history.
    CHECK(seq.init());
    CHECK(seq.size() == 0 && !seq.canundo() && !seq.canredo() && !seq.modified());
    CHECK(SequenceBufferProbe::buffer_count(seq) == 2);
    CHECK(SequenceBufferProbe::current_modify(seq) == 1);

    init(seq, "abcdef");
    CHECK(SequenceBufferProbe::span_buffers(seq) == std::vector<int>({0}));
    CHECK(SequenceBufferProbe::buffer_text(seq, 0) == "abcdef");
    CHECK(SequenceBufferProbe::current_modify(seq) == 1);
    CHECK(insert_bytes(seq, 2, "XY"));
    CHECK(SequenceBufferProbe::span_buffers(seq) == std::vector<int>({0, 1, 0}));
    CHECK(SequenceBufferProbe::buffer_text(seq, 0) == "abcdef");
    CHECK(seq.undo());
    expect_content(seq, "abcdef");
    CHECK(SequenceBufferProbe::span_buffers(seq) == std::vector<int>({0}));
    CHECK(seq.redo());
    expect_content(seq, "abXYcdef");
    CHECK(SequenceBufferProbe::span_buffers(seq) == std::vector<int>({0, 1, 0}));
}

void sequence_modify_buffers_roll_over_without_changing_initial_data()
{
    sequence    seq;
    std::string typed(0x10000, 'x');

    init(seq, "abc");
    CHECK(seq.insert(seq.size(), reinterpret_cast<const seqchar *>(typed.data()), typed.size()));
    CHECK(SequenceBufferProbe::current_modify(seq) == 1);
    CHECK(SequenceBufferProbe::span_buffers(seq) == std::vector<int>({0, 1}));
    CHECK(insert_bytes(seq, seq.size(), "y"));
    CHECK(SequenceBufferProbe::buffer_count(seq) == 3);
    CHECK(SequenceBufferProbe::current_modify(seq) == 2);
    CHECK(SequenceBufferProbe::span_buffers(seq) == std::vector<int>({0, 1, 2}));
    CHECK(SequenceBufferProbe::buffer_text(seq, 0) == "abc");
    CHECK(SequenceBufferProbe::buffer_text(seq, 1) == typed);

    // Buffer rollover must break coalescing: the two insertions undo separately.
    CHECK(seq.undo());
    CHECK(SequenceBufferProbe::span_buffers(seq) == std::vector<int>({0, 1}));
    CHECK(seq.undo());
    expect_content(seq, "abc");
    CHECK(SequenceBufferProbe::span_buffers(seq) == std::vector<int>({0}));
    CHECK(seq.redo());
    CHECK(seq.redo());
    CHECK(render_content(seq) == "abc" + typed + "y");
    CHECK(SequenceBufferProbe::span_buffers(seq) == std::vector<int>({0, 1, 2}));
    CHECK(SequenceBufferProbe::buffer_text(seq, 0) == "abc");
}

void sequence_file_initial_buffer_is_zero()
{
    TCHAR       file[MAX_PATH];
    const char *text = "first\nlast";

    CHECK(write_temp_file(text, strlen(text), file));
    for (size_w in_memory_limit : {size_w(0), size_w(64)})
    {
        sequence seq;

        CHECK(seq.open(file, false, in_memory_limit));
        CHECK(SequenceBufferProbe::buffer_count(seq) == 2);
        CHECK(SequenceBufferProbe::current_modify(seq) == 1);
        CHECK(SequenceBufferProbe::span_buffers(seq) == std::vector<int>({0}));
        CHECK(SequenceBufferProbe::buffer_text(seq, 0) == text);
        CHECK(insert_bytes(seq, 6, "new "));
        CHECK(SequenceBufferProbe::span_buffers(seq) == std::vector<int>({0, 1, 0}));
        CHECK(SequenceBufferProbe::buffer_text(seq, 0) == text);
        CHECK(seq.undo());
        CHECK(SequenceBufferProbe::span_buffers(seq) == std::vector<int>({0}));
        CHECK(seq.redo());
        expect_content(seq, "first\nnew last");
        CHECK(SequenceBufferProbe::span_buffers(seq) == std::vector<int>({0, 1, 0}));
    }
    CHECK(DeleteFile(file));
}

void checkpoint_preserves_buffer_origins_and_legacy_ids()
{
    for (bool legacy : {false, true})
    {
        checkpoint_store store;
        sequence         seq, copy, latest;
        int              initial = legacy ? 1 : 0;

        init(seq, "abc");
        if (legacy)
            SequenceBufferProbe::use_legacy_layout(seq);

        CHECK(insert_bytes(seq, 1, "XY"));
        CHECK(SequenceBufferProbe::span_buffers(seq) == std::vector<int>({initial, legacy ? 2 : 1, initial}));

        state_writer writer(store.dir);
        state_reader reader(store.dir);
        size_t       previous_count = SequenceBufferProbe::buffer_count(seq);

        CHECK(seq.save_state(writer));
        CHECK(copy.load_state(reader));
        CHECK(SequenceBufferProbe::buffer_count(copy) == previous_count + 1);
        CHECK(SequenceBufferProbe::current_modify(copy) == static_cast<int>(previous_count));
        CHECK(SequenceBufferProbe::span_buffers(copy) == SequenceBufferProbe::span_buffers(seq));
        CHECK(SequenceBufferProbe::buffer_text(copy, initial) == "abc");
        CHECK(insert_bytes(copy, copy.size(), "!"));
        CHECK(SequenceBufferProbe::buffer_length(copy, previous_count) == 1);
        CHECK(SequenceBufferProbe::buffer_text(copy, initial) == "abc");
        CHECK(copy.undo());
        CHECK(SequenceBufferProbe::span_buffers(copy) == SequenceBufferProbe::span_buffers(seq));
        CHECK(copy.redo());
        expect_content(copy, "aXYbc!");
        CHECK(copy.save_state(writer));
        CHECK(latest.load_state(reader));
        CHECK(SequenceBufferProbe::span_buffers(latest) == SequenceBufferProbe::span_buffers(copy));
        CHECK(SequenceBufferProbe::buffer_text(latest, initial) == "abc");
        CHECK(latest.undo());
        CHECK(latest.undo());
        expect_content(latest, "abc");
        CHECK(SequenceBufferProbe::span_buffers(latest) == std::vector<int>({initial}));
        CHECK(latest.redo());
        CHECK(latest.redo());
        expect_content(latest, "aXYbc!");
    }
}

void checkpoint_preserves_both_stacks_groups_and_saved_state()
{
    checkpoint_store store;
    sequence         seq, copy;
    init(seq, "one\r\ntwo\nthree");
    seq.set_line_scan_mode(sequence::line_scan_utf8);
    seq.group();
    CHECK(insert_bytes(seq, 0, "start\n"));
    CHECK(replace_bytes(seq, 6, "ONE", 3));
    seq.ungroup();
    seq.mark_saved();
    CHECK(seq.erase(0, 6));
    seq.breakopt();
    CHECK(insert_bytes(seq, seq.size(), "\nend"));
    CHECK(seq.undo());
    state_writer writer(store.dir);
    CHECK(seq.save_state(writer));
    state_reader reader(store.dir);
    init(copy, "replace me");
    CHECK(copy.load_state(reader));
    CHECK(render_content(copy) == render_content(seq));
    CHECK(copy.modified() == seq.modified());
    CHECK(copy.canundo() && copy.canredo());
    while (seq.canundo())
    {
        CHECK(seq.undo());
        CHECK(copy.undo());
        CHECK(render_content(copy) == render_content(seq));
        CHECK(copy.modified() == seq.modified());
        size_w a, b, c, x, y, z;
        seq.event_change(&a, &b, &c);
        copy.event_change(&x, &y, &z);
        CHECK(a == x && b == y && c == z);
    }
    CHECK(!copy.canundo());
    while (seq.canredo())
    {
        CHECK(seq.redo());
        CHECK(copy.redo());
        CHECK(render_content(copy) == render_content(seq));
        CHECK(copy.modified() == seq.modified());
    }
    CHECK(!copy.canredo());
    copy.breakopt();
    seq.breakopt();
    CHECK(insert_bytes(copy, 0, "new"));
    CHECK(insert_bytes(seq, 0, "new"));
    CHECK(copy.undo());
    CHECK(seq.undo());
    CHECK(render_content(copy) == render_content(seq));
}

void checkpoint_references_original_and_retains_it_before_save()
{
    checkpoint_store store;
    TCHAR            file[MAX_PATH];
    std::string      text = utf8_text((size_t)MEM_BLOCK_SIZE * 5);
    CHECK(write_temp_file(text.data(), text.size(), file));
    sequence seq, copy;
    CHECK(seq.open(file, false));
    seq.set_line_scan_mode(sequence::line_scan_utf8);
    CHECK(insert_bytes(seq, 25, "extra\r\n"));
    state_writer writer(store.dir);
    CHECK(seq.save_state(writer));
    TCHAR base[MAX_PATH];
    wsprintf(base, TEXT("%s\\buffer-0.bin"), store.dir);
    CHECK(GetFileAttributes(base) == INVALID_FILE_ATTRIBUTES); // original was referenced
    // Simulate an uncommitted leftover: hard-link creation cannot replace it,
    // so retention exercises the copy fallback before publishing the new path.
    wsprintf(base, TEXT("%s\\base-0.bin"), store.dir);
    FILE *leftover = 0;
    _wfopen_s(&leftover, base, L"wb");
    CHECK(leftover != 0);
    if (leftover)
    {
        fwrite("leftover", 1, 8, leftover);
        fclose(leftover);
    }
    CHECK(writer.preserve_sources()); // ordinary Save must keep the old base for undo
    CHECK(seq.save_state(writer));
    seq.clear();
    CHECK(DeleteFile(file));
    state_reader reader(store.dir);
    CHECK(copy.load_state(reader));
    CHECK(copy.size() == text.size() + 7);
    while (copy.index_next(MEM_BLOCK_SIZE))
    {
    }
    CHECK(copy.linecount_known());
    CHECK(copy.undo());
    CHECK(render_content(copy) == text);
    CHECK(copy.redo());
    CHECK(copy.modified());
    CHECK(copy.save_state(writer));
}

void checkpoint_rejects_groups_and_bad_data_without_changing_destination()
{
    checkpoint_store store;
    sequence         seq, copy;
    init(seq, "base");
    init(copy, "untouched");
    state_writer writer(store.dir);
    state_reader reader(store.dir);
    CHECK(seq.save_state(writer));
    seq.group();
    CHECK(insert_bytes(seq, 0, "group"));
    CHECK(!seq.save_state(writer));
    seq.ungroup();
    CHECK(copy.load_state(reader));
    expect_content(copy, "base");
    copy.clear();
    CHECK(seq.save_state(writer));
    // Interrupt a later append: extra uncommitted buffer bytes are ignored.
    TCHAR path[MAX_PATH];
    wsprintf(path, _T("%s\\buffer-0.bin"), store.dir);
    FILE *fp = 0;
    _wfopen_s(&fp, path, L"ab");
    CHECK(fp != 0);
    if (fp)
    {
        fwrite("uncommitted", 1, 11, fp);
        fclose(fp);
    }
    CHECK(copy.load_state(reader));
    CHECK(render_content(copy) == render_content(seq));
    copy.clear();
    init(copy, "untouched");
    wsprintf(path, _T("%s\\state.bin"), store.dir);
    _wfopen_s(&fp, path, L"r+b");
    CHECK(fp != 0);
    if (fp)
    {
        fseek(fp, 40, SEEK_SET);
        int ch = fgetc(fp);
        fseek(fp, 40, SEEK_SET);
        fputc(ch ^ 0x80, fp);
        fclose(fp);
    }
    CHECK(!copy.load_state(reader));
    expect_content(copy, "untouched");
}

void checkpoint_large_original_writes_only_modified_bytes()
{
    checkpoint_store store;
    TCHAR            original[MAX_PATH];
    if (!write_sparse_file("head\n", "tail", 8ULL * 1024 * 1024 * 1024, original))
        return;
    sequence seq;
    CHECK(seq.open(original, false));
    CHECK(insert_bytes(seq, 2, "edit"));
    state_writer writer(store.dir);
    CHECK(seq.save_state(writer));
    TCHAR pattern[MAX_PATH];
    wsprintf(pattern, TEXT("%s\\*"), store.dir);
    WIN32_FIND_DATA data;
    ULONGLONG       total = 0;
    HANDLE          find  = FindFirstFile(pattern, &data);
    if (find != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                total += ((ULONGLONG)data.nFileSizeHigh << 32) | data.nFileSizeLow;
        } while (FindNextFile(find, &data));
        FindClose(find);
    }
    CHECK(total < 4096); // includes the entire manifest and all persisted buffers
    seq.clear();
    CHECK(DeleteFile(original));
}

void checkpoint_rejects_changed_or_missing_original_without_replacing_destination()
{
    for (int kind = 0; kind < 5; ++kind)
    {
        checkpoint_store store;
        TCHAR            original[MAX_PATH];
        CHECK(write_temp_file("abcdef", 6, original));
        sequence seq, copy;
        CHECK(seq.open(original, false));
        CHECK(insert_bytes(seq, 2, "edit"));
        state_writer writer(store.dir);
        CHECK(seq.save_state(writer));
        state_reader reader(store.dir);
        CHECK(copy.load_state(reader));
        expect_content(copy, "abeditcdef");
        copy.clear();
        seq.clear();
        WIN32_FILE_ATTRIBUTE_DATA before;
        CHECK(GetFileAttributesEx(original, GetFileExInfoStandard, &before));
        if (kind == 0)
            CHECK(DeleteFile(original));
        else
        {
            if (kind == 4)
            {
                SaveFile replacement;
                CHECK(replacement.open(original));
                CHECK(replacement.write("abcdef", 6));
                CHECK(replacement.commit());
            }
            HANDLE file = CreateFile(original, GENERIC_WRITE | FILE_WRITE_ATTRIBUTES,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0, OPEN_EXISTING, 0, 0);
            CHECK(file != INVALID_HANDLE_VALUE);
            if (kind == 1)
            {
                SetFilePointer(file, 0, 0, FILE_END);
                DWORD n;
                CHECK(WriteFile(file, "!", 1, &n, 0));
            }
            FILETIME created = before.ftCreationTime, written = before.ftLastWriteTime;
            if (kind == 2)
                ++written.dwLowDateTime;
            if (kind == 3)
                ++created.dwLowDateTime;
            CHECK(SetFileTime(file, &created, 0, &written));
            CloseHandle(file);
        }
        init(copy, "keep this");
        CHECK(!copy.load_state(reader));
        CHECK(reader.original_unavailable());
        expect_content(copy, "keep this");
        CHECK(!copy.canundo() && !copy.modified());
        if (kind != 0)
            CHECK(DeleteFile(original));
    }
}

void checkpoint_publish_failure_preserves_previous_commit_and_context()
{
    checkpoint_store store;
    sequence         seq, copy;
    init(seq, "base");
    CHECK(insert_bytes(seq, 4, " one"));
    seq.breakopt();
    state_writer writer(store.dir);
    writer.set_context({1, 2, 3});
    CHECK(seq.save_state(writer));
    state_reader         pinned(store.dir);
    std::vector<uint8_t> context;
    CHECK(pinned.read_context(context));
    CHECK(context == std::vector<uint8_t>({1, 2, 3}));
    CHECK(insert_bytes(seq, seq.size(), " two"));
    seq.breakopt();
    writer.set_context({4, 5});
    TCHAR manifest[MAX_PATH];
    wsprintf(manifest, TEXT("%s\\state.bin"), store.dir);
    HANDLE lock = CreateFile(manifest, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
    CHECK(lock != INVALID_HANDLE_VALUE);
    CHECK(!seq.save_state(writer)); // backing append succeeds; manifest replacement fails
    CloseHandle(lock);
    state_reader previous(store.dir);
    CHECK(copy.load_state(previous));
    expect_content(copy, "base one");
    CHECK(copy.undo());
    expect_content(copy, "base");
    copy.clear();
    CHECK(seq.save_state(writer));
    CHECK(pinned.read_context(context));
    CHECK(context == std::vector<uint8_t>({1, 2, 3}));
    CHECK(copy.load_state(pinned));
    expect_content(copy, "base one");
    copy.clear();
    state_reader latest(store.dir);
    CHECK(latest.read_context(context));
    CHECK(context == std::vector<uint8_t>({4, 5}));
    CHECK(copy.load_state(latest));
    expect_content(copy, "base one two");
    CHECK(copy.undo());
    expect_content(copy, "base one");
}

void checkpoint_roundtrips_mixed_history()
{
    checkpoint_store store;
    sequence         seq;
    init(seq, "abcdef\r\nxyz");
    unsigned     random = 8147;
    state_writer writer(store.dir);
    state_reader reader(store.dir);
    for (int i = 0; i < 120; ++i)
    {
        random    = random * 1664525U + 1013904223U;
        size_w at = random % (seq.size() + 1);
        seq.breakopt();
        switch (i % 6)
        {
        case 0:
            CHECK(insert_bytes(seq, at, "insert"));
            break;
        case 1:
            if (at < seq.size())
                CHECK(seq.erase(at, 1));
            break;
        case 2:
            CHECK(replace_bytes(seq, at, "xy", at < seq.size() ? 1 : 0));
            break;
        case 3:
            if (seq.canundo())
                CHECK(seq.undo());
            break;
        case 4:
            if (seq.canredo())
                CHECK(seq.redo());
            break;
        case 5:
            seq.mark_saved();
            break;
        }
        CHECK(seq.save_state(writer));
        sequence copy;
        CHECK(copy.load_state(reader));
        CHECK(render_content(copy) == render_content(seq));
        CHECK(copy.modified() == seq.modified());
        // Explore every state reachable from this checkpoint.
        std::vector<std::string> states;
        while (copy.canundo())
        {
            CHECK(copy.undo());
            states.push_back(render_content(copy));
        }
        while (copy.canredo())
            CHECK(copy.redo());
    }
}

void checkpoint_preserves_unicode_indexes_and_empty_sequences()
{
    for (TEXT_ENCODING encoding : {NCP_UTF8, NCP_UTF16, NCP_UTF16BE, NCP_UTF32, NCP_UTF32BE})
    {
        checkpoint_store store;
        sequence         seq, copy;
        std::string      text = encode_text(L"first\r\n\x00e9\x20ac\nlast", encoding);
        CHECK(seq.init((const seqchar *)text.data(), text.size()));
        sequence::line_scan_mode mode = encoding == NCP_UTF8      ? sequence::line_scan_utf8
                                        : encoding == NCP_UTF16   ? sequence::line_scan_utf16le
                                        : encoding == NCP_UTF16BE ? sequence::line_scan_utf16be
                                        : encoding == NCP_UTF32   ? sequence::line_scan_utf32le
                                                                  : sequence::line_scan_utf32be;
        seq.set_line_scan_mode(mode);
        state_writer writer(store.dir);
        state_reader reader(store.dir);
        CHECK(seq.save_state(writer));
        CHECK(copy.load_state(reader));
        while (copy.index_next(MEM_BLOCK_SIZE))
        {
        }
        CHECK(copy.linecount() == seq.linecount());
        CHECK(copy.indexed_chars() == seq.indexed_chars());
        for (size_w line = 0; line < seq.linecount(); ++line)
        {
            size_w a, b;
            CHECK(seq.offset_from_lineno(line, &a));
            CHECK(copy.offset_from_lineno(line, &b));
            CHECK(a == b);
        }
    }
    checkpoint_store store;
    sequence         seq, copy;
    state_writer     writer(store.dir);
    state_reader     reader(store.dir);
    CHECK(seq.save_state(writer));
    CHECK(copy.load_state(reader));
    CHECK(copy.size() == 0 && !copy.modified() && !copy.canundo() && !copy.canredo());
    CHECK(SequenceBufferProbe::buffer_count(copy) == 2);
    CHECK(SequenceBufferProbe::current_modify(copy) == 1);
    CHECK(SequenceBufferProbe::buffer_length(copy, 0) == 0);
    CHECK(insert_bytes(copy, 0, "after restore"));
    CHECK(copy.undo());
    CHECK(copy.size() == 0);
}

void checkpoint_rejects_truncation_and_invalid_graph_with_valid_checksum()
{
    checkpoint_store store;
    sequence         seq, copy;
    init(seq, "abc");
    init(copy, "keep this");
    CHECK(insert_bytes(seq, 1, "xyz"));
    state_writer writer(store.dir);
    state_reader reader(store.dir);
    CHECK(seq.save_state(writer));
    TCHAR name[MAX_PATH];
    wsprintf(name, _T("%s\\state.bin"), store.dir);
    FILE *fp = 0;
    _wfopen_s(&fp, name, L"rb");
    CHECK(fp != 0);
    if (!fp)
        return;
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    rewind(fp);
    std::vector<uint8_t> bytes((size_t)size);
    CHECK(fread(bytes.data(), 1, bytes.size(), fp) == bytes.size());
    fclose(fp);
    for (size_t length = 0; length < bytes.size(); length += 7)
    {
        _wfopen_s(&fp, name, L"wb");
        CHECK(fp != 0);
        if (!fp)
            return;
        CHECK(fwrite(bytes.data(), 1, length, fp) == length);
        fclose(fp);
        CHECK(!copy.load_state(reader));
        expect_content(copy, "keep this");
    }
    // Make the head point to itself and repair the CRC: graph validation,
    // rather than the checksum alone, must reject this checkpoint.
    uint64_t buffer_count = 0;
    for (int i = 0; i < 8; ++i)
        buffer_count |= (uint64_t)bytes[64 + i] << (i * 8);
    size_t next = 32 + 64 + (size_t)buffer_count * 64 + 40;
    for (int i = 0; i < 8; ++i)
        bytes[next + i] = i == 0 ? 1 : 0;
    uint32_t crc = ~0U;
    for (size_t i = 32; i < bytes.size(); ++i)
    {
        crc ^= bytes[i];
        for (int b = 0; b < 8; ++b)
            crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1)));
    }
    crc = ~crc;
    for (int i = 0; i < 4; ++i)
        bytes[24 + i] = (uint8_t)(crc >> (i * 8));
    _wfopen_s(&fp, name, L"wb");
    CHECK(fp != 0);
    if (!fp)
        return;
    CHECK(fwrite(bytes.data(), 1, bytes.size(), fp) == bytes.size());
    fclose(fp);
    CHECK(!copy.load_state(reader));
    expect_content(copy, "keep this");
}

void insert_at_beginning()
{
    sequence seq;
    init(seq, "abcdef");

    CHECK(insert_bytes(seq, 0, "XX"));
    expect_content(seq, "XXabcdef");
}

void insert_at_end()
{
    sequence seq;
    init(seq, "abcdef");

    CHECK(insert_bytes(seq, seq.size(), "XX"));
    expect_content(seq, "abcdefXX");
}

void insert_on_span_boundary()
{
    sequence seq;
    init(seq, "abcdef");

    CHECK(insert_bytes(seq, 3, "XX"));
    CHECK(insert_bytes(seq, 5, "YY"));
    expect_content(seq, "abcXXYYdef");
}

void insert_in_middle_of_span()
{
    sequence seq;
    init(seq, "abcdef");

    CHECK(insert_bytes(seq, 2, "XX"));
    expect_content(seq, "abXXcdef");
}

void delete_starts_on_span_boundary()
{
    sequence seq;
    init(seq, "abcdef");

    CHECK(insert_bytes(seq, 3, "XX"));
    CHECK(seq.erase(3, 3));
    expect_content(seq, "abcef");
}

void delete_ends_on_span_boundary()
{
    sequence seq;
    init(seq, "abcdef");

    CHECK(insert_bytes(seq, 3, "XX"));
    CHECK(seq.erase(1, 2));
    expect_content(seq, "aXXdef");
}

void delete_entire_file_with_multiple_spans()
{
    sequence seq;
    init(seq, "abcdef");

    CHECK(insert_bytes(seq, 0, "S"));
    CHECK(insert_bytes(seq, 4, "M"));
    CHECK(insert_bytes(seq, seq.size(), "E"));
    expect_content(seq, "SabcMdefE");

    CHECK(seq.erase(0, seq.size()));
    expect_content(seq, "");
}

void delete_starts_and_stops_within_single_span()
{
    sequence seq;
    init(seq, "abcdef");

    CHECK(seq.erase(2, 2));
    expect_content(seq, "abef");
}

void delete_across_multiple_spans()
{
    sequence seq;
    init(seq, "abcdefgh");

    CHECK(insert_bytes(seq, 3, "XX"));
    CHECK(seq.erase(2, 5));
    expect_content(seq, "abfgh");
}

void replace_at_beginning()
{
    sequence seq;
    init(seq, "abcdef");

    CHECK(replace_bytes(seq, 0, "XX", 2));
    expect_content(seq, "XXcdef");
}

void replace_at_end()
{
    sequence seq;
    init(seq, "abcdef");

    CHECK(replace_bytes(seq, seq.size(), "XX", 2));
    expect_content(seq, "abcdefXX");
}

void replace_on_span_boundary()
{
    sequence seq;
    init(seq, "abcdef");

    CHECK(insert_bytes(seq, 3, "XX"));
    CHECK(replace_bytes(seq, 3, "YY", 2));
    expect_content(seq, "abcYYdef");
}

void replace_in_middle_of_span()
{
    sequence seq;
    init(seq, "abcdef");

    CHECK(replace_bytes(seq, 2, "XYZ", 3));
    expect_content(seq, "abXYZf");
}

void replace_across_multiple_spans()
{
    sequence seq;
    init(seq, "abcdef");

    CHECK(insert_bytes(seq, 3, "XX"));
    CHECK(replace_bytes(seq, 2, "QQ", 5));
    expect_content(seq, "abQQf");
}

void consecutive_insert_undo_coalesces()
{
    sequence seq;
    init(seq, "abc");

    CHECK(insert_bytes(seq, seq.size(), "X"));
    CHECK(insert_bytes(seq, seq.size(), "Y"));
    expect_content(seq, "abcXY");

    CHECK(seq.undo());
    expect_content(seq, "abc");

    CHECK(seq.redo());
    expect_content(seq, "abcXY");
}

void breakopt_prevents_insert_coalescing()
{
    sequence seq;
    init(seq, "abc");

    CHECK(insert_bytes(seq, seq.size(), "X"));
    seq.breakopt();
    CHECK(insert_bytes(seq, seq.size(), "Y"));
    expect_content(seq, "abcXY");

    CHECK(seq.undo());
    expect_content(seq, "abcX");

    CHECK(seq.undo());
    expect_content(seq, "abc");

    CHECK(seq.redo());
    expect_content(seq, "abcX");

    CHECK(seq.redo());
    expect_content(seq, "abcXY");
}

void forward_delete_undo_coalesces()
{
    sequence seq;
    init(seq, "abcdef");

    CHECK(seq.erase(2, 1));
    CHECK(seq.erase(2, 1));
    expect_content(seq, "abef");

    CHECK(seq.undo());
    expect_content(seq, "abcdef");

    CHECK(seq.redo());
    expect_content(seq, "abef");
}

void backward_delete_undo_coalesces()
{
    sequence seq;
    init(seq, "abcdef");

    CHECK(seq.erase(3, 1));
    CHECK(seq.erase(2, 1));
    expect_content(seq, "abef");

    CHECK(seq.undo());
    expect_content(seq, "abcdef");

    CHECK(seq.redo());
    expect_content(seq, "abef");
}

void replace_overrun_clamps_to_end()
{
    sequence seq;
    init(seq, "abcdef");

    CHECK(replace_bytes(seq, 4, "XYZ", 99));
    expect_content(seq, "abcdXYZ");
}

void replace_shorter_than_erased()
{
    sequence seq;
    init(seq, "abcdef");

    CHECK(replace_bytes(seq, 1, "Z", 4));
    expect_content(seq, "aZf");
}

void replace_longer_than_erased()
{
    sequence seq;
    init(seq, "abcdef");

    CHECK(replace_bytes(seq, 2, "WXYZ", 1));
    expect_content(seq, "abWXYZdef");
}

void undo_redo_insert_middle_split()
{
    sequence seq;
    init(seq, "abcdef");

    CHECK(insert_bytes(seq, 3, "XX"));
    expect_content(seq, "abcXXdef");

    CHECK(seq.undo());
    expect_content(seq, "abcdef");

    CHECK(seq.redo());
    expect_content(seq, "abcXXdef");
}

void undo_redo_delete_within_split_span()
{
    sequence seq;
    init(seq, "abcdef");

    CHECK(seq.erase(2, 2));
    expect_content(seq, "abef");

    CHECK(seq.undo());
    expect_content(seq, "abcdef");

    CHECK(seq.redo());
    expect_content(seq, "abef");
}

void undo_redo_replace_across_spans()
{
    sequence seq;
    init(seq, "abcdef");

    CHECK(insert_bytes(seq, 3, "XX"));
    seq.breakopt();
    CHECK(replace_bytes(seq, 2, "QQ", 5));
    expect_content(seq, "abQQf");

    CHECK(seq.undo());
    expect_content(seq, "abcXXdef");

    CHECK(seq.redo());
    expect_content(seq, "abQQf");
}

void grouped_operations_undo_redo_together()
{
    sequence seq;
    init(seq, "abcdef");

    seq.group();
    CHECK(insert_bytes(seq, 1, "X"));
    CHECK(seq.erase(4, 1));
    CHECK(replace_bytes(seq, 0, "Q", 1));
    seq.ungroup();
    expect_content(seq, "QXbcef");

    CHECK(seq.undo());
    expect_content(seq, "abcdef");

    CHECK(seq.redo());
    expect_content(seq, "QXbcef");
}

void nested_group_survives_inner_replace()
{
    sequence seq;
    init(seq, "abcdef");

    seq.group();
    CHECK(insert_bytes(seq, 1, "X"));
    CHECK(replace_bytes(seq, 3, "Y", 1));
    CHECK(seq.erase(5, 1));
    seq.ungroup();
	expect_content(seq, "aXbYdf");

    CHECK(seq.undo());
    expect_content(seq, "abcdef");

    CHECK(seq.redo());
	expect_content(seq, "aXbYdf");
}

void undo_redo_reports_changed_range()
{
    sequence seq;
    size_w offset = 0;
    size_w erased = 0;
    size_w inserted = 0;

    init(seq, "abcdef");

    CHECK(insert_bytes(seq, 3, "XX"));
    CHECK(seq.undo());
    seq.event_change(&offset, &erased, &inserted);
    CHECK(offset == 3 && erased == 2 && inserted == 0);

    CHECK(seq.redo());
    seq.event_change(&offset, &erased, &inserted);
    CHECK(offset == 3 && erased == 0 && inserted == 2);

    // A replace is an erase and an insert grouped together; undo reports one range covering both.
    seq.breakopt();
    CHECK(replace_bytes(seq, 2, "Q", 4));
    expect_content(seq, "abQef");
    CHECK(seq.undo());
    expect_content(seq, "abcXXdef");
    seq.event_change(&offset, &erased, &inserted);
    CHECK(offset == 2 && erased == 1 && inserted == 4);
}

void redo_invalidated_by_new_edit()
{
    sequence seq;
    init(seq, "abc");

    CHECK(insert_bytes(seq, seq.size(), "X"));
    CHECK(seq.undo());
    CHECK(seq.canredo());

    CHECK(insert_bytes(seq, seq.size(), "Y"));
    CHECK(!seq.canredo());
    expect_content(seq, "abcY");
}

void invalid_operations_do_not_modify_content()
{
    sequence seq;
    init(seq, "abc");

    CHECK(!insert_bytes(seq, 4, "X"));
    CHECK(!insert_bytes(seq, 1, ""));
    CHECK(!seq.erase(1, 0));
    CHECK(!seq.erase(2, 5));
    CHECK(!replace_bytes(seq, 4, "X", 1));
    expect_content(seq, "abc");
    CHECK(!seq.canundo());
}

void render_past_end_returns_available_bytes()
{
    sequence seq;
    seqchar actual[5] = {};
    init(seq, "abc");

    CHECK(seq.render(2, actual, 5) == 1);
    CHECK(actual[0] == 'c');

    CHECK(seq.render(3, actual, 5) == 0);
}

void open_file_renders_file_backed_content()
{
    sequence seq;
    TCHAR path[MAX_PATH];
    const char *text = "first\r\nsecond\nthird";

    CHECK(write_temp_file(text, strlen(text), path));
    CHECK(seq.open(path, true));
    expect_content(seq, text);
    CHECK(seq.linecount() == 3);
    expect_offset_from_lineno(seq, 1, 7);
    expect_offset_from_lineno(seq, 2, 14);

	seq.clear();
    DeleteFile(path);
}

void open_file_renders_across_view_boundary()
{
    sequence seq;
    TCHAR path[MAX_PATH];
    const size_t file_length = MEM_BLOCK_SIZE + 128;
    const size_w render_offset = MEM_BLOCK_SIZE - 60;
    const size_w render_length = 140;
    char *data = new char[file_length];
    std::string actual;

    for(size_t i = 0; i < file_length; i++)
        data[i] = static_cast<char>('A' + (i % 26));

    CHECK(write_temp_file(data, file_length, path));
    CHECK(seq.open(path, true));
    CHECK(seq.size() == file_length);

    actual.resize(render_length);
    CHECK(seq.render(render_offset, reinterpret_cast<seqchar *>(&actual[0]), render_length) == render_length);
    CHECK(memcmp(actual.data(), data + render_offset, render_length) == 0);

	seq.clear();
    DeleteFile(path);
    delete[] data;
}

void open_file_larger_than_4gb()
{
    sequence seq;
    TCHAR path[MAX_PATH];
    const size_w size = LARGE_FILE_SIZE;
    const size_w tail_length = strlen(LARGE_FILE_TAIL);
    const size_w last_line = size - strlen("last line");
    char buf[64];
    size_w line_begin = 0;
    size_w line_next = 0;
    size_w lineno = 0;

    if(!write_sparse_file(LARGE_FILE_HEAD, LARGE_FILE_TAIL, LARGE_FILE_SIZE, path))
    {
        printf("open_file_larger_than_4gb: skipped, no sparse file support\n");
        return;
    }

    CHECK(size == LARGE_FILE_SIZE);
    CHECK(seq.open(path, true));
    CHECK(seq.size() == LARGE_FILE_SIZE);

    // reading beyond 4GB
    CHECK(seq.render(size - tail_length, reinterpret_cast<seqchar *>(buf), tail_length) == tail_length);
    CHECK(memcmp(buf, LARGE_FILE_TAIL, static_cast<size_t>(tail_length)) == 0);

    // lines beyond 4GB are found locally, and numbered by estimate
    CHECK(seq.line_bounds_from_offset(size - 3, &line_begin, &line_next));
    CHECK(line_begin == last_line);
    CHECK(line_next == size);
    CHECK(seq.lineno_from_offset(size, &lineno, &line_begin));
    CHECK(line_begin == last_line);
    CHECK(!seq.lineno_known_at(size));

    seq.clear();
    DeleteFile(path);
}

void sequence_modified_follows_undo_history()
{
    sequence seq;

    init(seq, "abc");
    CHECK(!seq.modified());

    CHECK(insert_bytes(seq, 0, "X"));
    CHECK(seq.modified());

    // saved as "Xabc"
    seq.mark_saved();
    CHECK(!seq.modified());

    CHECK(insert_bytes(seq, 4, "Y"));
    CHECK(seq.modified());

    // undo and redo across the saved point
    CHECK(seq.undo());
    CHECK(render_content(seq) == "Xabc");
    CHECK(!seq.modified());
    CHECK(seq.redo());
    CHECK(seq.modified());

    // a different edit after undoing replaces the redo history, but undoing it
    // still lands back on the saved text
    CHECK(seq.undo());
    CHECK(insert_bytes(seq, 0, "Z"));
    CHECK(seq.modified());
    CHECK(seq.undo());
    CHECK(render_content(seq) == "Xabc");
    CHECK(!seq.modified());

    // and undoing past it is modified again
    CHECK(seq.undo());
    CHECK(render_content(seq) == "abc");
    CHECK(seq.modified());
}

void sequence_mark_saved_stops_typing_joining_the_saved_record()
{
    sequence seq;

    init(seq, "");

    // typing one character after another goes into one undo record...
    CHECK(insert_bytes(seq, 0, "a"));
    seq.mark_saved();

    // ...but not across a save: "b" would otherwise join the saved "a"
    CHECK(insert_bytes(seq, 1, "b"));
    CHECK(seq.modified());

    CHECK(seq.undo());
    CHECK(render_content(seq) == "a");
    CHECK(!seq.modified());
}

void sequence_modified_treats_group_as_one_edit()
{
    sequence seq;

    init(seq, "abc");
    seq.mark_saved();

    seq.group();
    CHECK(insert_bytes(seq, 0, "X"));
    CHECK(insert_bytes(seq, 4, "Y"));
    seq.ungroup();
    CHECK(seq.modified());

    // the whole group undoes at once, back to the saved text
    CHECK(seq.undo());
    CHECK(render_content(seq) == "abc");
    CHECK(!seq.modified());
    CHECK(!seq.canundo());
}

void sequence_reads_a_short_file_into_memory()
{
    sequence       seq;
    TCHAR          path[MAX_PATH];
    std::string    text;
    size_w         lines = 0;
    const seqchar  typed[] = "typed\n";

    // several view blocks long, and all of it read into memory
    while(text.size() < MEM_BLOCK_SIZE * 3)
        text += std::to_string(lines++) + "\n";

    CHECK(write_temp_file(text.data(), text.size(), path));
    CHECK(seq.open(path, false, text.size() + 1));
    CHECK(others_can_write(path));
    CHECK(render_content(seq) == text);

    seq.index_lines(0, seq.size());
    CHECK(seq.linecount_known());
    CHECK(seq.linecount() == lines + 1);

    // and edited just as a file read from disk is
    CHECK(seq.insert(MEM_BLOCK_SIZE * 2, typed, 6));
    CHECK(render_content(seq) == text.substr(0, MEM_BLOCK_SIZE * 2) + "typed\n" + text.substr(MEM_BLOCK_SIZE * 2));
    CHECK(seq.undo());
    CHECK(render_content(seq) == text);

    seq.clear();
    DeleteFile(path);
}

// Line breaks in text: CR, LF and CRLF each count once
size_w breaks_in(const std::string &text)
{
    size_w breaks = 0;

    for(size_t i = 0; i < text.size(); i++)
    {
        if(text[i] == '\n' || (text[i] == '\r' && (i + 1 == text.size() || text[i + 1] != '\n')))
            breaks++;
    }

    return breaks;
}

// Append to a file through a handle another program might hold
bool append_to(HANDLE writer, const std::string &text)
{
    DWORD written = 0;

    SetFilePointer(writer, 0, 0, FILE_END);
    return WriteFile(writer, text.data(), (DWORD)text.size(), &written, 0) && written == text.size();
}

void sequence_grows_as_a_file_is_appended_to()
{
    sequence    seq;
    TCHAR       path[MAX_PATH];
    std::string text = utf8_text(12 * 5600) + "\r";
    std::string more = "\nmore caf\xc3\xa9\r\n";
    std::string pages = utf8_text(12 * 11000);
    HANDLE      writer;
    size_w      added = 0;

    // a little over a line page, ending with a CR
    CHECK(write_temp_file(text.data(), text.size(), path));
    CHECK(seq.open(path, true));
    CHECK(seq.read_only());

    seq.set_line_scan_mode(sequence::line_scan_utf8);
    seq.index_lines(0, seq.size());

    writer = CreateFile(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0, OPEN_EXISTING, 0, 0);
    CHECK(writer != INVALID_HANDLE_VALUE);

    // the CR it ended with, and the LF appended to it, are one line break
    CHECK(append_to(writer, more));
    text += more;

    CHECK(seq.grow(&added) && added == more.size());
    CHECK(render_content(seq) == text);
    CHECK(seq.linecount_known());
    CHECK(seq.linecount() == breaks_in(text) + 1);
    CHECK(seq.indexed_chars() == utf8_chars(text, text.size()));

    // more across two line pages
    CHECK(append_to(writer, pages));
    text += pages;

    CHECK(seq.grow(&added) && added == pages.size());
    CHECK(render_content(seq) == text);
    CHECK(seq.linecount_known());
    CHECK(seq.linecount() == breaks_in(text) + 1);
    CHECK(seq.indexed_chars() == utf8_chars(text, text.size()));

    // nothing more, nothing added
    CHECK(seq.grow(&added) && added == 0);

    // rewritten shorter, it's no longer the file it was
    SetFilePointer(writer, 100, 0, FILE_BEGIN);
    CHECK(SetEndOfFile(writer));
    CHECK(!seq.grow(&added));

    CloseHandle(writer);
    seq.clear();
    DeleteFile(path);
}

void sequence_opens_read_only_while_another_program_writes()
{
    sequence seq;
    TCHAR    path[MAX_PATH];
    HANDLE   writer;
    size_w   added = 0;

    CHECK(write_temp_file("log line\r\n", 10, path));

    // with no one writing it, it's locked while it's read, and can't grow
    CHECK(seq.open(path, false));
    CHECK(!seq.read_only());
    CHECK(!others_can_write(path));
    CHECK(!seq.grow(&added));
    seq.clear();

    // a program keeping it open to append to
    writer = CreateFile(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_DELETE, 0, OPEN_EXISTING, 0, 0);
    CHECK(writer != INVALID_HANDLE_VALUE);

    // opens it read-only, rather than not at all
    CHECK(seq.open(path, false));
    CHECK(seq.read_only());
    CHECK(render_content(seq) == "log line\r\n");

    // edited, it can't grow: the text isn't the file's any more
    CHECK(insert_bytes(seq, 0, "x"));
    CHECK(!seq.grow(&added));

    seq.clear();
    CloseHandle(writer);
    DeleteFile(path);
}

// What render passes on, joined, and how many blocks it came in
std::string render_blocks(const sequence &seq, size_w index, size_w length, size_t *blocks)
{
    std::string text;

    *blocks = 0;

    seq.render(index, [&](const seqchar *block, size_t blocklen)
    {
        text.append(reinterpret_cast<const char *>(block), blocklen);
        (*blocks)++;
        return true;
    }, length);

    return text;
}

void sequence_render_passes_a_block_per_span()
{
    sequence seq;
    size_t blocks;
    size_t calls = 0;

    // "ab", "XY" and "cdef" in three spans
    init(seq, "abcdef");
    CHECK(insert_bytes(seq, 2, "XY"));

    // from inside the first span, from a span's start, and from inside the last
    CHECK(render_blocks(seq, 1, 7, &blocks) == "bXYcdef");
    CHECK(blocks == 3);
    CHECK(render_blocks(seq, 2, 6, &blocks) == "XYcdef");
    CHECK(blocks == 2);
    CHECK(render_blocks(seq, 5, 3, &blocks) == "def");
    CHECK(blocks == 1);

    // only as far as the end, and nothing from there
    CHECK(render_blocks(seq, 6, 100, &blocks) == "ef");
    CHECK(render_blocks(seq, seq.size(), 1, &blocks) == "");
    CHECK(blocks == 0);

    // stopped at the second block, it has rendered only the first
    CHECK(seq.render(0, [&calls](const seqchar *, size_t) { return ++calls < 2; }, seq.size()) == 2);
    CHECK(calls == 2);
}

const test_case tests[] = {
    {"sequence_initial_and_modify_buffers_are_separate", sequence_initial_and_modify_buffers_are_separate},
    {"sequence_modify_buffers_roll_over_without_changing_initial_data",
     sequence_modify_buffers_roll_over_without_changing_initial_data},
    {"sequence_file_initial_buffer_is_zero", sequence_file_initial_buffer_is_zero},
    {"checkpoint_preserves_buffer_origins_and_legacy_ids", checkpoint_preserves_buffer_origins_and_legacy_ids},
    {"checkpoint_large_original_writes_only_modified_bytes", checkpoint_large_original_writes_only_modified_bytes},
    {"checkpoint_rejects_changed_or_missing_original_without_replacing_destination",
     checkpoint_rejects_changed_or_missing_original_without_replacing_destination},
    {"checkpoint_publish_failure_preserves_previous_commit_and_context",
     checkpoint_publish_failure_preserves_previous_commit_and_context},
    {"checkpoint_preserves_both_stacks_groups_and_saved_state",
     checkpoint_preserves_both_stacks_groups_and_saved_state},
    {"checkpoint_references_original_and_retains_it_before_save",
     checkpoint_references_original_and_retains_it_before_save},
    {"checkpoint_rejects_groups_and_bad_data_without_changing_destination",
     checkpoint_rejects_groups_and_bad_data_without_changing_destination},
    {"checkpoint_roundtrips_mixed_history", checkpoint_roundtrips_mixed_history},
    {"checkpoint_preserves_unicode_indexes_and_empty_sequences",
     checkpoint_preserves_unicode_indexes_and_empty_sequences},
    {"checkpoint_rejects_truncation_and_invalid_graph_with_valid_checksum",
     checkpoint_rejects_truncation_and_invalid_graph_with_valid_checksum},
    {"insert_at_beginning", insert_at_beginning},
    {"insert_at_end", insert_at_end},
    {"insert_on_span_boundary", insert_on_span_boundary},
    {"insert_in_middle_of_span", insert_in_middle_of_span},
    {"delete_starts_on_span_boundary", delete_starts_on_span_boundary},
    {"delete_ends_on_span_boundary", delete_ends_on_span_boundary},
    {"delete_entire_file_with_multiple_spans", delete_entire_file_with_multiple_spans},
    {"delete_starts_and_stops_within_single_span", delete_starts_and_stops_within_single_span},
    {"delete_across_multiple_spans", delete_across_multiple_spans},
    {"replace_at_beginning", replace_at_beginning},
    {"replace_at_end", replace_at_end},
    {"replace_on_span_boundary", replace_on_span_boundary},
    {"replace_in_middle_of_span", replace_in_middle_of_span},
    {"replace_across_multiple_spans", replace_across_multiple_spans},
    {"consecutive_insert_undo_coalesces", consecutive_insert_undo_coalesces},
    {"breakopt_prevents_insert_coalescing", breakopt_prevents_insert_coalescing},
    {"forward_delete_undo_coalesces", forward_delete_undo_coalesces},
    {"backward_delete_undo_coalesces", backward_delete_undo_coalesces},
    {"replace_overrun_clamps_to_end", replace_overrun_clamps_to_end},
    {"replace_shorter_than_erased", replace_shorter_than_erased},
    {"replace_longer_than_erased", replace_longer_than_erased},
    {"undo_redo_insert_middle_split", undo_redo_insert_middle_split},
    {"undo_redo_delete_within_split_span", undo_redo_delete_within_split_span},
    {"undo_redo_replace_across_spans", undo_redo_replace_across_spans},
    {"grouped_operations_undo_redo_together", grouped_operations_undo_redo_together},
    {"nested_group_survives_inner_replace", nested_group_survives_inner_replace},
    {"undo_redo_reports_changed_range", undo_redo_reports_changed_range},
    {"redo_invalidated_by_new_edit", redo_invalidated_by_new_edit},
    {"invalid_operations_do_not_modify_content", invalid_operations_do_not_modify_content},
    {"render_past_end_returns_available_bytes", render_past_end_returns_available_bytes},
    {"open_file_renders_file_backed_content", open_file_renders_file_backed_content},
    {"open_file_renders_across_view_boundary", open_file_renders_across_view_boundary},
    {"open_file_larger_than_4gb", open_file_larger_than_4gb},
    {"sequence_modified_follows_undo_history", sequence_modified_follows_undo_history},
    {"sequence_mark_saved_stops_typing_joining_the_saved_record",
     sequence_mark_saved_stops_typing_joining_the_saved_record},
    {"sequence_modified_treats_group_as_one_edit", sequence_modified_treats_group_as_one_edit},
    {"sequence_reads_a_short_file_into_memory", sequence_reads_a_short_file_into_memory},
    {"sequence_grows_as_a_file_is_appended_to", sequence_grows_as_a_file_is_appended_to},
    {"sequence_opens_read_only_while_another_program_writes", sequence_opens_read_only_while_another_program_writes},
    {"sequence_render_passes_a_block_per_span", sequence_render_passes_a_block_per_span},
};
}

const test_group sequence_tests = { "sequence", tests, sizeof(tests) / sizeof(tests[0]) };
