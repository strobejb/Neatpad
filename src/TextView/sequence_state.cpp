// Sequence checkpoints. The editing engine knows only save_state/load_state;
// format, backing-store I/O and graph validation are confined to this file.
#define NOMINMAX
#include "sequence_state.h"
#include "SaveFile.h"
#include <algorithm>
#include <errno.h>
#include <limits.h>
#include <map>
#include <memory>
#include <stdio.h>
#include <string.h>
#include <utility>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <share.h>
#include <direct.h>
#include <io.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace
{
typedef uint64_t                       u64;
typedef uint32_t                       u32;
typedef std::basic_string<seqfilechar> path_string;
const size_t                           HEADER_SIZE  = 32;
const u64                              MAX_METADATA = 256ULL * 1024 * 1024;
const u64                              NO_BUFFER    = UINT64_MAX;
const uint8_t                          MAGIC[8]     = {'N', 'P', 'S', 'E', 'Q', 'S', 'T', 0};

path_string path(const path_string &dir, const char *name)
{
    path_string result = dir;

    if (!result.empty() && result.back() != '/' && result.back() != '\\')
        result += '/';
    while (*name)
        result += (seqfilechar)*name++;
    return result;
}

path_string buffer_path(const path_string &dir, size_t id)
{
    char name[48];

    snprintf(name, sizeof(name), "buffer-%llu.bin", (unsigned long long)id);
    return path(dir, name);
}

FILE *open_file(const path_string &name, const char *mode)
{
#ifdef _WIN32
    wchar_t wide[8];
    size_t  n = 0;

    while (mode[n] && n + 1 < sizeof(wide) / sizeof(wide[0]))
    {
        wide[n] = mode[n];
        ++n;
    }
    wide[n] = 0;

    FILE *fp = 0;

    _wfopen_s(&fp, name.c_str(), wide);
    return fp;
#else
    return fopen(name.c_str(), mode);
#endif
}

bool seek_file(FILE *fp, u64 offset)
{
    if (offset > INT64_MAX)
        return false;
#ifdef _WIN32
    return _fseeki64(fp, (int64_t)offset, SEEK_SET) == 0;
#else
    return fseeko(fp, (off_t)offset, SEEK_SET) == 0;
#endif
}

bool flush_file(FILE *fp)
{
#ifdef _WIN32
    return fflush(fp) == 0 && _commit(_fileno(fp)) == 0;
#else
    return fflush(fp) == 0 && fsync(fileno(fp)) == 0;
#endif
}

struct file_close
{
    void operator()(FILE *fp) const
    {
        if (fp)
            fclose(fp);
    }
};

typedef std::unique_ptr<FILE, file_close> file_ptr;

// CRC-32/ISO-HDLC (IEEE CRC-32): reflected polynomial 0xEDB88320,
// initial value 0xFFFFFFFF and final XOR 0xFFFFFFFF. Covers the manifest
// payload (metadata and context); backing-buffer bytes are not included.
u32 checksum(const uint8_t *data, size_t size)
{
    u32 crc = ~0U;

    for (size_t i = 0; i < size; ++i)
    {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b)
            crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1)));
    }
    return ~crc;
}

struct bytes
{
    std::vector<uint8_t> data;

    void put(u64 value, unsigned width)
    {
        for (unsigned i = 0; i < width; ++i)
        {
            data.push_back((uint8_t)value);
            value >>= 8;
        }
    }
};

// Sequential little-endian integer reader over the serialized header/payload.
// at is the next byte to read. A truncated field returns zero without advancing
// at and sets the sticky good flag to false, so decoding rejects the checkpoint.
struct cursor
{
    const std::vector<uint8_t> &data;
    size_t                      at   = 0;
    bool                        good = true;

    u64 get(unsigned width)
    {
        if (width > data.size() - at)
        {
            good = false;
            return 0;
        }

        u64 value = 0;

        for (unsigned i = 0; i < width; ++i)
            value |= (u64)data[at++] << (i * 8);

        return value;
    }
};

