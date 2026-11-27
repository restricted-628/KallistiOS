/* Owned asset constructor, shared by the direct driver and its host test. */
int gdrom_direct_load_aica(uint32_t *sound_offset, uint32_t fad, size_t sectors,
                           gdrom_direct_sector_type_t sector_type,
                           uint32_t timeout, gdrom_direct_result_t *result) {
    snd_mem_status_t pool;
    size_t bytes = dma_range_bytes(sectors, sector_type);
    uint32_t offset;

    if(result)
        memset(result, 0, sizeof(*result));
    if(!sound_offset) {
        errno = EINVAL;
        return -1;
    }
    if(aica_read_args(fad, sectors, bytes, timeout) < 0)
        return -1;
    if(bytes > SPU_RAM_SIZE_2MB - AICA_RAM_START) {
        errno = EFBIG;
        return -1;
    }
    /* A caller-created pool may include the driver/shared prefix, or start
       at zero (also the allocator's failure sentinel). Exclude such pools
       before allocation. The loading phase excludes global pool reset. */
    if(snd_mem_get_status(&pool) < 0)
        return -1;
    if(!pool.initialized || pool.pool_base < AICA_RAM_START
            || pool.pool_base >= SPU_RAM_SIZE_2MB
            || pool.pool_size > SPU_RAM_SIZE_2MB - pool.pool_base) {
        errno = ENODEV;
        return -1;
    }
    offset = snd_mem_malloc(bytes);
    if(!offset)
        return -1;

    if(gdrom_direct_read_sectors_dma_aica(offset, fad, sectors, sector_type,
                                         timeout, result) < 0) {
        /* Transport can return after partial DMA, but never before stop and
           destination drain. The block has not been published to any user. */
        int cause = errno;
        snd_mem_free(offset);
        errno = cause;
        return -1;
    }
    *sound_offset = offset;
    return 0;
}
