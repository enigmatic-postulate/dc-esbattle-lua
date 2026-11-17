// STUB (see kos.h): newlib mallinfo
#pragma once
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
struct mallinfo { int arena, ordblks, smblks, hblks, hblkhd, usmblks, fsmblks, uordblks, fordblks, keepcost; };
struct mallinfo mallinfo(void);
#pragma GCC diagnostic pop