struct buffer_record
{
    u64         length = 0;
    u32         flags  = 0;
    path_string source;
    u64         created = 0, stamp = 0, device = 0, fileid = 0;
};

std::string encode_path(const path_string &name)
{
#ifdef _WIN32
    if (name.empty())
        return {};

    int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, name.data(), (int)name.size(), 0, 0, 0, 0);

    if (!n)
        throw 1;

    std::string bytes(n, 0);

    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, name.data(), (int)name.size(), &bytes[0], n, 0, 0);
    return bytes;
#else
    return name;
#endif
}

path_string decode_path(const std::string &bytes)
{
    if (bytes.find('\0') != std::string::npos)
        throw 1;
#ifdef _WIN32
    if (bytes.empty())
        return {};

    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), (int)bytes.size(), 0, 0);

    if (!n)
        throw 1;

    path_string name(n, 0);

    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), (int)bytes.size(), &name[0], n);
    return name;
#else
    return bytes;
#endif
}

bool source_info(FILE *fp, buffer_record &r, bool name)
{
#ifdef _WIN32
    HANDLE                     handle = (HANDLE)_get_osfhandle(_fileno(fp));
    BY_HANDLE_FILE_INFORMATION info;

    if (!GetFileInformationByHandle(handle, &info))
        return false;

    r.length  = ((u64)info.nFileSizeHigh << 32) | info.nFileSizeLow;
    r.created = ((u64)info.ftCreationTime.dwHighDateTime << 32) | info.ftCreationTime.dwLowDateTime;
    r.stamp   = ((u64)info.ftLastWriteTime.dwHighDateTime << 32) | info.ftLastWriteTime.dwLowDateTime;
    r.device  = info.dwVolumeSerialNumber;
    r.fileid  = ((u64)info.nFileIndexHigh << 32) | info.nFileIndexLow;

    if (name)
    {
        std::vector<wchar_t> text(32768);
        DWORD count = GetFinalPathNameByHandleW(handle, text.data(), (DWORD)text.size(), FILE_NAME_NORMALIZED);

        if (!count || count >= text.size())
            return false;
        r.source.assign(text.data(), count);
    }
#else
    struct stat info;

    if (fstat(fileno(fp), &info))
        return false;

    r.length  = info.st_size;
    r.created = 0;
    r.stamp   = (u64)info.st_mtim.tv_sec * 1000000000 + info.st_mtim.tv_nsec;
    r.device  = info.st_dev;
    r.fileid  = info.st_ino;

    if (name)
    {
        char link[64], text[32768];

        snprintf(link, sizeof(link), "/proc/self/fd/%d", fileno(fp));

        ssize_t n = readlink(link, text, sizeof(text));

        if (n <= 0 || n == sizeof(text))
            return false;
        r.source.assign(text, (size_t)n);
    }
#endif
    return true;
}

bool same_source(const buffer_record &a, const buffer_record &b)
{
    return a.length == b.length && a.created == b.created && a.stamp == b.stamp && a.device == b.device &&
           a.fileid == b.fileid;
}

bool source_valid(const buffer_record &r)
{
    file_ptr      fp(open_file(r.source, "rb"));
    buffer_record now;

    return fp && source_info(fp.get(), now, false) && same_source(r, now);
}

struct span_record
{
    u64 buffer, offset, length, prev, next;
};

struct event_record
{
    u64 first, last, seqlen, index, length, group;
    u32 action, flags;
};

struct image
{
    u64                        length = 0, group = 0, saved = 0;
    u32                        mode = 0, flags = 0;
    std::vector<buffer_record> buffers;
    std::vector<span_record>   spans;
    std::vector<event_record>  undo, redo;
    std::vector<uint8_t>       context;
};

