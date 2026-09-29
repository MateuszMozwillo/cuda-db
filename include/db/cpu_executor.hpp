#pragma once

#include "db/query.hpp"
#include "db/mem_table.hpp"

namespace db {

// thread_count == 0 picks the number of threads automatically, small mem tables are then scanned
AggState handle_cpu_query(const MemTable &mem_table, const ResolvedQuery &query, unsigned thread_count = 0);

}
