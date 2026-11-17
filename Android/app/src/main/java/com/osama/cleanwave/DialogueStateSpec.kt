package com.osama.cleanwave

internal object DialogueStateSpec {
    val names = arrayOf("erb_norm_state","band_unit_norm_state","analysis_mem","synthesis_mem","rolling_erb_buf","rolling_feat_spec_buf","rolling_c0_buf","rolling_spec_buf_x","rolling_spec_buf_y","enc_hidden","erb_dec_hidden","df_dec_hidden")
    val shapes = arrayOf(longArrayOf(32L),longArrayOf(1L,96L,1L),longArrayOf(512L),longArrayOf(512L),longArrayOf(1L,1L,3L,32L),longArrayOf(1L,2L,3L,96L),longArrayOf(1L,64L,5L,96L),longArrayOf(5L,513L,2L),longArrayOf(7L,513L,2L),longArrayOf(1L,1L,256L),longArrayOf(2L,1L,256L),longArrayOf(2L,1L,256L))
}