// V1: 64-byte sequence record, then variable buffer records, 48-byte spans (IDs
// included), 56-byte history records, then u64 context length and opaque bytes.
// All integer fields are little endian. The header CRC covers this metadata;
// buffer files are immutable committed prefixes, verified by length on load.
bytes encode(const image &s)
{
    bytes b;

    b.put(s.mode, 4);
    b.put(s.flags, 4);
    b.put(s.length, 8);
    b.put(s.group, 8);
    b.put(s.saved, 8);
    b.put(s.buffers.size(), 8);
    b.put(s.spans.size(), 8);
    b.put(s.undo.size(), 8);
    b.put(s.redo.size(), 8);

    for (size_t i = 0; i < s.buffers.size(); ++i)
    {
        b.put(i, 8);
        b.put(s.buffers[i].length, 8);
        b.put(s.buffers[i].flags, 4);
        b.put(s.buffers[i].source.empty() ? 0 : 1, 4);
        b.put(s.buffers[i].created, 8);
        b.put(s.buffers[i].stamp, 8);
        b.put(s.buffers[i].device, 8);
        b.put(s.buffers[i].fileid, 8);

        std::string name = encode_path(s.buffers[i].source);

        b.put(name.size(), 8);
        b.data.insert(b.data.end(), name.begin(), name.end());
    }
    for (size_t i = 0; i < s.spans.size(); ++i)
    {
        const span_record &r = s.spans[i];

        b.put(i + 1, 8);
        b.put(r.buffer, 8);
        b.put(r.offset, 8);
        b.put(r.length, 8);
        b.put(r.prev, 8);
        b.put(r.next, 8);
    }
    for (const auto *stack : {&s.undo, &s.redo})
        for (const event_record &r : *stack)
        {
            b.put(r.first, 8);
            b.put(r.last, 8);
            b.put(r.seqlen, 8);
            b.put(r.index, 8);
            b.put(r.length, 8);
            b.put(r.group, 8);
            b.put(r.action, 4);
            b.put(r.flags, 4);
        }
    b.put(s.context.size(), 8);
    b.data.insert(b.data.end(), s.context.begin(), s.context.end());
    return b;
}

bool decode(const std::vector<uint8_t> &data, image &s)
{
    if (data.size() < 64)
        return false;

    cursor c = {data};

    s.mode   = (u32)c.get(4);
    s.flags  = (u32)c.get(4);
    s.length = c.get(8);
    s.group  = c.get(8);
    s.saved  = c.get(8);

    u64 buffers = c.get(8), spans = c.get(8), undo = c.get(8), redo = c.get(8);

    if (s.mode > sequence::line_scan_utf32be || s.flags > 1 || s.group >= SIZE_MAX || s.saved > s.group ||
        buffers > INT_MAX || spans < 2 || spans > MAX_METADATA / 48 || undo > MAX_METADATA / 56 ||
        redo > MAX_METADATA / 56 || buffers > MAX_METADATA / 64 ||
        64 + buffers * 64 + spans * 48 + (undo + redo) * 56 + 8 > data.size())
        return false;

    s.buffers.resize((size_t)buffers);
    s.spans.resize((size_t)spans);
    s.undo.resize((size_t)undo);
    s.redo.resize((size_t)redo);

    for (size_t i = 0; i < s.buffers.size(); ++i)
    {
        if (c.get(8) != i)
            return false;
        s.buffers[i].length = c.get(8);
        s.buffers[i].flags  = (u32)c.get(4);

        u64 kind = c.get(4);

        s.buffers[i].created = c.get(8);
        s.buffers[i].stamp   = c.get(8);
        s.buffers[i].device  = c.get(8);
        s.buffers[i].fileid  = c.get(8);

        u64 count = c.get(8);

        if (!c.good || count > 32768 || count > data.size() - c.at || kind > 1 || s.buffers[i].flags > 1 ||
            s.buffers[i].length > INT64_MAX || (kind == 0) != (count == 0))
            return false;

        s.buffers[i].source  = decode_path(std::string(data.begin() + c.at, data.begin() + c.at + (size_t)count));
        c.at                += (size_t)count;

        if (!kind && (s.buffers[i].created || s.buffers[i].stamp || s.buffers[i].device || s.buffers[i].fileid))
            return false;
    }
    for (size_t i = 0; i < s.spans.size(); ++i)
    {
        if (c.get(8) != i + 1)
            return false;

        span_record &r = s.spans[i];

        r.buffer = c.get(8);
        r.offset = c.get(8);
        r.length = c.get(8);
        r.prev   = c.get(8);
        r.next   = c.get(8);

        if (r.prev > spans || r.next > spans)
            return false;

        if (i < 2)
        {
            if (r.length || r.offset || r.buffer != NO_BUFFER)
                return false;
        }
        else if (r.buffer >= buffers || r.offset > s.buffers[(size_t)r.buffer].length ||
                 r.length > s.buffers[(size_t)r.buffer].length - r.offset || !r.prev || !r.next)
        {
            return false;
        }
    }
    for (auto *stack : {&s.undo, &s.redo})
        for (event_record &r : *stack)
        {
            r.first  = c.get(8);
            r.last   = c.get(8);
            r.seqlen = c.get(8);
            r.index  = c.get(8);
            r.length = c.get(8);
            r.group  = c.get(8);
            r.action = (u32)c.get(4);
            r.flags  = (u32)c.get(4);

            if (!r.first || !r.last || r.first > spans || r.last > spans || !r.group || r.group > s.group ||
                r.action < sequence::action_insert || r.action > sequence::action_replace || r.flags > 3)
                return false;
        }

    u64 context = c.get(8);

    if (!c.good || context > 65536 || context != data.size() - c.at)
        return false;

    s.context.assign(data.begin() + c.at, data.end());
    return true;
}

