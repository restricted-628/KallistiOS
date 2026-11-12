/* KOS VFS binding, separately linked from the transport-independent reader. */
#ifndef KOSDEC_ADX_FILE_H
#define KOSDEC_ADX_FILE_H
#include <kos/fs.h>
#include <kosdec/adx_stream.h>
#ifdef __cplusplus
extern "C" {
#endif
struct kosdec_adx_file {
    struct kosdec_adx_stream stream;
    file_t descriptor;
    int io_errno;
    uint8_t *cache;
    size_t cache_capacity, cache_used;
    uint32_t cache_offset;
};
/* Borrow an already opened, seekable, immutable file, size<=UINT32_MAX.
   Caller keeps descriptor alive and exclusively controls its cursor while using
   this binding; no other seek/read/close, including through dup aliases.
   Init queries size; stream_read synchronously seeks/reads and may block. Use a
   suitable calling thread/fiber, never IRQ or time-critical audio callbacks.
   Pause/release/cancel/read use file.stream. Cancellation is between reads; no
   backend transfer remains using reader storage when a read returns. This does
   not cancel or retire unrelated backend work. Never closes the descriptor.
   io_errno captures last backend error; successful init returns MORE. */
enum kosdec_adx_status kosdec_adx_file_init(struct kosdec_adx_file *,
                                         file_t descriptor, bool repeat);
/* Optional compressed read-ahead, same ownership/serialization as init above.
   Caller supplies 512..32768 bytes, disjoint from the file object, input/output
   and other live buffers; keep it alive until all source users are joined.
   At most one seek/read per cache miss, reading at most cache_bytes or the
   remaining file extent. Hits perform no VFS operation. Short reads are valid;
   WAIT/error invalidates the refill. Seek/loop may reuse immutable cached bytes.
   No allocation, descriptor close, asynchronous I/O or PCM predecode. The VFS
   handler's alignment/transport requirements still apply to the supplied buffer.
   Invalid buffer arguments return BAD_ARGUMENT without I/O or state changes.
   Reinitialization requires retired source users, as for the unbuffered binding. */
enum kosdec_adx_status kosdec_adx_file_init_buffered(struct kosdec_adx_file *,
    file_t descriptor, bool repeat, uint8_t *cache, size_t cache_bytes);
#ifdef __cplusplus
}
#endif
#endif
