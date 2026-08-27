# Compact-model asset container

This example converts a small OBJ at build time into one versioned compact-
model asset. Its vertex partition is stored as a checksummed LZ4 Frame while
the polygon/state partition remains raw and independently addressable. `bin2c`
embeds the resulting asset only to keep the example self-contained.

At runtime the application parses the bounded header, queries exact caller-
owned decode workspace, and opts into one LZ4 service fiber on a shared service
executor. Its deliberately tiny test budget publishes at most 16 bytes between
cooperative yields, making the example exercise multiple decode steps. The
normal asset loader then checks both decoded CRCs and passes the streams through
compact-model admission without decoding the vertex partition twice.

The service is an explicit example choice. The same asset can instead use the
synchronous decoder or its manually stepped state, and applications that use
raw assets create no worker thread or fiber and do not need to link `liblz4`.

The generated frame has no dictionary. Shared dictionaries are optional and
become useful mainly when many small, related model partitions are compressed
together under one content pipeline.