bool read_image(const path_string &dir, image &s, bool &missing)
{
    file_ptr fp(open_file(path(dir, "state.bin"), "rb"));

    missing = !fp && errno == ENOENT;
    if (!fp)
        return false;

    std::vector<uint8_t> header(HEADER_SIZE);

    if (fread(header.data(), 1, header.size(), fp.get()) != header.size() || memcmp(header.data(), MAGIC, 8))
        return false;

    cursor c = {header};

    c.at = 8;
    if (c.get(4) != 1 || c.get(4) != HEADER_SIZE)
        return false;

    u64 length = c.get(8);
    u32 crc    = (u32)c.get(4);

    if (c.get(4) || length > MAX_METADATA)
        return false;

    std::vector<uint8_t> data((size_t)length);

    if (fread(data.data(), 1, data.size(), fp.get()) != data.size() || fgetc(fp.get()) != EOF || ferror(fp.get()) ||
        checksum(data.data(), data.size()) != crc)
        return false;

    return decode(data, s);
}

bool publish(const path_string &directory, const image &s)
{
    bytes payload = encode(s), header;

    if (payload.data.size() > MAX_METADATA)
        return false;

    header.data.assign(MAGIC, MAGIC + 8);
    header.put(1, 4);
    header.put(HEADER_SIZE, 4);
    header.put(payload.data.size(), 8);
    header.put(checksum(payload.data.data(), payload.data.size()), 4);
    header.put(0, 4);

    SaveFile manifest;
    return manifest.open(path(directory, "state.bin").c_str()) &&
           manifest.write(header.data.data(), header.data.size()) &&
           manifest.write(payload.data.data(), payload.data.size()) && manifest.commit();
}

