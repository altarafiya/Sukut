package com.osama.cleanwave;

import com.osama.cleanwave.IOutputRouteListener;

interface ICleanWaveService {
    boolean startPassthrough() = 1;
    void stopProcessing() = 2;
    boolean isRunning() = 3;
    String getStatus() = 4;
    String getIdentity() = 5;
    void setMusicReduction(int percent) = 6;
    String getProcessingMetrics() = 7;
    void setProcessingMode(int mode) = 8;
    void setVoiceBoost(int db) = 9;
    void setSourceMix(int voiceDb, int musicDb, int otherDb) = 10;
    void setPlaybackOptions(int buffering, int amplificationDb, boolean silentRecovery, boolean effectsCompatibility, int sourceUid) = 11;
    void setOutputRouteListener(IOutputRouteListener listener) = 12;
    void stopForRestart() = 13;
    void destroy() = 16777114;
}
