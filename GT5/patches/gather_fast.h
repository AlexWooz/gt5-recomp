/* Big-endian indices to vertex references, 8 (16-bit) or 4 (32-bit) at a time, until a block holds
 * the restart value or fewer than a block remain. Returns how many were done; the caller does the
 * rest one at a time, so a restart is always handled by the ordinary path. */
static u32 ld_gather_fast(const u8* src, u32 count, int is_u32, int restart_enabled, u32 restart_value,
                          u32 base_index, rsx_vertex_ref* out)
{
    u32 i = 0;
    const __m128i base = _mm_set1_epi32((int)base_index);
    if (!is_u32) {
        const __m128i sw = _mm_setr_epi8(1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14);
        const int chk = restart_enabled && restart_value <= 0xFFFFu;
        const __m128i rv = _mm_set1_epi16((short)(restart_value & 0xFFFFu));
        for (; i + 8 <= count; i += 8) {
            const __m128i x = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)(src + (size_t)i * 2u)), sw);
            if (chk && _mm_movemask_epi8(_mm_cmpeq_epi16(x, rv))) break;
            const __m128i lo = _mm_cvtepu16_epi32(x);
            const __m128i hi = _mm_cvtepu16_epi32(_mm_srli_si128(x, 8));
            _mm_storeu_si128((__m128i*)(out + i), _mm_unpacklo_epi32(lo, base));
            _mm_storeu_si128((__m128i*)(out + i + 2), _mm_unpackhi_epi32(lo, base));
            _mm_storeu_si128((__m128i*)(out + i + 4), _mm_unpacklo_epi32(hi, base));
            _mm_storeu_si128((__m128i*)(out + i + 6), _mm_unpackhi_epi32(hi, base));
        }
    } else {
        const __m128i sw = _mm_setr_epi8(3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12);
        const __m128i rv = _mm_set1_epi32((int)restart_value);
        for (; i + 4 <= count; i += 4) {
            const __m128i x = _mm_shuffle_epi8(_mm_loadu_si128((const __m128i*)(src + (size_t)i * 4u)), sw);
            if (restart_enabled && _mm_movemask_epi8(_mm_cmpeq_epi32(x, rv))) break;
            _mm_storeu_si128((__m128i*)(out + i), _mm_unpacklo_epi32(x, base));
            _mm_storeu_si128((__m128i*)(out + i + 2), _mm_unpackhi_epi32(x, base));
        }
    }
    return i;
}