// Check ownership once, then simulate each history direction using IDs. No
// unvalidated pointer graph is ever handed to sequence's undo code/destructor.
bool validate(const image &s)
{
    const size_t n = s.spans.size();

    if (n < 2 || s.spans[0].prev || s.spans[1].next)
        return false;

    std::vector<uint8_t> owners(n + 1, 0);

    owners[1] = owners[2] = 1;

    auto chain            = [&](u64 first, u64 last, bool live, u64 &length) -> bool
    {
        u64 prev = live ? 1 : s.spans[(size_t)first - 1].prev;

        for (u64 id = first, steps = 0;; ++steps)
        {
            if (live && id == 2)
                return prev == s.spans[1].prev;

            if (id <= 2 || id > n || steps >= n || owners[(size_t)id])
                return false;

            const span_record &r = s.spans[(size_t)id - 1];

            if (r.prev != prev || r.length > UINT64_MAX - length)
                return false;

            owners[(size_t)id]  = 1;
            length             += r.length;

            if (!live && id == last)
                return true;

            prev = id;
            id   = r.next;
        }
    };

    u64 length = 0;

    if (!chain(s.spans[0].next, 2, true, length) || length != s.length)
        return false;

    for (const auto *stack : {&s.undo, &s.redo})
        for (const event_record &r : *stack)
            if (!(r.flags & 1))
            {
                u64 ignored = 0;

                if (!chain(r.first, r.last, false, ignored))
                    return false;
            }

    for (size_t i = 1; i <= n; ++i)
        if (!owners[i])
            return false;

    for (const auto *stack : {&s.undo, &s.redo})
    {
        std::vector<span_record> nodes = s.spans;
        std::vector<bool>        live(n + 1, false);

        for (u64 id = 1; id; id = nodes[(size_t)id - 1].next)
            live[(size_t)id] = true;

        u64 current = s.length;

        for (auto it = stack->rbegin(); it != stack->rend(); ++it)
        {
            const event_record &r = *it;
            u64                 a = (r.flags & 1) ? r.first : nodes[(size_t)r.first - 1].prev;
            u64                 b = (r.flags & 1) ? r.last : nodes[(size_t)r.last - 1].next;

            if (!a || !b || a == b || !live[(size_t)a] || !live[(size_t)b])
                return false;

            u64 removed = 0, added = 0, prev = a;

            for (u64 id = nodes[(size_t)a - 1].next, steps = 0; id != b; ++steps)
            {
                if (id <= 2 || id > n || steps >= n || !live[(size_t)id])
                    return false;

                span_record &node = nodes[(size_t)id - 1];

                if (node.prev != prev || node.length > UINT64_MAX - removed)
                    return false;

                removed          += node.length;
                live[(size_t)id]  = false;
                prev              = id;
                id                = node.next;
            }

            if (nodes[(size_t)b - 1].prev != prev || removed > current)
                return false;

            if (!(r.flags & 1))
            {
                prev = a;
                for (u64 id = r.first, steps = 0;; ++steps)
                {
                    if (id <= 2 || id > n || steps >= n || live[(size_t)id])
                        return false;

                    span_record &node = nodes[(size_t)id - 1];

                    if (node.prev != prev || node.length > UINT64_MAX - added)
                        return false;
                    added            += node.length;
                    live[(size_t)id]  = true;

                    if (id == r.last)
                        break;

                    prev = id;
                    id   = node.next;
                }
                if (nodes[(size_t)r.last - 1].next != b)
                    return false;

                nodes[(size_t)a - 1].next = r.first;
                nodes[(size_t)b - 1].prev = r.last;
            }
            else
            {
                nodes[(size_t)a - 1].next = b;
                nodes[(size_t)b - 1].prev = a;
            }

            if (added > UINT64_MAX - (current - removed) || current - removed + added != r.seqlen ||
                r.index > (std::max)(current, r.seqlen))
                return false;

            current = r.seqlen;
        }
    }
    return true;
}
} // namespace

