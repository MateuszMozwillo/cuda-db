#pragma once

#include "db/query.hpp"
#include "db/mem_table.hpp"

namespace db {

AggState handle_cpu_query(const MemTable &mem_table, const ResolvedQuery &query);

}
