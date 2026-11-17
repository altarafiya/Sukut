package com.osama.cleanwave;

oneway interface IOutputRouteListener {
    void onOutputRouteChanged(int deviceId, int deviceType);
}