bool sequence::save_state(state_writer &out) const
{
    if (group_refcount || out.directory.empty() || out.context.size() > 65536)
        return false;
    try
    {
        image old;
        bool  missing;

        if (!read_image(out.directory, old, missing) && !missing)
            return false;

        image s;

        s.context = out.context;
        s.length  = sequence_length;
        s.mode    = line_mode;
        s.group   = group_id;
        s.saved   = saved_group;
        s.flags   = can_quicksave ? 1 : 0;

        std::map<const span *, u64> ids;
        std::vector<const span *>   nodes;

        auto add = [&](const span *p) -> u64
        {
            if (!p)
                return 0;

            auto found = ids.find(p);

            if (found != ids.end())
                return found->second;

            u64 id = nodes.size() + 1;

            ids[p] = id;
            nodes.push_back(p);
            return id;
        };
        add(head);
        add(tail);
        for (const eventstack *stack : {&undostack, &redostack})
            for (const span_range *r : *stack)
            {
                add(r->first);
                add(r->last);
            }
        for (size_t i = 0; i < nodes.size(); ++i)
        {
            add(nodes[i]->prev);
            add(nodes[i]->next);
        }
        for (size_t i = 0; i < buffer_list.size(); ++i)
        {
            buffer_control *b = buffer_list[i];
            buffer_record   r;

            r.length = b->length;
            r.flags  = b->readonly ? 1U : 0U;
            if (i < old.buffers.size() && !old.buffers[i].source.empty())
            {
                r = old.buffers[i];
                if (r.length != b->length || !source_valid(r))
                    return false;
            }
            else if (i >= old.buffers.size() && (b->fp || (i == 1 && !out.original_file.empty())))
            {
                file_ptr original(b->fp ? 0 : open_file(out.original_file, "rb"));
                FILE    *fp = b->fp ? (FILE *)b->fp : original.get();

                if (!fp || !source_info(fp, r, true) || r.length != b->length || !source_valid(r))
                    return false;
                // Small originals may have been read into memory and released.
                // Compare those bytes once; large file-backed bases need only stat.
                if (!b->fp)
                {
                    std::vector<uint8_t> bytes((size_t)(std::min)(r.length, MEM_BLOCK_SIZE));

                    for (u64 at = 0; at < r.length;)
                    {
                        size_t n = (size_t)(std::min)(r.length - at, (u64)bytes.size());

                        if (fread(bytes.data(), 1, n, fp) != n || memcmp(bytes.data(), b->getptr(at, n), n))
                            return false;
                        at += n;
                    }
                }
            }
            s.buffers.push_back(r);
        }
        for (const span *p : nodes)
            s.spans.push_back({p == head || p == tail ? NO_BUFFER : (u64)p->buffer, p->offset, p->length, add(p->prev),
                               add(p->next)});
        for (int i = 0; i < 2; ++i)
        {
            const eventstack &from = i ? redostack : undostack;

            auto &to = i ? s.redo : s.undo;

            for (const span_range *r : from)
                to.push_back({add(r->first), add(r->last), r->sequence_length, r->index, r->length, r->group_id,
                              (u32)r->act, (r->boundary ? 1U : 0U) | (r->quicksave ? 2U : 0U)});
        }
        if (!validate(s))
            return false;

        bytes payload = encode(s);

        if (payload.data.size() > MAX_METADATA)
            return false;
#ifdef _WIN32
        if (_wmkdir(out.directory.c_str()) != 0 && errno != EEXIST)
            return false;
#else
        if (mkdir(out.directory.c_str(), 0700) != 0 && errno != EEXIST)
            return false;
#endif
        if (old.buffers.size() > buffer_list.size())
            return false;
        for (size_t i = 0; i < buffer_list.size(); ++i)
        {
            buffer_control *b = buffer_list[i];

            if (!s.buffers[i].source.empty())
                continue;

            u64 from = i < old.buffers.size() ? old.buffers[i].length : 0;

            if (from > b->length)
                return false;

            path_string name = buffer_path(out.directory, i);

            // Sealed buffers loaded from this store stay open for reading: leave
            // them alone. Future edits go to a fresh in-memory modify buffer.
            if (i < old.buffers.size() && from == b->length)
            {
                file_ptr fp(open_file(name, "rb"));

                if (!fp || (from && (!seek_file(fp.get(), from - 1) || fgetc(fp.get()) == EOF)))
                    return false;
                continue;
            }

            file_ptr fp(open_file(name, from ? "r+b" : "wb"));

            if (!fp || !seek_file(fp.get(), from))
                return false;
            while (from < b->length)
            {
                size_t count = (size_t)(std::min)(b->length - from, MEM_BLOCK_SIZE);

                const seqchar *raw = b->getptr(from, count);
                if (!raw || fwrite(raw, 1, count, fp.get()) != count)
                    return false;
                from += count;
            }
            if (!flush_file(fp.get()))
                return false;
        }
        return publish(out.directory, s);
    }
    catch (...)
    {
        return false;
    }
}

