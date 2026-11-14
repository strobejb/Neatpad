// Changes against the last saved piece layout. Only source identities and
// offsets are retained: no text is copied, and editing/undo never updates this
// baseline. Surviving bytes identify unchanged ranges even after branching.

#include "sequence.h"
#include <algorithm>

void sequence::capture_saved_ranges()
{
    try
    {
        std::vector<saved_range> ranges;
        size_w                   position = 0;

        for (const span *piece = head->next; piece != tail; piece = piece->next)
        {
            if (piece->length)
            {
                ranges.push_back({piece->buffer, piece->offset, piece->length, position});
                position += piece->length;
            }
        }

        std::sort(ranges.begin(), ranges.end(),
                  [](const saved_range &a, const saved_range &b)
                  {
                      return a.buffer < b.buffer || (a.buffer == b.buffer && a.offset < b.offset);
                  });

        // Source neighbours merge only when they were neighbours in the saved
        // document too. Modify-buffer append order need not be document order.
        size_t count = 0;

        for (const saved_range &range : ranges)
        {
            if (count && ranges[count - 1].buffer == range.buffer &&
                ranges[count - 1].offset + ranges[count - 1].length == range.offset &&
                ranges[count - 1].saved_offset + ranges[count - 1].length == range.saved_offset)
            {
                ranges[count - 1].length += range.length;
            }
            else
            {
                ranges[count++] = range;
            }
        }

        ranges.resize(count);
        saved_ranges.swap(ranges);
        saved_length    = sequence_length;
        diff_available  = true;
        diff_generation = 0;
        diff_cache.clear();
    }
    catch (...)
    {
        // Indicators are optional. A completed file save must still establish
        // its normal saved undo group if allocating this metadata fails.
        saved_ranges.clear();
        saved_length    = 0;
        diff_available  = false;
        diff_generation = 0;
        diff_cache.clear();
    }
}

bool sequence::build_diff() const
{
    if (!diff_available)
    {
        return false;
    }

    if (diff_generation == line_generation)
    {
        return true;
    }

    try
    {
        std::vector<diff_range> changes;
        size_w                  position        = 0;
        size_w                  matched_current = 0;
        size_w                  matched_saved   = 0;

        for (const span *piece = head->next; piece != tail; piece = piece->next)
        {
            auto range = std::lower_bound(saved_ranges.begin(), saved_ranges.end(), piece,
                                          [](const saved_range &saved, const span *current)
                                          {
                                              return saved.buffer < current->buffer ||
                                                     (saved.buffer == current->buffer &&
                                                      saved.offset + saved.length <= current->offset);
                                          });

            size_w piece_end = piece->offset + piece->length;

            for (; range != saved_ranges.end() && range->buffer == piece->buffer && range->offset < piece_end; ++range)
            {
                size_w begin = (std::max)(piece->offset, range->offset);
                size_w end   = (std::min)(piece_end, range->offset + range->length);

                if (begin >= end)
                {
                    continue;
                }

                size_w current = position + begin - piece->offset;
                size_w saved   = range->saved_offset + begin - range->offset;

                // Editing imports fresh source bytes; undo restores their old
                // positions. Common source identities therefore keep order.
                if (saved < matched_saved)
                {
                    return false;
                }

                if (current > matched_current || saved > matched_saved)
                {
                    changes.push_back({matched_current, saved - matched_saved, current - matched_current});
                }

                matched_current = current + end - begin;
                matched_saved   = saved + end - begin;
            }

            position += piece->length;
        }

        if (matched_current < sequence_length || matched_saved < saved_length)
        {
            changes.push_back({matched_current, saved_length - matched_saved, sequence_length - matched_current});
        }

        diff_cache.swap(changes);
        diff_generation = line_generation;
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool sequence::diff(size_w index, size_w length, const std::function<bool(const diff_range &)> &fn) const
{
    if (!build_diff())
    {
        return false;
    }

    if (index > sequence_length || (length == 0 && index < sequence_length))
    {
        return true;
    }

    size_w end = index + (std::min)(length, sequence_length - index);
    auto   change =
        std::lower_bound(diff_cache.begin(), diff_cache.end(), index,
                         [](const diff_range &range, size_w begin)
                         {
                             return range.inserted ? range.offset + range.inserted <= begin : range.offset < begin;
                         });

    for (; change != diff_cache.end() && change->offset <= end; ++change)
    {
        bool overlaps = change->inserted ? change->offset < end && change->offset + change->inserted > index
                                         : change->offset < end || (end == sequence_length && change->offset == end);

        if (overlaps && !fn(*change))
        {
            break;
        }
    }

    return true;
}
