#include "db/compression.hpp"

#include <algorithm>
#include <bit>

namespace db {

namespace {

template <typename T>
PackedColumn pack(const std::vector<T> &values) {
    PackedColumn column;
    column.row_count = values.size();

    for (std::size_t block_begin = 0; block_begin < values.size(); block_begin += PACKED_BLOCK_ROWS) {
        std::size_t block_end = std::min(block_begin + PACKED_BLOCK_ROWS, values.size());

        auto [min_it, max_it] = std::minmax_element(values.begin() + block_begin, values.begin() + block_end);
        std::uint64_t base = *min_it;
        std::uint64_t max_difference = static_cast<std::uint64_t>(*max_it) - base;

        PackedBlock block;
        block.base = base;
        block.word_offset = column.words.size();
        block.bit_width = static_cast<std::uint32_t>(std::bit_width(max_difference));
        column.blocks.push_back(block);

        std::size_t block_bits = (block_end - block_begin) * block.bit_width;
        column.words.resize(column.words.size() + (block_bits + 63) / 64, 0);

        if (block.bit_width == 0) {
            continue;
        }

        for (std::size_t row = block_begin; row < block_end; ++row) {
            std::uint64_t difference = static_cast<std::uint64_t>(values[row]) - base;
            std::size_t bit_position = (row - block_begin) * block.bit_width;
            std::size_t word = block.word_offset + bit_position / 64;
            unsigned offset = bit_position % 64;

            column.words[word] |= difference << offset;
            if (offset + block.bit_width > 64) {
                column.words[word + 1] |= difference >> (64 - offset);
            }
        }
    }

    column.words.push_back(0);
    return column;
}

}

PackedColumn pack_column(const std::vector<std::uint64_t> &values) {
    return pack(values);
}

PackedColumn pack_column(const std::vector<std::uint32_t> &values) {
    return pack(values);
}

// function used for unit testing
std::vector<std::uint64_t> unpack_column(const PackedColumn &column) {
    std::vector<std::uint64_t> values(column.row_count);

    for (std::size_t block_index = 0; block_index < column.blocks.size(); ++block_index) {
        const PackedBlock &block = column.blocks[block_index];
        std::size_t block_begin = block_index * PACKED_BLOCK_ROWS;
        std::size_t block_end = std::min(block_begin + PACKED_BLOCK_ROWS, column.row_count);

        for (std::size_t row = block_begin; row < block_end; ++row) {
            std::uint64_t difference = 0;
            std::size_t first_bit = (row - block_begin) * block.bit_width;

            for (std::uint32_t bit = 0; bit < block.bit_width; ++bit) {
                std::size_t position = first_bit + bit;
                std::uint64_t word = column.words[block.word_offset + position / 64];
                difference |= ((word >> (position % 64)) & 1ULL) << bit;
            }
            values[row] = block.base + difference;
        }
    }
    return values;
}

}