bool sequence::load_state(state_reader &in)
{
    in.source_unavailable = false;
    if (group_refcount || in.directory.empty())
        return false;
    try
    {
        image s;
        bool  missing;
        bool  read = in.checkpoint.empty() ? read_image(in.directory, s, missing) : decode(in.checkpoint, s);

        in.checkpoint.clear();
        if (!read || !validate(s))
            return false;
        sequence restored;
        restored.line_mode = (line_scan_mode)s.mode;
        restored.buffer_list.reserve(s.buffers.size() + 1);
        for (size_t i = 0; i < s.buffers.size(); ++i)
        {
            std::unique_ptr<buffer_control> b(new buffer_control);

            b->set_line_scan_mode(restored.line_mode);

            const buffer_record &record = s.buffers[i];
            path_string          name   = record.source.empty() ? buffer_path(in.directory, i) : record.source;

            if (!b->init_file(name.c_str(), false, 0) || b->readonly || b->length < record.length)
            {
                in.source_unavailable = !record.source.empty();
                return false;
            }
            if (!record.source.empty())
            {
                buffer_record now;

                if (!source_info((FILE *)b->fp, now, false) || !same_source(record, now))
                {
                    in.source_unavailable = true;
                    return false;
                }
            }
            b->length = b->maxsize = s.buffers[i].length;
            b->readonly            = (s.buffers[i].flags & 1) != 0;
            b->id                  = (int)i;
            restored.buffer_list.push_back(b.get());
            b.release();
        }

        std::unique_ptr<buffer_control> fresh(new buffer_control);

        fresh->set_line_scan_mode(restored.line_mode);
        if (!fresh->init(0x10000))
            return false;

        fresh->id                 = (int)restored.buffer_list.size();

        restored.modifybuffer_id  = fresh->id;
        restored.modifybuffer_pos = 0;
        restored.buffer_list.push_back(fresh.release());

        std::vector<std::unique_ptr<span>>       nodes;
        std::vector<std::unique_ptr<span_range>> events;

        nodes.reserve(s.spans.size());
        events.reserve(s.undo.size() + s.redo.size());

        for (const span_record &r : s.spans)
            nodes.emplace_back(new span(r.offset, r.length, r.buffer == NO_BUFFER ? 0 : (int)r.buffer));

        for (size_t i = 0; i < nodes.size(); ++i)
        {
            span              *p = nodes[i].get();
            const span_record &r = s.spans[i];

            p->prev = r.prev ? nodes[(size_t)r.prev - 1].get() : 0;
            p->next = r.next ? nodes[(size_t)r.next - 1].get() : 0;
            if (i >= 2)
                restored.update_span_line_data(p);
        }
        restored.undostack.reserve(s.undo.size());
        restored.redostack.reserve(s.redo.size());

        for (const auto *stack : {&s.undo, &s.redo})
            for (const event_record &r : *stack)
            {
                std::unique_ptr<span_range> event(
                    new span_range(r.seqlen, r.index, r.length, (action)r.action, (r.flags & 2) != 0, (size_t)r.group));
                event->first    = nodes[(size_t)r.first - 1].get();
                event->last     = nodes[(size_t)r.last - 1].get();
                event->boundary = (r.flags & 1) != 0;
                events.push_back(std::move(event));
            }
        // All allocations and validation have succeeded. Transfer ownership to
        // a valid temporary sequence, then exchange only its owning state.
        delete restored.head;
        delete restored.tail;

        restored.head = nodes[0].get();
        restored.tail = nodes[1].get();

        for (size_t i = 0; i < events.size(); ++i)
            (i < s.undo.size() ? restored.undostack : restored.redostack).push_back(events[i].release());

        for (auto &node : nodes)
            node.release();

        restored.sequence_length = s.length;
        restored.group_id        = (size_t)s.group;
        restored.saved_group     = (size_t)s.saved;
        restored.can_quicksave   = (s.flags & 1) != 0;

        int fresh_modify = restored.modifybuffer_id;

        std::swap(head, restored.head);
        std::swap(tail, restored.tail);
        std::swap(sequence_length, restored.sequence_length);

        buffer_list.swap(restored.buffer_list);
        undostack.swap(restored.undostack);
        redostack.swap(restored.redostack);

        std::swap(group_id, restored.group_id);
        std::swap(saved_group, restored.saved_group);
        std::swap(can_quicksave, restored.can_quicksave);
        std::swap(line_mode, restored.line_mode);

        modifybuffer_id  = fresh_modify;
        modifybuffer_pos = 0;
        group_refcount   = 0;
        frag1 = frag2 = 0;

        record_action(action_invalid, 0);

        change_offset = change_erased = change_inserted = 0;
        line_generation                                 = 1;

        prefix_generation                               = 0;
        prefix_end = prefix_breaks = prefix_chars = 0;
        prefix_complete                           = false;
        prefix_kinds                              = break_kinds();

        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool state_reader::read_context(std::vector<uint8_t> &bytes)
{
    try
    {
        image s;
        bool  missing;

        if (checkpoint.empty() ? !read_image(directory, s, missing) : !decode(checkpoint, s))
            return false;

        checkpoint = encode(s).data;
        bytes      = s.context;

        return true;
    }
    catch (...)
    {
        return false;
    }
}

// Retain a base only when the application is about to replace its original.
// Hard links cost no content I/O. Cross-volume/filesystem fallback happens here,
// never during the first checkpoint.
bool state_writer::preserve_sources()
{
    try
    {
        image s;
        bool  missing;

        if (!read_image(directory, s, missing))
            return false;

        for (size_t i = 0; i < s.buffers.size(); ++i)
        {
            buffer_record &r = s.buffers[i];

            if (r.source.empty())
                continue;

            if (!source_valid(r))
                return false;

            char leaf[48];

            snprintf(leaf, sizeof(leaf), "base-%llu.bin", (unsigned long long)i);

            path_string   target = path(directory, leaf);
            file_ptr      existing(open_file(target, "rb"));
            buffer_record now;
            bool          retained = existing && source_info(existing.get(), now, true) && same_source(r, now);
            bool          copied   = false;

            existing.reset();

            if (!retained)
            {
#ifdef _WIN32
                retained = CreateHardLinkW(target.c_str(), r.source.c_str(), 0) != FALSE;
#else
                retained = link(r.source.c_str(), target.c_str()) == 0;
#endif
                if (!retained)
                {
                    file_ptr source(open_file(r.source, "rb"));

                    SaveFile copy;
                    if (!source || !copy.open(target.c_str()))
                        return false;

                    std::vector<uint8_t> bytes((size_t)MEM_BLOCK_SIZE);

                    for (u64 at = 0; at < r.length;)
                    {
                        size_t n = (size_t)(std::min)(r.length - at, (u64)bytes.size());

                        if (fread(bytes.data(), 1, n, source.get()) != n || !copy.write(bytes.data(), n))
                            return false;
                        at += n;
                    }

                    buffer_record after;

                    if (!source_info(source.get(), after, false) || !same_source(r, after) || !copy.commit())
                        return false;
                    copied = true;
                }
            }

            file_ptr file(open_file(target, "rb"));

            if (!file || !source_info(file.get(), now, true) || now.length != r.length ||
                (!copied && !same_source(r, now)))
                return false;

            now.flags = r.flags;
            r         = now;
        }

        return publish(directory, s);
    }
    catch (...)
    {
        return false;
    }
}
