# accessor
------
​	Welcome. This is *accessor*, a library to read/write files such as archives, saved games etc., developed with reverse engineering tools in mind, not performance.

​	Accessors take care of:

- reading/writing integers of various signedness, endianness and size, from 1 to `sizeof(uintmax_t)`.
- reading/writing 32 and 64 bits floats of various endianness.
- reading/writing arrays of numbers (of various endianness and size)
- endianness is settable for every read/write operations
- complete error detection such as reading beyond EOF, invalid data etc.
- coverage recording (i.e. recording which part of a file where read and why)
- saving/restoring the cursor.
- etc.

Your feedback is welcome.

xvi;

​	PS: *accessor* is primarely developed on **macOS**, but should easily be portable to any standard-compliant platform.
