#pragma once

#ifdef __CUDACC__
#define DB_HOST_DEVICE __host__ __device__
#else
#define DB_HOST_DEVICE
#endif
