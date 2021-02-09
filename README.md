# CircularBuffer
A parameterized class that provides a high-performance circular buffer
implementation.

## Summary
Since there isn't one in the STL, I needed to hand-code a circular buffer
implementation to support high-performance networking data transfer.

This class was optimized using a "data mapping" approach that adds some
heuristics to mass-transfer data into and out of its allocated buffer.

I wrote some code (included in the main.cpp module) that implements an
insertion/extraction test.  Tests were performed using 50,000 iterations
of randomized amounts, ranging from 1 to 75,000 units, with each inserted
amount being CRC validated when extracted.  The CircularBuffer class is
specialized with **uint8_t** data units, with only the insertion (insert_units())
and extractions (extract_units()) calls being timed.  All other overhead
(e.g., calculating CRC values for validation) is excluded from timings.

In gathering data, I performed three consecutive runs after each optimization
type, and their total execution times were averaged.  The orignal "baseline"
timing was using the simple, brute-force insertion/extraction of reading a
single unit at a time.  Using that approach, the test-execution time for
three runs averaged to **~3475.87ms**.

I then optimized the insertion code, using "data mapping" heuristics, and
executing it with one or more ::memcpy() calls.  After optimizing insertions,
the test-execution time for three runs averaged to **~1784.356ms**.  This
represented a respectable 49% execution time decrease over the original approach.

Then the extraction was optimized using similar "data mapping" heuristics.
The test-execution time for three runs now averaged to **~40.332ms**, which is
a whopping 98% execution time decrease over original approach!

On Windows, compile with: cl /O2 /EHsc main.cpp

I hope you find this useful.

## Documentation
See the main.cpp module for example usage.
