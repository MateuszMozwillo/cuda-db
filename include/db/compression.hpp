#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "db/host_device.hpp"

namespace db {

constexpr std::size_t PACKED_BLOCK_ROWS = 1024;

struct PackedBlock {
    std::uint64_t base;
    std::uint64_t word_offset;
    std::uint32_t bit_width;
};

struct PackedColumn {
    std::vector<PackedBlock> blocks;
    std::vector<std::uint64_t> words;
    std::size_t row_count = 0;

    std::size_t size_bytes() const {
        return blocks.size() * sizeof(PackedBlock) + words.size() * sizeof(std::uint64_t);
    }
};

struct PackedColumnView {
    const PackedBlock *blocks;
    const std::uint64_t *words;
};

PackedColumn pack_column(const std::vector<std::uint64_t> &values);
PackedColumn pack_column(const std::vector<std::uint32_t> &values);

std::vector<std::uint64_t> unpack_column(const PackedColumn &column);

DB_HOST_DEVICE inline std::uint64_t unpack_value(PackedColumnView column, std::size_t row) {
    const PackedBlock &block = column.blocks[row / PACKED_BLOCK_ROWS];

    if (block.bit_width == 0) {
        return block.base;
    }

    const std::uint64_t bit_position = static_cast<std::uint64_t>(row % PACKED_BLOCK_ROWS) * block.bit_width;
    const std::uint64_t word = block.word_offset + bit_position / 64;
    const unsigned offset = bit_position % 64;

    std::uint64_t difference = column.words[word] >> offset;

    if (offset != 0) {
        difference |= column.words[word + 1] << (64 - offset);
    }

    if (block.bit_width < 64) {
        difference &= (1ULL << block.bit_width) - 1;
    }

    return block.base + difference;
}

}
