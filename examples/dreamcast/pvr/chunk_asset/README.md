# Compact-model asset container

This example converts a small OBJ at build time into one versioned compact-
model asset. Its vertex partition is stored as a checksummed LZ4 Frame while
the polygon/state partition remains raw and independently addressable. `bin2c`
embeds the resulting asset only to keep the example self-contained.

At runtime the application parses the bounded header, queries exact caller-
owned decode workspace, invokes the optional `liblz4` decoder, checks both
decoded CRCs, and passes the resulting streams through normal compact-model
admission. No worker thread or fiber is created. The same synchronous load may
be called by a developer-owned thread or fiber; applications that use raw
assets do not need to link `liblz4`.

The generated frame has no dictionary. Shared dictionaries are optional and
become useful mainly when many small, related model partitions are compressed
together under one content pipeline.
